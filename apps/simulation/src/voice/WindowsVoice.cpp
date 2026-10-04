#include "voice/WindowsVoice.h"

#include <windows.h>
#include <sapi.h>

#include <cassert>
#include <chrono>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace evolve::voice {
namespace {

template<class T> class ComPtr {
public:
    ~ComPtr() { reset(); }
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T* get() const { return value_; }
    T* operator->() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }
    T** put() { reset(); return &value_; }
    void reset() { if (value_) { value_->Release(); value_ = nullptr; } }
private:
    T* value_{};
};

class Apartment {
public:
    ~Apartment() { reset(); }
    HRESULT initialize() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        owns_ = SUCCEEDED(hr); // S_FALSE must also be balanced by CoUninitialize.
        return hr;
    }
    void reset() { if (owns_) { CoUninitialize(); owns_ = false; } }
private:
    bool owns_{};
};

struct TaskMemDeleter {
    void operator()(void* pointer) const { CoTaskMemFree(pointer); }
};
template<class T> using TaskMem = std::unique_ptr<T, TaskMemDeleter>;

// Every event, including ignored/stale events, owns its payload until released.
struct Event {
    SPEVENT value{};
    ~Event() {
        switch (value.elParamType) {
        case SPET_LPARAM_IS_TOKEN:
        case SPET_LPARAM_IS_OBJECT:
            if (value.lParam) reinterpret_cast<IUnknown*>(value.lParam)->Release();
            break;
        case SPET_LPARAM_IS_POINTER:
        case SPET_LPARAM_IS_STRING:
            CoTaskMemFree(reinterpret_cast<void*>(value.lParam));
            break;
        default: break;
        }
    }
};

std::string utf8(const wchar_t* text) {
    if (!text || !*text) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                                       nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                            result.data(), size, nullptr, nullptr)) return {};
    result.pop_back();
    return result;
}

std::wstring wide(const std::string& text) {
    if (text.empty() || text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                       static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), result.data(), size)) return {};
    return result;
}

std::string failureText(const char* action, HRESULT hr) {
    std::ostringstream text;
    text << action << " (0x" << std::hex << std::uppercase << std::setw(8)
         << std::setfill('0') << static_cast<unsigned long>(hr) << ").";
    return text.str();
}

// Read installed desktop tokens only. Never use a shared recognizer or select
// an arbitrary third-party/cloud engine from the user's default token.
HRESULT microsoftEnglishToken(const wchar_t* categoryId, ComPtr<ISpObjectToken>& token) {
    ComPtr<ISpObjectTokenCategory> category;
    HRESULT hr = CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_ISpObjectTokenCategory,
                                  reinterpret_cast<void**>(category.put()));
    if (FAILED(hr)) return hr;
    hr = category->SetId(categoryId, FALSE);
    if (FAILED(hr)) return hr;
    ComPtr<IEnumSpObjectTokens> tokens;
    hr = category->EnumTokens(L"Vendor=Microsoft;Language=409", L"VendorPreferred", tokens.put());
    if (FAILED(hr)) return hr;
    ULONG fetched{};
    hr = tokens->Next(1, token.put(), &fetched);
    return SUCCEEDED(hr) && fetched == 1 ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

using Clock = std::chrono::steady_clock;
constexpr std::size_t MaxTranscriptBytes = 4096;
constexpr auto FinishTimeout = std::chrono::seconds(2);
constexpr auto HoldTimeout = std::chrono::seconds(30);

} // namespace

struct WindowsVoice::Impl {
    // Apartment is destroyed last, after all COM interfaces.
    Apartment apartment;
    ComPtr<ISpRecognizer> recognizer;
    ComPtr<ISpAudio> input;
    ComPtr<ISpRecoContext> context;
    ComPtr<ISpRecoGrammar> grammar;
    ComPtr<ISpVoice> speaker;
    DWORD ownerThread{};
    State state{State::Disabled};
    std::uint64_t session{};
    ULONG stream{};
    bool streamStarted{};
    Clock::time_point started{}, finishDeadline{};
    std::string error, notice, transcript;

    void checkThread() const { assert(ownerThread == 0 || ownerThread == GetCurrentThreadId()); }

    void drain() {
        if (!context) return;
        for (;;) {
            Event event;
            ULONG count{};
            if (context->GetEvents(1, &event.value, &count) != S_OK || !count) break;
        }
    }

    void releaseSession() {
        // The context itself is per session as well as its grammar ID. A late
        // event cannot be inherited by a later push-to-talk context.
        if (grammar) grammar->SetDictationState(SPRS_INACTIVE);
        grammar.reset();
        drain();
        context.reset();
        streamStarted = false;
    }

    HRESULT cancel() {
        ++session;
        transcript.clear();
        HRESULT result = recognizer ? recognizer->SetRecoState(SPRST_INACTIVE_WITH_PURGE) : S_OK;
        if (speaker) {
            const HRESULT hr = speaker->Speak(nullptr, SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
            if (FAILED(hr)) {
                speaker.reset();
                if (SUCCEEDED(result)) result = hr;
            }
        }
        releaseSession();
        return result;
    }

    bool fail(const char* action, HRESULT hr) {
        cancel();
        // Releasing the input/recognizer is the fail-closed fallback if an
        // engine or device could not honor an inactive-state request.
        recognizer.reset();
        input.reset();
        speaker.reset();
        state = State::Error;
        error = failureText(action, hr);
        notice.clear();
        return false;
    }

    bool createSession() {
        HRESULT hr = recognizer->CreateRecoContext(context.put());
        if (FAILED(hr)) return fail("Cannot create the local speech context", hr);
        // Do not retain or expose recorded audio. Only final text is consumed.
        hr = context->SetAudioOptions(SPAO_NONE, nullptr, nullptr);
        if (FAILED(hr)) return fail("Cannot disable retained speech audio", hr);
        hr = context->SetNotifyWin32Event();
        if (FAILED(hr)) return fail("Cannot initialize local speech notifications", hr);
        const ULONGLONG interests = SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_START_SR_STREAM)
                                  | SPFEI(SPEI_END_SR_STREAM);
        hr = context->SetInterest(interests, interests);
        if (FAILED(hr)) return fail("Cannot subscribe to local speech results", hr);
        hr = context->CreateGrammar(session, grammar.put());
        if (FAILED(hr)) return fail("Cannot create the dictation grammar", hr);
        hr = grammar->LoadDictation(nullptr, SPLO_STATIC);
        if (FAILED(hr)) return fail("The installed recognizer does not support dictation", hr);
        return true;
    }
};

WindowsVoice::WindowsVoice() : impl_(std::make_unique<Impl>()) {}
WindowsVoice::~WindowsVoice() { shutdown(); }

bool WindowsVoice::initialize() {
    auto& i = *impl_;
    i.checkThread();
    if (i.state == State::Ready || i.state == State::Listening || i.state == State::Finishing) return true;
    shutdown();
    i.ownerThread = GetCurrentThreadId();
    HRESULT hr = i.apartment.initialize();
    if (FAILED(hr)) return i.fail("Cannot initialize the speech COM apartment", hr);

    ComPtr<ISpObjectToken> token;
    hr = microsoftEnglishToken(SPCAT_RECOGNIZERS, token);
    if (FAILED(hr)) return i.fail("No Microsoft US-English desktop speech recognizer is available. Install its Windows speech language component; typed commands still work", hr);
    hr = CoCreateInstance(CLSID_SpInprocRecognizer, nullptr, CLSCTX_INPROC_SERVER,
                          IID_ISpRecognizer, reinterpret_cast<void**>(i.recognizer.put()));
    if (FAILED(hr)) return i.fail("Cannot create the local speech recognizer", hr);
    // Select the installed Microsoft engine before any call which might load
    // the user's default engine. No input or active grammar exists yet.
    hr = i.recognizer->SetRecognizer(token.get());
    if (FAILED(hr)) return i.fail("Cannot load the installed Microsoft speech recognizer", hr);
    // Inactivate before attaching any audio input or activating any grammar.
    hr = i.recognizer->SetRecoState(SPRST_INACTIVE_WITH_PURGE);
    if (FAILED(hr)) return i.fail("Cannot keep the selected speech engine inactive", hr);
    hr = CoCreateInstance(CLSID_SpMMAudioIn, nullptr, CLSCTX_INPROC_SERVER,
                          IID_ISpAudio, reinterpret_cast<void**>(i.input.put()));
    if (FAILED(hr)) return i.fail("Cannot create the microphone input", hr);
    hr = i.recognizer->SetInput(i.input.get(), TRUE);
    if (FAILED(hr)) return i.fail("Cannot configure microphone input. Check Windows microphone access and the default input device", hr);
    ++i.session;
    if (!i.createSession()) return false; // Validate dictation without activating it.
    i.releaseSession();
    i.error.clear();
    i.notice.clear();
    i.state = State::Ready;
    return true;
}

void WindowsVoice::shutdown() {
    auto& i = *impl_;
    i.checkThread();
    i.cancel();
    i.speaker.reset();
    i.recognizer.reset();
    i.input.reset();
    i.apartment.reset();
    i.state = State::Disabled;
    i.error.clear();
    i.notice.clear();
    i.ownerThread = 0;
}

bool WindowsVoice::beginPushToTalk() {
    auto& i = *impl_;
    i.checkThread();
    if (i.state != State::Ready || !i.recognizer) return false;
    // Also interrupt any spoken reply before opening the microphone.
    const HRESULT cancelResult = i.cancel();
    if (FAILED(cancelResult)) return i.fail("Cannot cancel previous local audio safely", cancelResult);
    i.error.clear();
    i.notice.clear();
    if (!i.createSession()) return false;
    HRESULT hr = i.grammar->SetDictationState(SPRS_ACTIVE);
    if (FAILED(hr)) return i.fail("Cannot activate local dictation", hr);
    hr = i.recognizer->SetRecoState(SPRST_ACTIVE);
    if (FAILED(hr)) return i.fail("Cannot start the microphone. Check its connection, Windows desktop-app microphone access, and other audio apps", hr);
    i.state = State::Listening;
    i.started = Clock::now();
    return true;
}

void WindowsVoice::endPushToTalk() {
    auto& i = *impl_;
    i.checkThread();
    if (i.state != State::Listening) return;
    // INACTIVE closes input, but unlike WITH_PURGE it permits already buffered
    // speech to finish. The finalization deadline never opens the microphone.
    i.state = State::Finishing;
    i.finishDeadline = Clock::now() + FinishTimeout;
    const HRESULT hr = i.recognizer->SetRecoState(SPRST_INACTIVE);
    if (FAILED(hr)) i.fail("Cannot stop the microphone cleanly", hr);
}

void WindowsVoice::stop() {
    auto& i = *impl_;
    i.checkThread();
    const HRESULT hr = i.cancel();
    if (FAILED(hr)) { i.fail("Cannot stop the local speech engine cleanly", hr); return; }
    if (i.recognizer) {
        i.state = State::Ready;
        i.notice = "Stopped. Microphone off.";
    }
}

std::vector<TranscriptEvent> WindowsVoice::poll() {
    auto& i = *impl_;
    i.checkThread();
    std::vector<TranscriptEvent> result;
    if (!i.context || (i.state != State::Listening && i.state != State::Finishing)) return result;
    if (i.state == State::Listening && Clock::now() - i.started >= HoldTimeout) {
        endPushToTalk();
        if (i.state == State::Error) return result;
        i.notice = "The 30-second push-to-talk limit was reached.";
    }

    bool ended = false;
    for (;;) {
        Event event;
        ULONG count{};
        const HRESULT hr = i.context->GetEvents(1, &event.value, &count);
        if (FAILED(hr)) { i.fail("Cannot read local speech results", hr); return {}; }
        if (hr != S_OK || !count) break;
        if (event.value.eEventId == SPEI_START_SR_STREAM) {
            i.stream = event.value.ulStreamNum;
            i.streamStarted = true;
        } else if (event.value.eEventId == SPEI_RECOGNITION && event.value.lParam) {
            auto* recognition = reinterpret_cast<ISpRecoResult*>(event.value.lParam);
            SPPHRASE* rawPhrase{};
            const HRESULT phraseResult = recognition->GetPhrase(&rawPhrase);
            TaskMem<SPPHRASE> phrase(rawPhrase);
            if (FAILED(phraseResult) || !phrase || phrase->ullGrammarID != i.session) continue;
            wchar_t* rawText{};
            const HRESULT textResult = recognition->GetText(SP_GETWHOLEPHRASE, SP_GETWHOLEPHRASE,
                                                            TRUE, &rawText, nullptr);
            TaskMem<wchar_t> text(rawText);
            if (FAILED(textResult) || !text) continue;
            auto recognized = utf8(text.get());
            if (recognized.empty()) continue;
            if (i.transcript.size() + recognized.size() + 1 > MaxTranscriptBytes) {
                i.fail("Speech exceeded the 4096-byte limit. Please use a shorter utterance", E_INVALIDARG);
                return {};
            }
            if (!i.transcript.empty()) i.transcript += ' ';
            i.transcript += recognized;
        } else if (event.value.eEventId == SPEI_END_SR_STREAM && i.streamStarted
                   && event.value.ulStreamNum == i.stream) {
            const HRESULT streamResult = static_cast<HRESULT>(event.value.lParam);
            if (FAILED(streamResult)) {
                i.fail("The microphone stream failed. Check the input device and retry Enable local voice", streamResult);
                return {};
            }
            ended = true;
        }
    }

    if (ended && i.state == State::Listening) {
        i.fail("The microphone stream ended before Talk was released. Please retry", E_ABORT);
        return {};
    }
    if (ended || (i.state == State::Finishing && Clock::now() >= i.finishDeadline)) {
        const HRESULT hr = i.recognizer->SetRecoState(SPRST_INACTIVE_WITH_PURGE);
        if (FAILED(hr)) { i.fail("Cannot finish local speech safely", hr); return {}; }
        // A timeout is not evidence of a complete utterance: the engine could
        // still be processing a qualifying word such as "not". Never dispatch
        // just the already-recognized prefix when stream completion is missing.
        if (!ended) i.notice = "Speech finalization timed out. Nothing was sent; please try again.";
        else if (!i.transcript.empty()) result.push_back({i.session, std::move(i.transcript)});
        else i.notice = "No speech recognized. Hold Talk, speak clearly, then release.";
        i.transcript.clear();
        i.releaseSession();
        i.state = State::Ready;
    }
    return result;
}

bool WindowsVoice::speak(const std::string& text) {
    auto& i = *impl_;
    i.checkThread();
    if (i.state != State::Ready || text.empty()) return false;
    if (text.size() > MaxTranscriptBytes || text.find('\0') != std::string::npos) {
        i.error = "Spoken replies must contain at most 4096 bytes of plain UTF-8 text.";
        return false;
    }
    const auto spoken = wide(text);
    if (spoken.empty()) { i.error = "The spoken reply is not valid UTF-8."; return false; }
    if (!i.speaker) {
        ComPtr<ISpObjectToken> voiceToken;
        HRESULT hr = microsoftEnglishToken(SPCAT_VOICES, voiceToken);
        if (FAILED(hr)) {
            i.error = failureText("No Microsoft US-English desktop speaking voice is installed. Text replies still work", hr);
            return false;
        }
        hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_INPROC_SERVER,
                              IID_ISpVoice, reinterpret_cast<void**>(i.speaker.put()));
        if (SUCCEEDED(hr)) hr = i.speaker->SetVoice(voiceToken.get());
        if (FAILED(hr)) {
            i.speaker.reset();
            i.error = failureText("Cannot initialize local spoken replies", hr);
            return false;
        }
    }
    const HRESULT hr = i.speaker->Speak(spoken.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_NOT_XML, nullptr);
    if (FAILED(hr)) { i.error = failureText("Cannot play the spoken reply", hr); return false; }
    i.error.clear();
    return true;
}

bool WindowsVoice::isSpeaking() const {
    const auto& i = *impl_;
    i.checkThread();
    if (!i.speaker) return false;
    SPVOICESTATUS status{};
    return SUCCEEDED(i.speaker->GetStatus(&status, nullptr)) && status.dwRunningState == SPRS_IS_SPEAKING;
}

WindowsVoice::State WindowsVoice::state() const { return impl_->state; }
std::uint64_t WindowsVoice::currentSessionId() const { return impl_->session; }
const std::string& WindowsVoice::error() const { return impl_->error; }

std::string WindowsVoice::statusText() const {
    const auto& i = *impl_;
    switch (i.state) {
    case State::Disabled: return "Local voice disabled. Microphone off.";
    case State::Ready:
        if (isSpeaking()) return "Speaking locally. Microphone off.";
        return i.notice.empty() ? "Local voice ready. Microphone off." : i.notice;
    case State::Listening: return "Listening locally while Talk is held.";
    case State::Finishing: return "Microphone off. Finishing local transcription...";
    case State::Error: return i.error;
    }
    return "Local voice unavailable.";
}

} // namespace evolve::voice
