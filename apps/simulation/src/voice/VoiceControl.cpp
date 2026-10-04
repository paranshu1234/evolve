#include "voice/VoiceControl.h"
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace evolve::voice {
namespace {
std::uint64_t nextCounter(std::uint64_t& counter) {
    if (counter == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("Voice session counter exhausted. Restart the application.");
    return ++counter;
}
bool plain(const Action& action) {
    return action.index == 0 && action.base == 0 && !action.enabled && action.scalar == 0.0;
}
bool booleanValue(const Action& action) {
    return action.index == 0 && action.base == 0 && action.scalar == 0.0;
}
bool validBase(char base) { return base == 'A' || base == 'C' || base == 'G' || base == 'T'; }
Decision denied(const char* message) { return {DecisionStatus::Rejected, std::nullopt, 0, message}; }
ParseResult parsed(const Action& action) { return {action, "Offline command recognized: " + describeAction(action)}; }
bool number(const std::string& text, std::size_t& value, std::size_t maximum) {
    if (text.empty()) return false;
    // A small spoken-number convenience for the deterministic offline grammar.
    static const char* words[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
        "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen", "twenty"};
    for (std::size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
        if (text == words[i]) { value = i; return value <= maximum; }
    }
    value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<std::size_t>(c - '0');
        if (digit > maximum || value > (maximum - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return true;
}
bool normalize(const std::string& text, std::string& output) {
    if (text.empty() || text.size() > 256) return false;
    bool space = false;
    for (const unsigned char c : text) {
        if (c == ' ' || c == '\t') { space = !output.empty(); continue; }
        if (space) { output.push_back(' '); space = false; }
        if (c >= 'A' && c <= 'Z') output.push_back(static_cast<char>(c - 'A' + 'a'));
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '?' || c == '!')
            output.push_back(static_cast<char>(c));
        else return false;
    }
    // Dictation commonly appends one terminal punctuation character. It cannot
    // introduce another command or hide unconsumed input.
    if (!output.empty() && (output.back() == '.' || output.back() == '?' || output.back() == '!')) output.pop_back();
    if (!output.empty() && output.back() == ' ') output.pop_back();
    if (output.empty()) return false;
    return output.find_first_of(".?!") == std::string::npos;
}
}

bool isValidAction(const Action& action, std::size_t sequenceLength) {
    if (sequenceLength == 0 || sequenceLength > MaxBases || !std::isfinite(action.scalar)) return false;
    switch (action.type) {
    case ActionType::SelectBase:
        return action.index < sequenceLength && action.base == 0 && !action.enabled && action.scalar == 0.0;
    case ActionType::EditBase:
        return action.index < sequenceLength && validBase(action.base) && !action.enabled && action.scalar == 0.0;
    case ActionType::SetCompare: case ActionType::SetGrid: case ActionType::SetRotation:
        return booleanValue(action);
    case ActionType::SetLighting:
        return action.index == 0 && action.base == 0 && !action.enabled && action.scalar >= 20.0 && action.scalar <= 160.0
            && std::floor(action.scalar) == action.scalar;
    case ActionType::Help: case ActionType::DescribeProject: case ActionType::Undo: case ActionType::Redo:
    case ActionType::RestoreBaseline: case ActionType::LoadDemo: case ActionType::ImportSequence:
    case ActionType::OpenProject: case ActionType::SaveProject: case ActionType::ExportResults:
    case ActionType::FrameAll: case ActionType::RunAnalysis: case ActionType::CancelAnalysis:
        return plain(action);
    }
    return false;
}
bool requiresConfirmation(ActionType type) {
    switch (type) {
    case ActionType::RestoreBaseline: case ActionType::LoadDemo: case ActionType::ImportSequence:
    case ActionType::OpenProject: case ActionType::SaveProject: case ActionType::ExportResults:
        return true;
    default: return false;
    }
}
std::string describeAction(const Action& action) {
    if (!isValidAction(action)) return "Unsupported action.";
    switch (action.type) {
    case ActionType::Help: return "Show supported commands.";
    case ActionType::DescribeProject: return "Describe the current project and composition-only limits.";
    case ActionType::SelectBase: return "Select base " + std::to_string(action.index + 1) + ".";
    case ActionType::EditBase: return "Change base " + std::to_string(action.index + 1) + " to " + action.base + ".";
    case ActionType::Undo: return "Undo the last sequence change.";
    case ActionType::Redo: return "Redo the last sequence change.";
    case ActionType::RestoreBaseline: return "Restore the original baseline sequence (undoable).";
    case ActionType::LoadDemo: return "Replace the project with the synthetic demo, after any unsaved-changes prompt.";
    case ActionType::ImportSequence: return "Choose a local DNA file to replace the project, after any unsaved-changes prompt.";
    case ActionType::OpenProject: return "Choose a local project to open, after any unsaved-changes prompt.";
    case ActionType::SaveProject: return "Save the project locally; this can overwrite its current file. A new destination uses a file dialog.";
    case ActionType::ExportResults: return "Choose a local CSV destination for current composition results; confirm any overwrite in the file dialog.";
    case ActionType::SetCompare: return action.enabled ? "Show baseline comparison." : "Hide baseline comparison.";
    case ActionType::SetGrid: return action.enabled ? "Show the grid." : "Hide the grid.";
    case ActionType::SetRotation: return action.enabled ? "Start rotation." : "Stop rotation.";
    case ActionType::FrameAll: return "Frame the entire sequence.";
    case ActionType::SetLighting: return "Set lighting to " + std::to_string(static_cast<int>(action.scalar)) + " percent.";
    case ActionType::RunAnalysis: return "Run mock composition analysis; no biological prediction.";
    case ActionType::CancelAnalysis: return "Cancel the current mock analysis.";
    }
    return "Unsupported action.";
}
const char* localCommandHelp() {
    return "Offline command mode (fixed English phrases, not conversational AI). Examples: help; describe project; "
        "select base 12; edit base 12 to C; undo; redo; restore baseline; load demo; import DNA; open project; "
        "save project; export results; compare on/off; grid on/off; rotation on/off; frame all; "
        "set lighting to 100; run analysis; cancel analysis. One command at a time. "
        "File and reset actions require a confirmation button. File paths cannot be spoken.";
}
ParseResult parseLocalCommand(const std::string& text) {
    std::string command;
    if (!normalize(text, command)) return {std::nullopt, "Use one supported offline command (up to 256 ASCII characters)."};
    struct Entry { const char* phrase; ActionType type; };
    static const Entry entries[] = {
        {"help", ActionType::Help}, {"show commands", ActionType::Help}, {"describe project", ActionType::DescribeProject},
        {"project status", ActionType::DescribeProject}, {"undo", ActionType::Undo}, {"redo", ActionType::Redo},
        {"restore baseline", ActionType::RestoreBaseline}, {"reset to baseline", ActionType::RestoreBaseline},
        {"load demo", ActionType::LoadDemo}, {"demo project", ActionType::LoadDemo},
        {"import dna", ActionType::ImportSequence}, {"import sequence", ActionType::ImportSequence},
        {"open project", ActionType::OpenProject}, {"save project", ActionType::SaveProject},
        {"export results", ActionType::ExportResults}, {"frame all", ActionType::FrameAll},
        {"run analysis", ActionType::RunAnalysis}, {"run mock analysis", ActionType::RunAnalysis},
        {"cancel analysis", ActionType::CancelAnalysis}
    };
    for (const auto& entry : entries) if (command == entry.phrase) return parsed(Action{entry.type});
    struct ValueEntry { const char* phrase; ActionType type; bool enabled; };
    static const ValueEntry values[] = {
        {"compare on", ActionType::SetCompare, true}, {"compare off", ActionType::SetCompare, false},
        {"show comparison", ActionType::SetCompare, true}, {"hide comparison", ActionType::SetCompare, false},
        {"grid on", ActionType::SetGrid, true}, {"grid off", ActionType::SetGrid, false},
        {"show grid", ActionType::SetGrid, true}, {"hide grid", ActionType::SetGrid, false},
        {"rotation on", ActionType::SetRotation, true}, {"rotation off", ActionType::SetRotation, false},
        {"start rotation", ActionType::SetRotation, true}, {"stop rotation", ActionType::SetRotation, false}
    };
    for (const auto& entry : values) if (command == entry.phrase) {
        Action action{entry.type}; action.enabled = entry.enabled; return parsed(action);
    }
    std::istringstream input(command);
    std::vector<std::string> tokens;
    for (std::string token; input >> token;) tokens.push_back(token);
    std::size_t position{};
    if (tokens.size() == 3 && tokens[0] == "select" && tokens[1] == "base" && number(tokens[2], position, MaxBases) && position > 0) {
        Action action{ActionType::SelectBase}; action.index = position - 1; return parsed(action);
    }
    if (tokens.size() == 5 && (tokens[0] == "edit" || tokens[0] == "change" || tokens[0] == "set") && tokens[1] == "base"
        && tokens[3] == "to" && number(tokens[2], position, MaxBases) && position > 0 && tokens[4].size() == 1) {
        Action action{ActionType::EditBase}; action.index = position - 1; action.base = static_cast<char>(tokens[4][0] - 'a' + 'A');
        if (isValidAction(action)) return parsed(action);
    }
    if (tokens.size() == 4 && tokens[0] == "set" && tokens[1] == "lighting" && tokens[2] == "to" && number(tokens[3], position, 160)) {
        Action action{ActionType::SetLighting}; action.scalar = static_cast<double>(position);
        if (isValidAction(action)) return parsed(action);
    }
    return {std::nullopt, "Unrecognized offline command. Say or type 'help' for the fixed command list."};
}

void Controller::clearPending() { active_ = {}; pending_.reset(); confirmationToken_ = 0; }
std::uint64_t Controller::beginSession() {
    nextCounter(session_); clearPending(); state_ = State::Idle; return session_;
}
void Controller::endSession() { clearPending(); state_ = State::Disabled; }
void Controller::cancel() { clearPending(); if (state_ != State::Disabled) state_ = State::Idle; }
RequestTicket Controller::beginRequest(std::uint64_t projectEpoch, std::uint64_t revision) {
    if (state_ == State::Disabled) return {};
    const auto request = nextCounter(requestCounter_);
    clearPending(); active_ = {session_, request, projectEpoch, revision}; state_ = State::Processing; return active_;
}
bool Controller::matches(const RequestTicket& ticket) const {
    return ticket.session != 0 && ticket.request != 0 && ticket.session == active_.session && ticket.request == active_.request
        && ticket.projectEpoch == active_.projectEpoch && ticket.revision == active_.revision;
}
Decision Controller::accept(const RequestTicket& ticket, const Action& action,
                            std::uint64_t currentEpoch, std::uint64_t currentRevision, std::size_t sequenceLength) {
    if (state_ != State::Processing || !matches(ticket)) return denied("Ignored a cancelled, replaced, or already handled voice response.");
    if (ticket.projectEpoch != currentEpoch || ticket.revision != currentRevision) {
        cancel(); return denied("The project changed while this request was pending. Please ask again.");
    }
    if (!isValidAction(action, sequenceLength)) { cancel(); return denied("Rejected an unsupported or out-of-range action."); }
    if (requiresConfirmation(action.type)) {
        const auto token = nextCounter(tokenCounter_);
        pending_ = action; confirmationToken_ = token; state_ = State::AwaitingConfirmation;
        return {DecisionStatus::NeedsConfirmation, std::nullopt, token, "Confirm: " + describeAction(action)};
    }
    cancel(); return {DecisionStatus::Ready, action, 0, describeAction(action)};
}
Decision Controller::confirm(std::uint64_t token, std::uint64_t currentEpoch,
                             std::uint64_t currentRevision, std::size_t sequenceLength) {
    if (state_ != State::AwaitingConfirmation || token == 0 || token != confirmationToken_ || !pending_)
        return denied("There is no matching pending confirmation.");
    if (active_.projectEpoch != currentEpoch || active_.revision != currentRevision) {
        cancel(); return denied("The project or destination changed. Ask again before confirming.");
    }
    const auto action = *pending_;
    cancel(); // Consume the token before dispatch, including validation failures.
    if (!isValidAction(action, sequenceLength)) return denied("The pending action is no longer valid.");
    return {DecisionStatus::Ready, action, 0, describeAction(action)};
}
bool Controller::reject(const RequestTicket& ticket) {
    if (state_ != State::Processing || !matches(ticket)) return false;
    cancel(); return true;
}
}
