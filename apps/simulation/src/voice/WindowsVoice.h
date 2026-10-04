#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace evolve::voice {

struct TranscriptEvent {
    std::uint64_t sessionId{};
    std::string text; // UTF-8, final recognized phrases from one push-to-talk hold.
};

// Windows-only local SAPI input/output. Construction never enables a microphone.
// All methods, including destruction, must run on the same UI/COM thread.
// This adapter recognizes text; it never interprets commands or edits a project.
class WindowsVoice {
public:
    enum class State { Disabled, Ready, Listening, Finishing, Error };

    WindowsVoice();
    ~WindowsVoice();
    WindowsVoice(const WindowsVoice&) = delete;
    WindowsVoice& operator=(const WindowsVoice&) = delete;

    // Call only after the user explicitly enables local voice. Initialization
    // checks for an installed Microsoft US-English desktop SAPI recognizer.
    // It leaves the recognizer inactive and does not start recording.
    bool initialize();
    void shutdown();
    bool beginPushToTalk();
    // Closes microphone input and allows at most two seconds to finish already
    // buffered speech. poll() returns one combined final transcript when ready.
    // If stream completion times out, the whole utterance is discarded.
    void endPushToTalk();
    // Immediate cancellation: purge input/output and invalidate the session.
    // Also call on focus loss, mode/context changes, project replacement, close.
    void stop();
    std::vector<TranscriptEvent> poll();

    // Optional local TTS. Does not start the microphone or initialize voice.
    // Refuses while listening/finishing. Text is treated literally, never XML.
    bool speak(const std::string& text);
    bool isSpeaking() const;

    State state() const;
    std::uint64_t currentSessionId() const;
    const std::string& error() const;
    std::string statusText() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace evolve::voice
