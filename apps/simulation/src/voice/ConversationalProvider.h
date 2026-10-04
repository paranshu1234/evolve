#pragma once
#include "VoiceControl.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace evolve::voice {

// Deliberately cannot contain a sequence, filename, path, or project title.
struct AggregateContext {
    std::size_t sequenceLength{};
    std::size_t editCount{};
    double gcPercent{};
    bool canUndo{};
    bool canRedo{};
    bool hasAnalysis{};
    bool analysisRunning{};
};

struct ProviderConfig {
    bool enabled{false}; // Runtime disclosure + explicit opt-in required before true.
    std::string model{"gpt-4.1-mini"};
};

struct ConversationTurn {
    std::string user;
    std::string assistant; // Actual UI result or displayed answer, never presumed success.
};

struct ProviderRequest {
    std::string transcript; // UTF-8; at most 2048 bytes, checked before sending.
    AggregateContext context;
    std::vector<ConversationTurn> conversation; // At most 4 complete turns, 2048 bytes per field.
};

struct ProviderResult {
    std::uint64_t generation{};
    std::string text;
    std::optional<Action> action; // Proposal only; always pass through UI validation.
    std::string error; // Local fixed message; never server content or credentials.
    bool ok() const { return error.empty(); }
};

struct ProviderHttpResponse {
    int status{};
    std::string body;
    std::string error;
};

// Injectable for offline fixtures. Native transport reads OPENAI_API_KEY itself,
// only for an enabled request, and sends exclusively to api.openai.com:443.
using ProviderTransport = std::function<ProviderHttpResponse(
    const std::string& requestJson, const std::shared_ptr<std::atomic_bool>& cancelled)>;

// Pure helpers used by tests; neither reads credentials nor performs network I/O.
// UI skips unsuitable history instead of poisoning the next request. No truncation.
bool canRetainConversationTurn(const ConversationTurn&) noexcept;
std::string buildProviderRequest(const ProviderRequest&, const std::string& model);
ProviderResult parseProviderResponse(const std::string&, std::uint64_t generation = 0);

// One owned worker; UI polls rather than receiving cross-thread callbacks.
// Public methods must be called from one owner/UI thread. Destruction cancels and
// joins the worker. cancel()/setEnabled(false) are non-blocking; a synchronous
// native operation already in flight may finish/timeout, but can never dispatch.
class ConversationalProvider {
public:
    explicit ConversationalProvider(ProviderConfig config = {}, ProviderTransport transport = {});
    ~ConversationalProvider();
    ConversationalProvider(const ConversationalProvider&) = delete;
    ConversationalProvider& operator=(const ConversationalProvider&) = delete;

    void setEnabled(bool enabled);
    bool enabled() const;
    bool busy() const;
    std::uint64_t submit(ProviderRequest request);
    std::optional<ProviderResult> poll();
    void cancel();
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace evolve::voice
