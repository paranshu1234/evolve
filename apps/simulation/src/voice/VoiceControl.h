#pragma once
#include "core/Project.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace evolve::voice {
// These are the only operations a voice/text provider can propose. Paths, shell
// commands, scripts, arbitrary sequence imports, and confirmation are absent.
enum class ActionType {
    Help, DescribeProject, SelectBase, EditBase, Undo, Redo, RestoreBaseline,
    LoadDemo, ImportSequence, OpenProject, SaveProject, ExportResults,
    SetCompare, SetGrid, SetRotation, FrameAll, SetLighting, RunAnalysis, CancelAnalysis
};
struct Action {
    ActionType type{ActionType::Help};
    std::size_t index{}; // Zero-based, even though local commands use positions 1–256.
    char base{};         // Uppercase A, C, G, or T for EditBase only.
    bool enabled{};      // Explicit desired value, never a toggle.
    double scalar{};     // Integer lighting percentage, 20–160.
};
// Non-applicable fields must retain their default values. This rejects ambiguous
// or malformed provider payloads in addition to checking the enum/ranges.
bool isValidAction(const Action& action, std::size_t sequenceLength = MaxBases);
bool requiresConfirmation(ActionType type);
std::string describeAction(const Action& action);
const char* localCommandHelp();
struct ParseResult {
    std::optional<Action> action;
    std::string message;
};
// A narrow offline English command parser, not a conversational AI. Input is
// bounded to 256 ASCII bytes; no substring matching, chaining, or path arguments.
ParseResult parseLocalCommand(const std::string& text);

enum class State { Disabled, Idle, Processing, AwaitingConfirmation };
enum class DecisionStatus { Ready, NeedsConfirmation, Rejected };
struct RequestTicket {
    std::uint64_t session{};
    std::uint64_t request{};
    std::uint64_t projectEpoch{};
    std::uint64_t revision{};
};
struct Decision {
    DecisionStatus status{DecisionStatus::Rejected};
    std::optional<Action> action; // Set only when Ready. Never execute a proposal.
    std::uint64_t confirmationToken{}; // Nonzero only when NeedsConfirmation.
    std::string message;
};

// UI-thread-owned gate; call it only on the owning UI thread. Provider workers
// carry copies of tickets and post completions back to that thread. Every request
// supersedes the previous request AND its confirmation. Even same-revision
// replacements must advance projectEpoch. Advance epoch when the active save
// destination changes too, so an old confirmation cannot target a different file.
class Controller {
public:
    std::uint64_t beginSession();
    void endSession();
    void cancel();
    RequestTicket beginRequest(std::uint64_t projectEpoch, std::uint64_t revision);
    Decision accept(const RequestTicket& ticket, const Action& action,
                    std::uint64_t currentEpoch, std::uint64_t currentRevision,
                    std::size_t sequenceLength = MaxBases);
    // Call only from an explicit user confirmation UI, never from a provider.
    // Uses the stored exact proposal, not a caller-supplied replacement action.
    // File actions still use native file dialogs and their overwrite warnings.
    Decision confirm(std::uint64_t token, std::uint64_t currentEpoch,
                     std::uint64_t currentRevision, std::size_t sequenceLength = MaxBases);
    // Consume an invalid/failed provider response without affecting a newer one.
    bool reject(const RequestTicket& ticket);
    State state() const { return state_; }
    std::uint64_t session() const { return session_; }
    std::uint64_t pendingConfirmation() const { return confirmationToken_; }
private:
    bool matches(const RequestTicket& ticket) const;
    void clearPending();
    State state_{State::Disabled};
    std::uint64_t session_{};
    std::uint64_t requestCounter_{};
    std::uint64_t tokenCounter_{};
    std::uint64_t confirmationToken_{};
    RequestTicket active_;
    std::optional<Action> pending_;
};
}
