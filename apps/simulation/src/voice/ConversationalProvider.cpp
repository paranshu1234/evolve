#include "ConversationalProvider.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace evolve::voice {
namespace {
constexpr std::size_t MaxResponseBytes = 128 * 1024;
constexpr std::size_t MaxTranscriptBytes = 2048;
constexpr std::size_t MaxSpeechBytes = 4096;

// No third-party parser dependency. Every external JSON value is bounded; duplicate
// keys, malformed UTF-8, non-finite numbers and trailing data fail closed.
struct Json {
    enum class Kind { Null, Boolean, Number, String, Array, Object } kind{Kind::Null};
    bool boolean{};
    double number{};
    std::string string;
    std::vector<Json> array;
    std::map<std::string, Json> object;
};

bool validUtf8(const std::string& text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first < 0x80) continue;
        unsigned count{};
        std::uint32_t code{};
        if (first >= 0xc2 && first <= 0xdf) { count = 1; code = first & 0x1f; }
        else if (first >= 0xe0 && first <= 0xef) { count = 2; code = first & 0x0f; }
        else if (first >= 0xf0 && first <= 0xf4) { count = 3; code = first & 0x07; }
        else return false;
        if (i + count > text.size()) return false;
        for (unsigned n = 0; n < count; ++n) {
            const auto byte = static_cast<unsigned char>(text[i++]);
            if ((byte & 0xc0) != 0x80) return false;
            code = (code << 6) | (byte & 0x3f);
        }
        if ((count == 1 && code < 0x80) || (count == 2 && code < 0x800) ||
            (count == 3 && code < 0x10000) || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text) {}
    Json parse() {
        if (text_.size() > MaxResponseBytes || !validUtf8(text_)) fail();
        auto result = value(0);
        whitespace();
        if (pos_ != text_.size()) fail();
        return result;
    }
private:
    [[noreturn]] static void fail() { throw std::runtime_error("Invalid provider JSON."); }
    void whitespace() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' ||
               text_[pos_] == '\n' || text_[pos_] == '\r')) ++pos_;
    }
    bool take(char c) {
        whitespace();
        if (pos_ < text_.size() && text_[pos_] == c) { ++pos_; return true; }
        return false;
    }
    void expect(char c) { if (!take(c)) fail(); }
    void literal(const char* token) {
        for (; *token; ++token) if (pos_ >= text_.size() || text_[pos_++] != *token) fail();
    }
    std::uint32_t hex4() {
        std::uint32_t result{};
        for (int n = 0; n < 4; ++n) {
            if (pos_ == text_.size()) fail();
            const char c = text_[pos_++];
            unsigned digit{};
            if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
            else fail();
            result = result * 16 + digit;
        }
        return result;
    }
    static void utf8(std::string& out, std::uint32_t code) {
        if (code < 0x80) out += static_cast<char>(code);
        else if (code < 0x800) {
            out += static_cast<char>(0xc0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3f));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xe0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        }
    }
    std::string string() {
        expect('"');
        std::string result;
        while (pos_ < text_.size()) {
            const auto c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"') return result;
            if (c < 0x20 || result.size() >= 16384) fail();
            if (c != '\\') { result += static_cast<char>(c); continue; }
            if (pos_ == text_.size()) fail();
            switch (text_[pos_++]) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'u': {
                    auto code = hex4();
                    if (code >= 0xd800 && code <= 0xdbff) {
                        if (pos_ + 2 > text_.size() || text_[pos_++] != '\\' || text_[pos_++] != 'u') fail();
                        const auto low = hex4();
                        if (low < 0xdc00 || low > 0xdfff) fail();
                        code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                    } else if (code >= 0xdc00 && code <= 0xdfff) fail();
                    utf8(result, code);
                    break;
                }
                default: fail();
            }
        }
        fail();
    }
    Json number() {
        const auto begin = pos_;
        if (text_[pos_] == '-') ++pos_;
        if (pos_ == text_.size()) fail();
        if (text_[pos_] == '0') ++pos_;
        else {
            if (text_[pos_] < '1' || text_[pos_] > '9') fail();
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            const auto start = pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
            if (pos_ == start) fail();
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            const auto start = pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
            if (pos_ == start) fail();
        }
        if (pos_ - begin > 64) fail();
        Json result;
        result.kind = Json::Kind::Number;
        std::istringstream input(text_.substr(begin, pos_ - begin));
        input.imbue(std::locale::classic());
        if (!(input >> result.number) || !input.eof() || !std::isfinite(result.number)) fail();
        return result;
    }
    Json value(unsigned depth) {
        whitespace();
        if (depth > 24 || ++nodes_ > 4096 || pos_ == text_.size()) fail();
        Json result;
        switch (text_[pos_]) {
            case 'n': literal("null"); return result;
            case 't': literal("true"); result.kind = Json::Kind::Boolean; result.boolean = true; return result;
            case 'f': literal("false"); result.kind = Json::Kind::Boolean; return result;
            case '"': result.kind = Json::Kind::String; result.string = string(); return result;
            case '[':
                ++pos_; result.kind = Json::Kind::Array;
                if (take(']')) return result;
                do { result.array.push_back(value(depth + 1)); } while (take(','));
                expect(']'); return result;
            case '{':
                ++pos_; result.kind = Json::Kind::Object;
                if (take('}')) return result;
                do {
                    auto key = string();
                    expect(':');
                    auto member = value(depth + 1);
                    if (!result.object.emplace(std::move(key), std::move(member)).second) fail();
                } while (take(','));
                expect('}'); return result;
            default: return number();
        }
    }
    const std::string& text_;
    std::size_t pos_{};
    std::size_t nodes_{};
};

const Json* member(const Json& object, const char* key) {
    if (object.kind != Json::Kind::Object) return nullptr;
    const auto found = object.object.find(key);
    return found == object.object.end() ? nullptr : &found->second;
}
const Json& required(const Json& object, const char* key, Json::Kind kind) {
    const auto* result = member(object, key);
    if (!result || result->kind != kind) throw std::runtime_error("Invalid provider response.");
    return *result;
}
bool isText(const Json& object, const char* key, const char* text) {
    const auto* value = member(object, key);
    return value && value->kind == Json::Kind::String && value->string == text;
}

std::string quote(const std::string& text) {
    std::string result{"\""};
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : text) {
        if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 0x20) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
        else result += static_cast<char>(c);
    }
    return result + '"';
}

struct ToolAction { const char* name; ActionType type; };
constexpr ToolAction Actions[] = {
    {"help", ActionType::Help}, {"describe_project", ActionType::DescribeProject},
    {"select_base", ActionType::SelectBase}, {"edit_base", ActionType::EditBase},
    {"undo", ActionType::Undo}, {"redo", ActionType::Redo},
    {"restore_baseline", ActionType::RestoreBaseline}, {"load_demo", ActionType::LoadDemo},
    {"import_sequence", ActionType::ImportSequence}, {"open_project", ActionType::OpenProject},
    {"save_project", ActionType::SaveProject}, {"export_results", ActionType::ExportResults},
    {"set_compare", ActionType::SetCompare}, {"set_grid", ActionType::SetGrid},
    {"set_rotation", ActionType::SetRotation}, {"frame_all", ActionType::FrameAll},
    {"set_lighting", ActionType::SetLighting}, {"run_analysis", ActionType::RunAnalysis},
    {"cancel_analysis", ActionType::CancelAnalysis}
};

Action parseAction(const std::string& text) {
    if (text.size() > 2048) throw std::runtime_error("Oversized action.");
    const auto args = JsonParser(text).parse();
    if (args.kind != Json::Kind::Object || args.object.size() != 5) throw std::runtime_error("Invalid action fields.");
    const auto& name = required(args, "action", Json::Kind::String).string;
    const auto found = std::find_if(std::begin(Actions), std::end(Actions),
        [&](const ToolAction& item) { return name == item.name; });
    if (found == std::end(Actions)) throw std::runtime_error("Unknown action.");
    Action action;
    action.type = found->type;
    const auto* index = member(args, "index");
    const auto* base = member(args, "base");
    const auto* enabled = member(args, "enabled");
    const auto* scalar = member(args, "scalar");
    if (!index || !base || !enabled || !scalar) throw std::runtime_error("Missing action fields.");
    const bool usesIndex = action.type == ActionType::SelectBase || action.type == ActionType::EditBase;
    const bool usesBase = action.type == ActionType::EditBase;
    const bool usesEnabled = action.type == ActionType::SetCompare || action.type == ActionType::SetGrid || action.type == ActionType::SetRotation;
    const bool usesScalar = action.type == ActionType::SetLighting;
    if (usesIndex) {
        if (index->kind != Json::Kind::Number || index->number < 0 || index->number >= MaxBases ||
            std::floor(index->number) != index->number) throw std::runtime_error("Invalid base index.");
        action.index = static_cast<std::size_t>(index->number);
    } else if (index->kind != Json::Kind::Null) throw std::runtime_error("Unexpected index.");
    if (usesBase) {
        if (base->kind != Json::Kind::String || base->string.size() != 1) throw std::runtime_error("Invalid base.");
        action.base = base->string[0];
    } else if (base->kind != Json::Kind::Null) throw std::runtime_error("Unexpected base.");
    if (usesEnabled) {
        if (enabled->kind != Json::Kind::Boolean) throw std::runtime_error("Invalid toggle.");
        action.enabled = enabled->boolean;
    } else if (enabled->kind != Json::Kind::Null) throw std::runtime_error("Unexpected toggle.");
    if (usesScalar) {
        if (scalar->kind != Json::Kind::Number) throw std::runtime_error("Invalid lighting.");
        action.scalar = scalar->number;
    } else if (scalar->kind != Json::Kind::Null) throw std::runtime_error("Unexpected lighting.");
    if (!isValidAction(action)) throw std::runtime_error("Action outside allowed bounds.");
    return action;
}

std::string toolsJson() {
    std::string names;
    for (const auto& action : Actions) { if (!names.empty()) names += ','; names += quote(action.name); }
    return "[{\"type\":\"function\",\"name\":\"evolve_action\",\"description\":" +
        quote("Propose exactly one requested Evolve UI action. The local UI validates it and handles confirmation. All unused arguments must be null. Never claim it succeeded.") +
        ",\"strict\":true,\"parameters\":{\"type\":\"object\",\"additionalProperties\":false,\"properties\":{" +
        "\"action\":{\"type\":\"string\",\"enum\":[" + names + "]}," +
        "\"index\":{\"type\":[\"integer\",\"null\"],\"description\":\"Zero-based index for select_base or edit_base only; spoken position 1 is index 0.\",\"minimum\":0,\"maximum\":255}," +
        "\"base\":{\"type\":[\"string\",\"null\"],\"enum\":[\"A\",\"C\",\"G\",\"T\",null]}," +
        "\"enabled\":{\"type\":[\"boolean\",\"null\"],\"description\":\"Explicit on/off value for set_compare, set_grid or set_rotation only.\"}," +
        "\"scalar\":{\"type\":[\"integer\",\"null\"],\"description\":\"Lighting slider value 20 through 160 for set_lighting only.\",\"minimum\":20,\"maximum\":160}}," +
        "\"required\":[\"action\",\"index\",\"base\",\"enabled\",\"scalar\"]}}]";
}

void validateText(const std::string& text) {
    if (text.empty() || text.size() > MaxTranscriptBytes || !validUtf8(text) ||
        std::all_of(text.begin(), text.end(), [](unsigned char c) { return c == ' '; }))
        throw std::invalid_argument("Cloud text must be valid UTF-8 and contain 1 to 2048 bytes.");
    std::size_t dnaRun{};
    for (const unsigned char c : text) {
        if (c < 0x20 || c == 0x7f || c == '/' || c == '\\')
            throw std::invalid_argument("Cloud text cannot contain control characters, URLs or paths. Use the local file controls.");
        const auto upper = (c >= 'a' && c <= 'z') ? static_cast<unsigned char>(c - 'a' + 'A') : c;
        if (upper == 'A' || upper == 'C' || upper == 'G' || upper == 'T') ++dnaRun;
        else if (c != ' ' && c != ',' && c != '-') dnaRun = 0;
        if (dnaRun >= 8) throw std::invalid_argument("Sequence-like text stays local. Use a short command without sequence data.");
    }
    std::string lower = text;
    for (char& c : lower) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    for (const char* extension : {".fasta", ".fa", ".fna", ".evolve", ".csv", ".json", ".txt", ".dna", ".xml"})
        if (lower.find(extension) != std::string::npos)
            throw std::invalid_argument("Filenames stay local. Ask to open the local file picker instead.");
}

void validateRequest(const ProviderRequest& request, const std::string& model) {
    if (model.empty() || model.size() > 80 || std::any_of(model.begin(), model.end(), [](unsigned char c) {
            return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.');
        })) throw std::invalid_argument("Invalid cloud model identifier.");
    validateText(request.transcript);
    if (request.conversation.size() > 4) throw std::invalid_argument("Cloud conversation is limited to four prior turns.");
    for (const auto& turn : request.conversation) { validateText(turn.user); validateText(turn.assistant); }
    const auto& state = request.context;
    if (state.sequenceLength > MaxBases || state.editCount > state.sequenceLength ||
        !std::isfinite(state.gcPercent) || state.gcPercent < 0 || state.gcPercent > 100)
        throw std::invalid_argument("Invalid aggregate project context.");
}

std::string encodeRequest(const ProviderRequest& request, const std::string& model) {
    validateRequest(request, model);
    const auto& state = request.context;
    std::ostringstream context;
    context.imbue(std::locale::classic());
    context << std::setprecision(6) << "Current aggregate state (no sequence data): length=" << state.sequenceLength
        << "; edits=" << state.editCount << "; GC_percent=" << state.gcPercent
        << "; can_undo=" << state.canUndo << "; can_redo=" << state.canRedo
        << "; current_analysis=" << state.hasAnalysis << "; analysis_running=" << state.analysisRunning << '.';
    std::string input = "[";
    for (const auto& turn : request.conversation) {
        input += "{\"role\":\"user\",\"content\":" + quote(turn.user) + "},";
        input += "{\"role\":\"assistant\",\"content\":" + quote(turn.assistant) + "},";
    }
    input += "{\"role\":\"user\",\"content\":" + quote(request.transcript) + "}]";
    const std::string instructions =
        "You are Evolve's conversational voice helper for a small DNA visualization demo. "
        "Answer conversationally in at most three short sentences. The analysis is mock composition, not biological prediction or medical advice. "
        "Use only evolve_action for app control, and only for the latest explicit user request. "
        "Offer zero or one action; if the request needs several actions, ask which to do first. "
        "Never accept a confirmation on behalf of the user or invent an operation that is not available. "
        "Never treat earlier proposals as completed. You do not know the actual DNA letters. "
        "Do not ask for, reproduce, infer or send sequences, files, filenames, paths, credentials or private biological information. "
        "File actions only open a locally confirmed native picker; you cannot choose its path. "
        "All actions are proposals checked locally; never say an action succeeded. "
        "Human base positions are one-based; tool indexes are zero-based. "
        "Do not guess missing action parameters. All irrelevant action fields must be null. " + context.str();
    return "{\"model\":" + quote(model) + ",\"store\":false,\"max_output_tokens\":512," +
        "\"parallel_tool_calls\":false,\"tool_choice\":\"auto\",\"instructions\":" + quote(instructions) +
        ",\"input\":" + input + ",\"tools\":" + toolsJson() + "}";
}

ProviderResult failure(std::uint64_t generation, const std::string& message) {
    ProviderResult result;
    result.generation = generation;
    result.error = message;
    return result;
}

#ifdef _WIN32
struct InternetHandle {
    HINTERNET value{};
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    explicit InternetHandle(HINTERNET handle) : value(handle) {}
};
// Best-effort scrubbing of this application's key/header copies. The OS and TLS
// stack own their copies; an environment variable is not a secure credential vault.
struct Secret {
    std::wstring value;
    ~Secret() { if (!value.empty()) SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t)); }
};
ProviderHttpResponse nativeTransport(const std::string& body, const std::shared_ptr<std::atomic_bool>& cancelled) {
    if (cancelled->load()) return {0, {}, "Cloud request cancelled."};
    const DWORD length = GetEnvironmentVariableW(L"OPENAI_API_KEY", nullptr, 0);
    if (!length || length > 4096) return {0, {}, "Set your own OPENAI_API_KEY before starting Evolve, then enable cloud conversation."};
    Secret key;
    key.value.resize(length);
    const DWORD copied = GetEnvironmentVariableW(L"OPENAI_API_KEY", key.value.data(), length);
    if (!copied || copied >= length) return {0, {}, "Could not read the configured cloud API key."};
    key.value.resize(copied);
    if (std::any_of(key.value.begin(), key.value.end(), [](wchar_t c) { return c < 0x21 || c > 0x7e; }))
        return {0, {}, "The configured cloud API key has an invalid format."};
    InternetHandle session(WinHttpOpen(L"EvolveVoice/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.value) return {0, {}, "Could not initialize the Windows HTTPS client."};
    if (!WinHttpSetTimeouts(session.value, 5000, 5000, 10000, 10000))
        return {0, {}, "Could not configure HTTPS timeouts."};
    InternetHandle connection(WinHttpConnect(session.value, L"api.openai.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection.value) return {0, {}, "Could not connect to the cloud provider."};
    InternetHandle request(WinHttpOpenRequest(connection.value, L"POST", L"/v1/responses", nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.value) return {0, {}, "Could not initialize the cloud request."};
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    DWORD disable = WINHTTP_DISABLE_COOKIES;
    DWORD autologon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)) ||
        !WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)) ||
        !WinHttpSetOption(request.value, WINHTTP_OPTION_AUTOLOGON_POLICY, &autologon, sizeof(autologon)))
        return {0, {}, "Could not configure safe HTTPS request options."};
    Secret headers;
    headers.value = L"Content-Type: application/json\r\nAuthorization: Bearer " + key.value + L"\r\n";
    if (cancelled->load()) return {0, {}, "Cloud request cancelled."};
    if (!WinHttpSendRequest(request.value, headers.value.c_str(), static_cast<DWORD>(headers.value.size()),
            const_cast<char*>(body.data()), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) ||
        cancelled->load() || !WinHttpReceiveResponse(request.value, nullptr))
        return {0, {}, "Cloud request failed or timed out. Check your connection and try again."};
    DWORD status{}, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
        return {0, {}, "Could not read the cloud response status."};
    ProviderHttpResponse result;
    result.status = static_cast<int>(status);
    if (status != 200) return result; // Never propagate remote error bodies.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    for (;;) {
        if (cancelled->load()) return {0, {}, "Cloud request cancelled."};
        if (std::chrono::steady_clock::now() > deadline) return {0, {}, "Cloud response timed out."};
        DWORD available{};
        if (!WinHttpQueryDataAvailable(request.value, &available)) return {0, {}, "Could not read the cloud response."};
        if (!available) return result;
        if (available > MaxResponseBytes - result.body.size()) return {0, {}, "Cloud response exceeded the size limit."};
        const auto oldSize = result.body.size();
        result.body.resize(oldSize + available);
        DWORD read{};
        if (!WinHttpReadData(request.value, result.body.data() + oldSize, available, &read) || !read)
            return {0, {}, "Could not read the complete cloud response."};
        result.body.resize(oldSize + read);
    }
}
#else
ProviderHttpResponse nativeTransport(const std::string&, const std::shared_ptr<std::atomic_bool>&) {
    return {0, {}, "The live cloud provider requires the Windows desktop build."};
}
#endif

} // namespace

bool canRetainConversationTurn(const ConversationTurn& turn) noexcept {
    try { validateText(turn.user); validateText(turn.assistant); return true; }
    catch (...) { return false; }
}

std::string buildProviderRequest(const ProviderRequest& request, const std::string& model) {
    return encodeRequest(request, model);
}

ProviderResult parseProviderResponse(const std::string& response, std::uint64_t generation) {
    try {
        const auto root = JsonParser(response).parse();
        if (!isText(root, "status", "completed")) return failure(generation, "The cloud response was incomplete. No action was accepted.");
        const auto& output = required(root, "output", Json::Kind::Array).array;
        if (output.size() > 32) throw std::runtime_error("Too many output items.");
        ProviderResult result;
        result.generation = generation;
        bool refusal = false;
        for (const auto& item : output) {
            const auto& type = required(item, "type", Json::Kind::String).string;
            if (type == "function_call") {
                if (result.action || !isText(item, "name", "evolve_action")) throw std::runtime_error("Unexpected tool call.");
                if (const auto* status = member(item, "status"); status &&
                    (status->kind != Json::Kind::String || status->string != "completed")) throw std::runtime_error("Incomplete tool call.");
                result.action = parseAction(required(item, "arguments", Json::Kind::String).string);
            } else if (type == "message") {
                if (!isText(item, "role", "assistant")) throw std::runtime_error("Unexpected message role.");
                if (const auto* status = member(item, "status"); status &&
                    (status->kind != Json::Kind::String || status->string != "completed")) throw std::runtime_error("Incomplete message.");
                const auto& content = required(item, "content", Json::Kind::Array).array;
                for (const auto& part : content) {
                    const auto& partType = required(part, "type", Json::Kind::String).string;
                    std::string text;
                    if (partType == "output_text") text = required(part, "text", Json::Kind::String).string;
                    else if (partType == "refusal") { text = required(part, "refusal", Json::Kind::String).string; refusal = true; }
                    else throw std::runtime_error("Unexpected content type.");
                    for (const unsigned char c : text)
                        if ((c < 0x20 && c != '\n' && c != '\r' && c != '\t') || c == 0x7f)
                            throw std::runtime_error("Invalid speech control character.");
                    if (!result.text.empty() && !text.empty()) result.text += '\n';
                    result.text += text;
                    if (result.text.size() > MaxSpeechBytes) throw std::runtime_error("Speech exceeded limit.");
                }
            } else if (type != "reasoning") throw std::runtime_error("Unexpected output item.");
        }
        if (refusal && result.action) throw std::runtime_error("Refusal mixed with action.");
        if (result.text.empty() && !result.action) throw std::runtime_error("Empty response.");
        if (result.action) {
            // Do not speak untrusted claims of success before local dispatch. The UI
            // will narrate actual success, failure, or a confirmation requirement.
            result.text = "I have a proposed action ready for local validation.";
        }
        return result;
    } catch (...) {
        return failure(generation, "The cloud response was invalid or outside the allowed actions. Nothing was executed.");
    }
}

class ConversationalProvider::Impl {
public:
    struct Pending { std::uint64_t generation; ProviderRequest request; std::shared_ptr<std::atomic_bool> cancelled; };
    explicit Impl(ProviderConfig config, ProviderTransport transport)
        : config_(std::move(config)), transport_(transport ? std::move(transport) : ProviderTransport(nativeTransport)),
          worker_([this] { run(); }) {}
    ~Impl() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            invalidate();
        }
        changed_.notify_one();
        if (worker_.joinable()) worker_.join();
    }
    void invalidate() {
        ++generation_;
        if (activeCancel_) activeCancel_->store(true);
        pending_.reset();
        result_.reset();
        busy_ = false;
    }
    void run() {
        for (;;) {
            Pending work;
            std::string model;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                changed_.wait(lock, [&] { return stopping_ || pending_.has_value(); });
                if (stopping_) return;
                work = std::move(*pending_);
                pending_.reset();
                model = config_.model;
            }
            ProviderResult result;
            try {
                const auto body = encodeRequest(work.request, model);
                if (work.cancelled->load()) continue;
                const auto http = transport_(body, work.cancelled);
                if (!http.error.empty()) result = failure(work.generation, http.error);
                else if (http.status == 401 || http.status == 403) result = failure(work.generation, "Cloud authorization failed. Check your own API key and model access.");
                else if (http.status == 429) result = failure(work.generation, "Cloud rate or quota limit reached. No action was accepted.");
                else if (http.status != 200) result = failure(work.generation, "The cloud provider could not complete this request. No action was accepted.");
                else {
                    result = parseProviderResponse(http.body, work.generation);
                    if (result.action && !isValidAction(*result.action, work.request.context.sequenceLength))
                        result = failure(work.generation, "The proposed action is outside the current project bounds.");
                }
            } catch (const std::invalid_argument& error) {
                result = failure(work.generation, error.what()); // Only our fixed validation messages.
            } catch (...) {
                result = failure(work.generation, "The cloud request failed safely. No action was accepted.");
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_ || !config_.enabled || work.cancelled->load() || work.generation != generation_) continue;
                result_ = std::move(result);
                busy_ = false;
                activeCancel_.reset();
            }
        }
    }
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    ProviderConfig config_;
    ProviderTransport transport_;
    bool stopping_{};
    bool busy_{};
    std::uint64_t generation_{};
    std::optional<Pending> pending_;
    std::optional<ProviderResult> result_;
    std::shared_ptr<std::atomic_bool> activeCancel_;
    std::thread worker_;
};

ConversationalProvider::ConversationalProvider(ProviderConfig config, ProviderTransport transport)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(transport))) {}
ConversationalProvider::~ConversationalProvider() = default;
void ConversationalProvider::setEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    if (impl_->config_.enabled != enabled) {
        impl_->invalidate();
        impl_->config_.enabled = enabled;
    }
}
bool ConversationalProvider::enabled() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->config_.enabled;
}
bool ConversationalProvider::busy() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->busy_;
}
std::uint64_t ConversationalProvider::submit(ProviderRequest request) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    const auto generation = ++impl_->generation_;
    if (impl_->activeCancel_) impl_->activeCancel_->store(true);
    impl_->pending_.reset();
    impl_->result_.reset();
    impl_->busy_ = false;
    if (!impl_->config_.enabled) {
        impl_->result_ = failure(generation, "Cloud conversation is disabled. Enable it only after reviewing the data-sharing disclosure.");
        return generation;
    }
    try { validateRequest(request, impl_->config_.model); }
    catch (const std::exception& error) {
        impl_->result_ = failure(generation, error.what());
        return generation;
    }
    impl_->activeCancel_ = std::make_shared<std::atomic_bool>(false);
    impl_->pending_ = Impl::Pending{generation, std::move(request), impl_->activeCancel_};
    impl_->busy_ = true;
    impl_->changed_.notify_one();
    return generation;
}
std::optional<ProviderResult> ConversationalProvider::poll() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    if (!impl_->result_) return std::nullopt;
    auto result = std::move(impl_->result_);
    impl_->result_.reset();
    return result;
}
void ConversationalProvider::cancel() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->invalidate();
}

} // namespace evolve::voice
