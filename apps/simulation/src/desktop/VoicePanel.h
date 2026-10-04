#pragma once
#include "voice/VoiceControl.h"
#include "voice/ConversationalProvider.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <string>

namespace evolve::voice {
struct WorkspaceContext {
    std::uint64_t epoch{},revision{};
    std::size_t baseCount{};
    std::string summary;
    AggregateContext aggregate;
};
// Native, keyboard-accessible companion window. All workspace callbacks run on
// the application UI thread; provider threads only return proposals.
class VoicePanel {
public:
    using Context = std::function<WorkspaceContext()>;
    using Execute = std::function<std::string(const Action&)>;
    VoicePanel(Context context,Execute execute);
    ~VoicePanel();
    void show(HWND owner,float scale);
    void poll();
    void stop();
    bool dialogMessage(MSG& message);
    // Deterministic Windows UI smoke: typed controls only, no microphone/network.
    void smokeTest();
    void setWorkspaceBusy(bool busy);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
