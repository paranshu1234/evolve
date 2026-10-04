#include "VoicePanel.h"
#include "voice/WindowsVoice.h"
#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

namespace evolve::voice {
namespace {
enum Id { Enable=700,Cloud,Talk,Stop,Input,Send,History,Status,Approve,Reject,Speak,Disclosure };
std::wstring wide(const std::string& s) {
    if(s.empty()) return {};
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(n<=0) return L"Invalid text.";
    std::wstring out(static_cast<std::size_t>(n),0);
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n);return out;
}
std::string utf8(const std::wstring& s) {
    if(s.empty()) return {};
    const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    if(n<=0) return {};
    std::string out(static_cast<std::size_t>(n),0);
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;
}
}
struct VoicePanel::Impl {
    Context context;
    Execute execute;
    HWND window{};
    HFONT font{};
    float scale{1};
    Controller controller;
    WindowsVoice local;
    ConversationalProvider provider;
    std::optional<RequestTicket> microphoneTicket,providerTicket;
    std::uint64_t microphoneSession{},providerGeneration{};
    bool held{},speaking{},closing{},executing{};
    std::wstring history;
    std::vector<ConversationTurn> conversation;
    std::string cloudTurnUser;
    std::uint64_t conversationEpoch{};
    void syncConversation() {
        const auto epoch=context().epoch;
        if(epoch!=conversationEpoch) {conversation.clear();conversationEpoch=epoch;}
    }
    void remember(const std::string& user,const std::string& outcome) {
        syncConversation();
        // Keep complete UTF-8 turns rather than cutting a character or meaning.
        if(!provider.enabled() || !canRetainConversationTurn({user,outcome})) return;
        conversation.push_back({user,outcome});
        if(conversation.size()>4) conversation.erase(conversation.begin());
    }
    Impl(Context c,Execute e):context(std::move(c)),execute(std::move(e)) {}
    ~Impl() {stop();local.shutdown();if(window) DestroyWindow(window);if(font) DeleteObject(font);}
    int px(int n) const {return static_cast<int>(std::lround(n*scale));}
    HWND child(int id) const {return GetDlgItem(window,id);}
    void append(const std::string& who,const std::string& text) {
        if(!window) return;
        history+=wide(who+": "+text)+L"\r\n\r\n";
        if(history.size()>24000) history=L"Earlier transcript cleared to limit memory.\r\n\r\n"+history.substr(history.size()-16000);
        SetWindowTextW(child(History),history.c_str());SendMessageW(child(History),EM_SETSEL,history.size(),history.size());SendMessageW(child(History),EM_SCROLLCARET,0,0);
    }
    void reply(const std::string& text) {
        append("Evolve",text);
        if(speaking && !local.speak(text)) append("Speech",local.error().empty() ? "Spoken output is unavailable. Read the reply above." : local.error());
    }
    void update() {
        if(!window) return;
        const bool enabled=local.state()!=WindowsVoice::State::Disabled && local.state()!=WindowsVoice::State::Error;
        SetWindowTextW(child(Enable),enabled ? L"Disable microphone":L"Enable local microphone");
        EnableWindow(child(Talk),enabled && !closing);
        EnableWindow(child(Speak),enabled);
        const bool confirmation=controller.state()==State::AwaitingConfirmation;
        EnableWindow(child(Approve),confirmation);EnableWindow(child(Reject),confirmation);
        std::string status=local.statusText();
        status += provider.enabled() ? " | Cloud conversation ON" : " | Local command mode (no cloud)";
        if(provider.busy()) status+=" | Waiting for OpenAI";
        if(confirmation) status+=" | Review exact action; use Approve or Reject";
        SetWindowTextW(child(Status),wide(status).c_str());
        SetWindowTextW(child(Talk),held ? L"MIC ON - release to send":L"Hold to talk (Space)");
    }
    void stop() {
        held=false;local.stop();provider.cancel();controller.cancel();
        microphoneTicket.reset();providerTicket.reset();microphoneSession=providerGeneration=0;cloudTurnUser.clear();
        if(window && GetCapture()==child(Talk)) ReleaseCapture();
        update();
    }
    void beginTalk() {
        if(held) return;
        stop();
        if(!local.beginPushToTalk()) {reply(local.error());update();return;}
        if(controller.state()==State::Disabled) controller.beginSession();
        const auto c=context();microphoneTicket=controller.beginRequest(c.epoch,c.revision);
        microphoneSession=local.currentSessionId();held=true;update();
    }
    void endTalk() {if(!held) return;held=false;local.endPushToTalk();update();}
    void handle(const Decision& decision) {
        if(decision.status==DecisionStatus::NeedsConfirmation) {
            reply(decision.message+" Use the Approve button for this exact action; voice cannot approve it.");
        } else if(decision.status==DecisionStatus::Ready && decision.action) {
            const auto user=cloudTurnUser;
            try {const auto outcome=execute(*decision.action);remember(user,outcome);reply(outcome);cloudTurnUser.clear();}
            catch(const std::exception&) {reply("The action failed. No success is assumed. Check the workspace and try again.");}
        } else reply(decision.message);
        update();
    }
    void process(const std::string& text,std::optional<RequestTicket> existing={}) {
        if(text.empty()) return;
        if(!existing) stop();
        if(controller.state()==State::Disabled) controller.beginSession();
        const auto c=context();
        const auto ticket=existing ? *existing:controller.beginRequest(c.epoch,c.revision);
        if(ticket.projectEpoch!=c.epoch || ticket.revision!=c.revision) {
            controller.reject(ticket);reply("Workspace changed while listening. Repeat the request against the current project.");return;
        }
        append("You",text);
        if(provider.enabled()) {
            syncConversation();cloudTurnUser=text;
            providerTicket=ticket;providerGeneration=provider.submit({text,c.aggregate,conversation});
        } else {
            const auto parsed=parseLocalCommand(text);
            if(!parsed.action) {controller.reject(ticket);reply(parsed.message);}
            else handle(controller.accept(ticket,*parsed.action,c.epoch,c.revision,c.baseCount));
        }
        update();
    }
    void command(int id) {
        if(executing) return;
        switch(id) {
        case Enable:
            stop();
            if(local.state()!=WindowsVoice::State::Disabled && local.state()!=WindowsVoice::State::Error) {local.shutdown();reply("Microphone disabled. Typed commands remain available.");}
            else if(local.initialize()) reply("Local speech is ready. Nothing is recorded until you hold Talk or hold Space on that button. Release sends the recognized words; Stop cancels.");
            else reply(local.error());
            break;
        case Cloud:
            stop();conversation.clear();
            if(provider.enabled()) {provider.setEnabled(false);reply("Cloud conversation disabled. Exact local commands remain available.");}
            else {
                const int answer=MessageBoxW(window,
                    L"Enable OpenAI conversation for this window session?\n\nYour typed and locally recognized spoken words, plus up to four recent completed conversation turns, base count, changed-base count, GC percentage and analysis/undo status, will be sent to OpenAI. Raw sequences, filenames and paths are not automatically included. Do not dictate private sequence data unless you intend to share it.\n\nOpenAI API usage has separate charges from ChatGPT. Requires your own OPENAI_API_KEY in the process environment; this app never saves it.\n\nNo audio is sent by this implementation. Windows speech stays local. Review OpenAI's API terms and data controls before enabling. Continue?",
                    L"Cloud provider and usage consent",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION);
                if(answer==IDYES) {provider.setEnabled(true);reply("Cloud conversation enabled for this session. Every sent prompt uses your OpenAI API account. Model suggestions are validated locally; sensitive actions require Approve.");}
            }
            SendMessageW(child(Cloud),BM_SETCHECK,provider.enabled() ? BST_CHECKED:BST_UNCHECKED,0);break;
        case Stop: stop();reply("Stopped. Pending replies, actions and confirmations were discarded.");break;
        case Send: {
            const int length=GetWindowTextLengthW(child(Input));std::wstring text(static_cast<std::size_t>(length)+1,0);
            GetWindowTextW(child(Input),text.data(),length+1);text.resize(static_cast<std::size_t>(length));
            SetWindowTextW(child(Input),L"");process(utf8(text));break;
        }
        case Approve: {
            const auto c=context();handle(controller.confirm(controller.pendingConfirmation(),c.epoch,c.revision,c.baseCount));break;
        }
        case Reject: stop();reply("Action rejected. No proposed change was made.");break;
        case Speak: speaking=SendMessageW(child(Speak),BM_GETCHECK,0,0)==BST_CHECKED;
            if(!speaking) stop();break;
        default:break;
        }
        update();
    }
    void poll() {
        if(!window || !IsWindowVisible(window) || executing) return;
        syncConversation();const auto c=context();
        for(const auto& event:local.poll()) {
            if(event.sessionId==microphoneSession && microphoneTicket) {
                const auto ticket=*microphoneTicket;microphoneTicket.reset();process(event.text,ticket);
            }
        }
        if(held && local.state()!=WindowsVoice::State::Listening) {
            held=false;if(GetCapture()==child(Talk)) ReleaseCapture();
        }
        if(microphoneTicket && (local.state()==WindowsVoice::State::Ready || local.state()==WindowsVoice::State::Error)) {
            controller.reject(*microphoneTicket);microphoneTicket.reset();
            if(local.state()==WindowsVoice::State::Error) append("Speech",local.error());
            else append("Speech","No speech recognized. Nothing was sent or changed.");
        }
        if(auto result=provider.poll()) {
            if(providerTicket && result->generation==providerGeneration) {
                const auto ticket=*providerTicket;providerTicket.reset();
                if(ticket.projectEpoch!=c.epoch || ticket.revision!=c.revision) {
                    controller.reject(ticket);reply("Workspace changed. The delayed cloud reply was discarded; repeat your request.");
                } else if(!result->ok()) {controller.reject(ticket);reply(result->error);}
                else if(result->action) {
                    // Do not echo a model's claimed success. Only execution below
                    // can report success; proposals pass the local trust gate.
                    handle(controller.accept(ticket,*result->action,c.epoch,c.revision,c.baseCount));
                } else if(controller.reject(ticket)) {remember(cloudTurnUser,result->text);reply(result->text);cloudTurnUser.clear();}
            }
        }
        update();
    }
    void layout() {
        RECT r{};GetClientRect(window,&r);const int w=static_cast<int>(r.right/scale),h=static_cast<int>(r.bottom/scale);
        auto place=[&](int id,int x,int y,int width,int height){MoveWindow(child(id),px(x),px(y),px(width),px(height),TRUE);};
        place(Enable,16,16,205,30);place(Cloud,235,16,w-251,30);
        place(Disclosure,16,52,w-32,50);place(Talk,16,110,235,36);place(Stop,263,110,100,36);place(Speak,377,110,w-393,36);
        place(Status,16,154,w-32,48);place(History,16,207,w-32,std::max(80,h-334));
        place(Approve,16,h-116,180,30);place(Reject,208,h-116,100,30);
        place(Input,16,h-74,w-130,48);place(Send,w-102,h-74,86,48);
    }
    static LRESULT CALLBACK talkProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
        auto* self=reinterpret_cast<Impl*>(data);
        switch(msg) {
        case WM_LBUTTONDOWN:SetFocus(hwnd);self->beginTalk();if(self->held) SetCapture(hwnd);return 0;
        case WM_LBUTTONUP:self->endTalk();if(GetCapture()==hwnd) ReleaseCapture();return 0;
        case WM_KEYDOWN:if(wp==VK_SPACE) {if(!(lp&(1LL<<30))) self->beginTalk();return 0;}break;
        case WM_KEYUP:if(wp==VK_SPACE) {self->endTalk();return 0;}break;
        case WM_CAPTURECHANGED:case WM_KILLFOCUS:if(self->held) self->stop();break;
        case WM_GETDLGCODE:return DLGC_WANTCHARS;
        }
        return DefSubclassProc(hwnd,msg,wp,lp);
    }
    static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(msg==WM_NCCREATE) {self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self) return DefWindowProcW(hwnd,msg,wp,lp);
        try {
            switch(msg) {
            case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={self->px(650),self->px(520)};return 0;
            case WM_SIZE:if(GetDlgItem(hwnd,History)) self->layout();return 0;
            case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED) self->command(LOWORD(wp));return 0;
            case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE) self->stop();return 0;
            case WM_CLOSE:self->stop();self->local.shutdown();self->provider.setEnabled(false);self->conversation.clear();SendMessageW(self->child(Cloud),BM_SETCHECK,BST_UNCHECKED,0);ShowWindow(hwnd,SW_HIDE);return 0;
            case WM_NCDESTROY:self->window=nullptr;break;
            }
        } catch(const std::exception&) {self->stop();self->append("Evolve","Voice control failed safely. No success is assumed.");return 0;}
        return DefWindowProcW(hwnd,msg,wp,lp);
    }
    void show(HWND owner,float dpi) {
        if(!window) {
            scale=dpi;
            WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);wc.lpszClassName=L"EvolveVoicePanel";
            if(!RegisterClassExW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Cannot register voice panel.");
            font=CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
            window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"Evolve Voice Agent | experimental",WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,CW_USEDEFAULT,px(740),px(640),owner,nullptr,wc.hInstance,this);
            if(!window) throw std::runtime_error("Cannot create voice panel.");
            auto add=[&](const wchar_t* cls,const wchar_t* label,int id,DWORD style){HWND h=CreateWindowExW(cls==std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE:0,cls,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),wc.hInstance,nullptr);if(!h) throw std::runtime_error("Cannot create voice control.");SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return h;};
            add(L"BUTTON",L"Enable local microphone",Enable,WS_TABSTOP);
            add(L"BUTTON",L"Use OpenAI conversation (paid API)",Cloud,WS_TABSTOP|BS_AUTOCHECKBOX);
            add(L"STATIC",L"Default: no microphone or cloud. Local commands are limited English commands. Cloud mode sends text + aggregate context. Neither mode predicts biological effects.",Disclosure,0);
            HWND talk=add(L"BUTTON",L"Hold to talk (Space)",Talk,WS_TABSTOP);
            SetWindowSubclass(talk,talkProc,1,reinterpret_cast<DWORD_PTR>(this));
            add(L"BUTTON",L"Stop",Stop,WS_TABSTOP);
            add(L"BUTTON",L"Speak replies",Speak,WS_TABSTOP|BS_AUTOCHECKBOX);
            add(L"STATIC",L"",Status,0);
            add(L"EDIT",L"",History,ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP);
            add(L"BUTTON",L"Approve exact action",Approve,WS_TABSTOP);
            add(L"BUTTON",L"Reject",Reject,WS_TABSTOP);
            add(L"EDIT",L"",Input,ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP);
            SendMessageW(child(Input),EM_SETLIMITTEXT,2048,0);
            add(L"BUTTON",L"Send",Send,WS_TABSTOP);
            controller.beginSession();layout();append("Evolve",localCommandHelp());
        }
        ShowWindow(window,SW_SHOW);SetForegroundWindow(window);update();
    }
};
VoicePanel::VoicePanel(Context c,Execute e):impl_(std::make_unique<Impl>(std::move(c),std::move(e))) {}
VoicePanel::~VoicePanel()=default;
void VoicePanel::show(HWND owner,float scale) {impl_->show(owner,scale);}
void VoicePanel::poll() {impl_->poll();}
void VoicePanel::stop() {impl_->stop();}
void VoicePanel::setWorkspaceBusy(bool busy) {
    impl_->executing=busy;
    if(busy) impl_->stop();
    if(impl_->window) EnableWindow(impl_->window,!busy);
}
void VoicePanel::smokeTest() {
    if(!impl_->window || impl_->local.state()!=WindowsVoice::State::Disabled || impl_->provider.enabled())
        throw std::runtime_error("Voice smoke: microphone/cloud must remain disabled by default.");
    SetWindowTextW(impl_->child(Input),L"select base 2");
    SendMessageW(impl_->window,WM_COMMAND,MAKEWPARAM(Send,BN_CLICKED),reinterpret_cast<LPARAM>(impl_->child(Send)));
    if(impl_->history.find(L"Selected base 2.")==std::wstring::npos)
        throw std::runtime_error("Voice smoke: typed command did not execute through UI dispatch.");
    SetWindowTextW(impl_->child(Input),L"restore baseline");
    SendMessageW(impl_->window,WM_COMMAND,MAKEWPARAM(Send,BN_CLICKED),reinterpret_cast<LPARAM>(impl_->child(Send)));
    if(impl_->controller.state()!=State::AwaitingConfirmation)
        throw std::runtime_error("Voice smoke: reset did not require local confirmation.");
    SendMessageW(impl_->window,WM_COMMAND,MAKEWPARAM(Reject,BN_CLICKED),reinterpret_cast<LPARAM>(impl_->child(Reject)));
    if(impl_->controller.pendingConfirmation()!=0)
        throw std::runtime_error("Voice smoke: rejected confirmation remained active.");
    SendMessageW(impl_->window,WM_CLOSE,0,0);
}
bool VoicePanel::dialogMessage(MSG& message) {
    if(!impl_->window || !IsWindowVisible(impl_->window)) return false;
    if(message.hwnd!=impl_->window && !IsChild(impl_->window,message.hwnd)) return false;
    if(message.message==WM_KEYDOWN && message.wParam==VK_ESCAPE) {impl_->stop();return true;}
    if(!IsDialogMessageW(impl_->window,&message)) {TranslateMessage(&message);DispatchMessageW(&message);}
    return true;
}
}
