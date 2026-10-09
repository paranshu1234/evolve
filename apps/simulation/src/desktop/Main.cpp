#include "core/Project.h"
#include "desktop/CircuitWindow.h"
#include "renderer/Dx12Renderer.h"
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
using namespace evolve;
constexpr COLORREF Background=RGB(14,21,30),Panel=RGB(23,33,45),Text=RGB(224,234,242),Muted=RGB(140,162,182),Accent=RGB(67,217,182);
enum Control {Demo=100,Import,Open,Save,Export,BaseList,BaseA,BaseC,BaseG,BaseT,Undo,Redo,Restore,Compare,Grid,Rotate,Frame,Run,Cancel,Sound,Light,Selection,Result,Progress,Status,Source,Circuit};

std::wstring wide(const std::string& s) {
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);
    std::wstring result(static_cast<std::size_t>(n),0);
    MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),result.data(),n);return result;
}
std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file) throw std::runtime_error("Cannot open the selected file.");
    auto size=file.tellg();if(size<0 || size>65536) throw std::runtime_error("v0.1 imports files up to 64 KiB.");
    file.seekg(0);std::string text(static_cast<std::size_t>(size),'\0');
    if(!file.read(text.data(),static_cast<std::streamsize>(size))) throw std::runtime_error("Cannot read the selected file.");return text;
}
void atomicWrite(const std::filesystem::path& path,const std::string& text) {
    // A sibling temporary file permits an atomic same-volume replacement on Windows.
    auto temp=path;temp+=L".tmp-"+std::to_wstring(GetCurrentProcessId());
    {
        std::ofstream file(temp,std::ios::binary|std::ios::trunc);
        if(!file) throw std::runtime_error("Cannot create the temporary save file.");
        file.write(text.data(),static_cast<std::streamsize>(text.size()));file.flush();
        if(!file) {file.close();DeleteFileW(temp.c_str());throw std::runtime_error("Cannot write the save file.");}
    }
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());throw std::runtime_error("Cannot replace the destination file. Your previous file was preserved.");
    }
}

class Application {
public:
    HWND window{},viewport{};
    bool smoke{},warp{},failed{};
    int smokeFrame{};
    ~Application() {renderer.reset();if(font) DeleteObject(font);if(headingFont) DeleteObject(headingFont);DeleteObject(backgroundBrush);DeleteObject(panelBrush);}
    void create(HINSTANCE instance);
    int loop();
    LRESULT message(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp);
    LRESULT viewportMessage(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp);
private:
    Project project;
    std::unique_ptr<Dx12Renderer> renderer;
    OrbitCamera camera;
    std::size_t selected{};
    bool compare{},grid{true},rotate{},sound{},dirty{},running{},dragged{};
    int dragButton{};POINT last{},press{};
    HFONT font{},headingFont{};
    HBRUSH backgroundBrush{CreateSolidBrush(Background)},panelBrush{CreateSolidBrush(Panel)};
    float scale{1};
    std::filesystem::path currentPath;
    Analysis pending;
    std::chrono::steady_clock::time_point started,lastTick;
    int px(int value) const {return static_cast<int>(std::lround(static_cast<float>(value)*scale));}
    HWND control(int id) const {return GetDlgItem(window,id);}
    HWND add(const wchar_t* cls,const wchar_t* text,int id,DWORD style=0);
    void button(const wchar_t* text,int id) {add(L"BUTTON",text,id,BS_OWNERDRAW|WS_TABSTOP);}
    void place(int id,int x,int y,int w,int h) {MoveWindow(control(id),px(x),px(y),px(w),px(h),TRUE);}
    void layout();
    void draw();
    void drawButton(const DRAWITEMSTRUCT& item);
    void refresh(bool rebuild=true);
    void setStatus(const std::wstring& message) {SetWindowTextW(control(Status),message.c_str());}
    void mutate(bool changed);
    void command(int id);
    void tick();
    void select(std::size_t index);
    void frame() {camera.frame(project.sequence().size(),compare,renderer->aspect());}
    bool confirmReplace();
    std::filesystem::path fileDialog(bool save,bool projectFile,bool csv=false);
    bool saveProject();
    void cancel();
    void error(const std::exception& e) {
        if(smoke) {std::ofstream("smoke-test-error.log")<<e.what();failed=true;PostQuitMessage(1);}
        else MessageBoxW(window,wide(e.what()).c_str(),L"Evolve.ai",MB_OK|MB_ICONERROR);
    }
};

LRESULT CALLBACK MainProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* app=reinterpret_cast<Application*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE) {app=static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    return app ? app->message(hwnd,msg,wp,lp):DefWindowProcW(hwnd,msg,wp,lp);
}
LRESULT CALLBACK ViewProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* app=reinterpret_cast<Application*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE) {app=static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    return app ? app->viewportMessage(hwnd,msg,wp,lp):DefWindowProcW(hwnd,msg,wp,lp);
}
HWND Application::add(const wchar_t* cls,const wchar_t* text,int id,DWORD style) {
    HWND child=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    if(!child) throw std::runtime_error("Cannot create desktop control.");
    SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return child;
}
void Application::create(HINSTANCE instance) {
    SetProcessDPIAware();HDC dc=GetDC(nullptr);scale=static_cast<float>(GetDeviceCaps(dc,LOGPIXELSX))/96.0f;ReleaseDC(nullptr,dc);
    font=CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    headingFont=CreateFontW(-px(25),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_BAR_CLASSES|ICC_PROGRESS_CLASS};InitCommonControlsEx(&common);
    WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=MainProc;wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wc.lpszClassName=L"EvolveDesktop";wc.hbrBackground=backgroundBrush;
    if(!RegisterClassExW(&wc)) throw std::runtime_error("Cannot register application window.");
    wc.lpfnWndProc=ViewProc;wc.lpszClassName=L"EvolveViewport";wc.hbrBackground=nullptr;wc.style=CS_OWNDC;
    if(!RegisterClassExW(&wc)) throw std::runtime_error("Cannot register viewport window.");
    window=CreateWindowExW(0,L"EvolveDesktop",L"Evolve.ai v0.1 | Genome workspace",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,px(1380),px(860),nullptr,nullptr,instance,this);
    if(!window) throw std::runtime_error("Cannot create application window.");
    viewport=CreateWindowExW(0,L"EvolveViewport",L"DNA viewport",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,1,1,window,nullptr,instance,this);
    if(!viewport) throw std::runtime_error("Cannot create 3D viewport.");
    button(L"Demo project",Demo);button(L"Import DNA",Import);button(L"Open project",Open);button(L"Save project",Save);button(L"Export results",Export);
    add(L"STATIC",L"Synthetic example / 24 bases",Source);
    add(L"LISTBOX",L"",BaseList,LBS_NOTIFY|WS_VSCROLL|WS_TABSTOP|LBS_NOINTEGRALHEIGHT);
    add(L"STATIC",L"",Selection);
    button(L"A",BaseA);button(L"C",BaseC);button(L"G",BaseG);button(L"T",BaseT);
    button(L"Undo",Undo);button(L"Redo",Redo);button(L"Restore baseline",Restore);
    button(L"Compare: off",Compare);button(L"Grid: on",Grid);button(L"Rotate: off",Rotate);button(L"Frame all [F]",Frame);
    button(L"Circuit lab",Circuit);
    button(L"Run mock analysis",Run);button(L"Cancel",Cancel);button(L"Sound: off",Sound);
    add(L"STATIC",L"Run analysis to compare sequence composition.",Result);
    add(PROGRESS_CLASSW,L"",Progress,PBS_SMOOTH);SendMessageW(control(Progress),PBM_SETRANGE32,0,100);
    add(TRACKBAR_CLASSW,L"",Light,TBS_HORZ|TBS_NOTICKS|WS_TABSTOP);SendMessageW(control(Light),TBM_SETRANGE,TRUE,MAKELPARAM(20,160));SendMessageW(control(Light),TBM_SETPOS,TRUE,100);
    add(L"STATIC",L"",Status);
    layout();renderer=std::make_unique<Dx12Renderer>();renderer->initialize(viewport,warp);frame();refresh();
    setStatus(L"Ready  /  Synthetic data  /  "+renderer->adapterName());
    lastTick=std::chrono::steady_clock::now();SetTimer(window,1,16,nullptr);ShowWindow(window,SW_SHOW);UpdateWindow(window);
}
void Application::layout() {
    if(!viewport) return;
    RECT r{};GetClientRect(window,&r);int w=static_cast<int>(static_cast<float>(r.right)/scale),h=static_cast<int>(static_cast<float>(r.bottom)/scale);
    const int right=w-282;
    place(Demo,22,66,128,32);place(Import,158,66,112,32);place(Open,278,66,120,32);place(Save,406,66,116,32);place(Export,530,66,126,32);
    place(Source,22,143,178,50);place(BaseList,22,198,178,std::max(170,h-295));
    place(Selection,right,146,260,75);
    for(int i=0;i<4;++i) place(BaseA+i,right+i*65,231,57,34);
    place(Undo,right,275,126,30);place(Redo,right+134,275,126,30);place(Restore,right,313,260,30);
    place(Run,right,383,174,34);place(Cancel,right+182,383,78,34);place(Progress,right,429,260,5);place(Result,right,449,260,126);
    place(Light,right,603,180,28);place(Sound,right+188,603,72,28);
    int vw=std::max(100,right-240),vh=std::max(100,h-270);
    MoveWindow(viewport,px(222),px(154),px(vw),px(vh),TRUE);
    place(Frame,222,111,118,30);place(Compare,348,111,120,30);place(Grid,476,111,91,30);place(Rotate,575,111,105,30);
    place(Circuit,664,66,130,32);
    place(Status,22,h-33,w-44,23);
    if(renderer) renderer->resize(px(vw),px(vh));InvalidateRect(window,nullptr,TRUE);
}
void Application::draw() {
    PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);SetBkMode(dc,TRANSPARENT);
    RECT client{};GetClientRect(window,&client);FillRect(dc,&client,backgroundBrush);
    int w=static_cast<int>(static_cast<float>(client.right)/scale),h=static_cast<int>(static_cast<float>(client.bottom)/scale),right=w-282;
    auto text=[&](const wchar_t* value,int x,int y,int width,COLORREF color,HFONT type) {
        SelectObject(dc,type);SetTextColor(dc,color);RECT r{px(x),px(y),px(x+width),px(y+32)};DrawTextW(dc,value,-1,&r,DT_LEFT|DT_SINGLELINE|DT_VCENTER);
    };
    text(L"evolve.ai",22,17,220,Text,headingFont);
    text(L"GENOME WORKSPACE",205,20,260,Muted,font);
    text(L"v0.1   /   LOCAL PROTOTYPE",w-278,20,255,Accent,font);
    text(L"SEQUENCE",22,109,170,Muted,font);text(L"VIRTUAL EDIT",right,109,260,Muted,font);
    text(L"COMPOSITION / DEMO",right,346,260,Muted,font);text(L"STUDIO LIGHTING",right,570,260,Muted,font);
    text(L"A  Adenine",222,h-72,104,RGB(46,217,176),font);text(L"T  Thymine",332,h-72,104,RGB(245,158,74),font);
    text(L"G  Guanine",442,h-72,104,RGB(122,145,255),font);text(L"C  Cytosine",552,h-72,104,RGB(245,99,145),font);
    text(compare ? L"Left: baseline   |   Right: edited scenario":L"Drag: orbit   |   Right-drag: pan   |   Wheel: zoom   |   Click: select",222,h-103,std::max(200,right-235),Muted,font);
    text(L"Schematic geometry. No biological predictions.",right,646,265,Muted,font);
    EndPaint(window,&paint);
}
void Application::drawButton(const DRAWITEMSTRUCT& item) {
    wchar_t label[128]{};GetWindowTextW(item.hwndItem,label,128);
    bool selectedBase=item.CtlID>=BaseA && item.CtlID<=BaseT && project.sequence()[selected]=="ACGT"[item.CtlID-BaseA];
    bool primary=item.CtlID==Run || selectedBase;
    bool disabled=(item.itemState&ODS_DISABLED)!=0;
    COLORREF bg=primary ? RGB(27,71,65):Panel;
    if(item.itemState&ODS_SELECTED) bg=RGB(40,68,82);
    HBRUSH brush=CreateSolidBrush(bg);HPEN pen=CreatePen(PS_SOLID,1,primary ? Accent:RGB(43,61,77));
    auto oldBrush=SelectObject(item.hDC,brush),oldPen=SelectObject(item.hDC,pen);
    RoundRect(item.hDC,item.rcItem.left,item.rcItem.top,item.rcItem.right,item.rcItem.bottom,px(8),px(8));
    SelectObject(item.hDC,oldBrush);SelectObject(item.hDC,oldPen);DeleteObject(brush);DeleteObject(pen);
    SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,disabled ? RGB(86,106,120):(primary ? Accent:Text));SelectObject(item.hDC,font);
    RECT rect=item.rcItem;DrawTextW(item.hDC,label,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    if(item.itemState&ODS_FOCUS) {InflateRect(&rect,-4,-4);DrawFocusRect(item.hDC,&rect);}
}
void Application::refresh(bool rebuild) {
    selected=std::min(selected,project.sequence().size()-1);
    SendMessageW(control(BaseList),WM_SETREDRAW,FALSE,0);SendMessageW(control(BaseList),LB_RESETCONTENT,0,0);
    for(std::size_t i=0;i<project.sequence().size();++i) {
        std::wostringstream row;row<<std::setw(3)<<std::setfill(L'0')<<i+1<<L"    "<<static_cast<wchar_t>(project.sequence()[i])<<L" : "<<static_cast<wchar_t>(complement(project.sequence()[i]));
        if(project.sequence()[i]!=project.baseline()[i]) row<<L"   *";
        SendMessageW(control(BaseList),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(row.str().c_str()));
    }
    SendMessageW(control(BaseList),LB_SETCURSEL,selected,0);SendMessageW(control(BaseList),WM_SETREDRAW,TRUE,0);InvalidateRect(control(BaseList),nullptr,TRUE);
    std::wostringstream info;info<<L"Base pair "<<selected+1<<L" of "<<project.sequence().size()<<L"\nBaseline: "<<static_cast<wchar_t>(project.baseline()[selected])
        <<L"   /   Scenario: "<<static_cast<wchar_t>(project.sequence()[selected])<<L"\nChoose a replacement below.";
    SetWindowTextW(control(Selection),info.str().c_str());
    for(int i=0;i<4;++i) InvalidateRect(control(BaseA+i),nullptr,TRUE);
    EnableWindow(control(Undo),project.canUndo());EnableWindow(control(Redo),project.canRedo());EnableWindow(control(Restore),project.sequence()!=project.baseline());
    EnableWindow(control(Run),!running);EnableWindow(control(Cancel),running);EnableWindow(control(Export),project.hasResult());
    if(project.hasResult()) {
        auto result=project.result();std::wostringstream text;text<<std::fixed<<std::setprecision(1)<<L"Baseline GC      "<<result.baselineGc<<L"%\nScenario GC     "<<result.scenarioGc
            <<L"%\nChanged bases   "<<result.edits<<L"\n\nComposition only / no effect prediction\nmock-composition-v0.1";
        SetWindowTextW(control(Result),text.str().c_str());
    } else SetWindowTextW(control(Result),running ? L"Analyzing the selected scenario...\n\nDemo job. Scientific inference is not connected.":L"No current result.\nRun mock analysis to compare GC content and edited base counts.");
    std::wstring title=L"Evolve.ai v0.1 | "+(currentPath.empty() ? std::wstring(L"Untitled project"):currentPath.filename().wstring())+(dirty ? L" *":L"");SetWindowTextW(window,title.c_str());
    if(rebuild && renderer) renderer->setMesh(buildHelix(project.sequence(),project.baseline(),selected,compare,grid));
}
void Application::select(std::size_t index) {selected=index;refresh();}
void Application::cancel() {running=false;SendMessageW(control(Progress),PBM_SETPOS,0,0);}
void Application::mutate(bool changed) {
    if(!changed) return;dirty=true;cancel();refresh();setStatus(L"Scenario changed. Run mock analysis to refresh results.");
}
bool Application::confirmReplace() {
    if(!dirty) return true;
    int answer=MessageBoxW(window,L"Save your current project before continuing?",L"Unsaved changes",MB_YESNOCANCEL|MB_ICONQUESTION);
    if(answer==IDCANCEL) return false;if(answer==IDYES) return saveProject();return true;
}
std::filesystem::path Application::fileDialog(bool save,bool projectFile,bool csv) {
    wchar_t path[32768]{};OPENFILENAMEW dialog{sizeof(dialog)};dialog.hwndOwner=window;dialog.lpstrFile=path;dialog.nMaxFile=32768;
    dialog.lpstrFilter=csv ? L"CSV results (*.csv)\0*.csv\0\0":(projectFile ? L"Evolve projects (*.evolve)\0*.evolve\0\0":L"DNA sequence (*.fasta;*.fa;*.txt)\0*.fasta;*.fa;*.txt\0All files\0*.*\0\0");
    dialog.lpstrDefExt=csv ? L"csv":(projectFile ? L"evolve":L"fasta");
    dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save ? OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    BOOL ok=save ? GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog);
    if(!ok && CommDlgExtendedError()!=0) throw std::runtime_error("The file dialog could not be opened.");
    return ok ? std::filesystem::path(path):std::filesystem::path{};
}
bool Application::saveProject() {
    auto path=currentPath.empty() ? fileDialog(true,true):currentPath;if(path.empty()) return false;
    atomicWrite(path,project.serialize());currentPath=path;dirty=false;refresh(false);setStatus(L"Project saved locally. Baseline and virtual edits preserved.");return true;
}
void Application::command(int id) {
    if(id>=BaseA && id<=BaseT) {mutate(project.edit(selected,"ACGT"[id-BaseA]));return;}
    switch(id) {
    case Demo:
        if(!confirmReplace()) break;
        cancel();project.importSequence(DemoSequence);currentPath.clear();dirty=false;selected=0;
        SetWindowTextW(control(Source),L"Synthetic example / 24 bases");frame();refresh();setStatus(L"Synthetic example loaded.");break;
    case Import: case Open: {
        if(!confirmReplace()) break;
        auto path=fileDialog(false,id==Open);if(path.empty()) break;
        auto text=readFile(path);
        if(id==Import) project.importSequence(text);else project.deserialize(text);
        cancel();currentPath=id==Open ? path:std::filesystem::path{};dirty=id==Import;selected=0;
        SetWindowTextW(control(Source),(L"Local input / "+std::to_wstring(project.sequence().size())+L" bases").c_str());frame();refresh();setStatus(L"Loaded locally. Geometry is schematic; no scientific inference is connected.");break;
    }
    case Save: saveProject();break;
    case Export: {
        if(!project.hasResult()) break;auto path=fileDialog(true,false,true);if(path.empty()) break;
        auto r=project.result();std::ostringstream csv;csv<<"model,revision,baseline_gc_percent,scenario_gc_percent,edited_bases,evidence\n"<<r.model<<','<<r.revision<<','<<r.baselineGc<<','<<r.scenarioGc<<','<<r.edits<<",composition_only_no_biological_prediction\n";
        atomicWrite(path,csv.str());setStatus(L"Composition results exported as CSV.");break;
    }
    case Undo: mutate(project.undo());break;case Redo: mutate(project.redo());break;case Restore: mutate(project.restoreBaseline());break;
    case Compare: compare=!compare;SetWindowTextW(control(Compare),compare ? L"Compare: on":L"Compare: off");frame();refresh();InvalidateRect(window,nullptr,TRUE);break;
    case Grid: grid=!grid;SetWindowTextW(control(Grid),grid ? L"Grid: on":L"Grid: off");refresh();break;
    case Rotate: rotate=!rotate;SetWindowTextW(control(Rotate),rotate ? L"Rotate: on":L"Rotate: off");break;
    case Frame: frame();break;
    case Sound: sound=!sound;SetWindowTextW(control(Sound),sound ? L"Sound: on":L"Sound: off");break;
    case Run: if(!running) {pending=project.analyze();running=true;started=std::chrono::steady_clock::now();refresh(false);setStatus(L"Running mock composition analysis...");}break;
    case Circuit: circuit::showCircuitWindow(window);break;
    case Cancel: cancel();refresh(false);setStatus(L"Analysis cancelled. Existing data preserved.");break;
    }
}
void Application::tick() {
    if(!renderer || IsIconic(window)) return;
    const auto now=std::chrono::steady_clock::now();float dt=std::min(0.1f,std::chrono::duration<float>(now-lastTick).count());lastTick=now;
    if(rotate && !dragButton) camera.yaw+=dt*0.25f;
    if(running) {
        float elapsed=std::chrono::duration<float>(now-started).count();int progress=std::min(100,static_cast<int>(elapsed/1.2f*100));
        SendMessageW(control(Progress),PBM_SETPOS,progress,0);
        if(progress==100) {running=false;const bool accepted=project.accept(pending);refresh(false);setStatus(accepted ? L"Mock analysis complete. Composition statistics only.":L"Outdated analysis discarded.");if(sound) MessageBeep(MB_OK);}
    }
    std::wstring screenshot;
    if(smoke) {
        ++smokeFrame;
        if(smokeFrame==2) {command(BaseC);command(Compare);camera.orbit(20,-8);camera.zoom(0.5f);}
        if(smokeFrame==3) {SetWindowPos(window,nullptr,0,0,px(1240),px(790),SWP_NOMOVE|SWP_NOZORDER);screenshot=L"evolve-v0.1-viewport.bmp";}
        if(smokeFrame==4) {
            if(renderer->captureNonBackgroundPixels()<500) throw std::runtime_error("Smoke test rendered no visible geometry.");
            command(Run);
        }
        if(smokeFrame>5 && !running) {
            if(!project.hasResult() || project.result().edits!=1) throw std::runtime_error("Smoke test analysis did not complete.");
            atomicWrite(L"smoke-test.evolve",project.serialize());Project reopened;reopened.deserialize(readFile(L"smoke-test.evolve"));
            if(reopened.sequence()!=project.sequence()) throw std::runtime_error("Smoke test save/reopen failed.");
            std::ofstream("smoke-test.log")<<"PASS: DX12 initialization, geometry readback, orbit/zoom, resize, compare, edit, mock analysis, save/reopen. Visible pixels: "<<renderer->captureNonBackgroundPixels()<<'\n';
            dirty=false;DestroyWindow(window);return;
        }
        if(smokeFrame>600) throw std::runtime_error("Smoke test timed out.");
    }
    float lighting=static_cast<float>(SendMessageW(control(Light),TBM_GETPOS,0,0))/100.0f;
    renderer->render(camera,lighting,screenshot);
}
LRESULT Application::message(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    try {
        switch(msg) {
        case WM_GETMINMAXINFO: {auto* info=reinterpret_cast<MINMAXINFO*>(lp);info->ptMinTrackSize={px(1120),px(770)};return 0;}
        case WM_SIZE: if(wp!=SIZE_MINIMIZED) layout();return 0;
        case WM_PAINT: if(window) draw();else {PAINTSTRUCT p{};BeginPaint(hwnd,&p);EndPaint(hwnd,&p);}return 0;
        case WM_DRAWITEM: drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
        case WM_CTLCOLORSTATIC: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN: {
            HDC dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,Text);SetBkColor(dc,msg==WM_CTLCOLORLISTBOX ? Panel:Background);
            return reinterpret_cast<LRESULT>(msg==WM_CTLCOLORLISTBOX ? panelBrush:backgroundBrush);
        }
        case WM_COMMAND:
            if(LOWORD(wp)==BaseList && HIWORD(wp)==LBN_SELCHANGE) {auto i=SendMessageW(control(BaseList),LB_GETCURSEL,0,0);if(i!=LB_ERR) select(static_cast<std::size_t>(i));}
            else if(HIWORD(wp)==BN_CLICKED) command(LOWORD(wp));return 0;
        case WM_TIMER: tick();return 0;
        case WM_MOUSEWHEEL: {
            POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(viewport,&point);
            RECT rect{};GetClientRect(viewport,&rect);
            if(PtInRect(&rect,point)) {camera.zoom(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp))/WHEEL_DELTA);return 0;}
            break;
        }
        case WM_CLOSE: if(confirmReplace()) DestroyWindow(hwnd);return 0;
        case WM_DESTROY: KillTimer(hwnd,1);PostQuitMessage(failed ? 1:0);return 0;
        }
    } catch(const std::exception& e) {
        if(msg==WM_TIMER || msg==WM_SIZE) {KillTimer(hwnd,1);failed=true;error(e);if(!smoke) DestroyWindow(hwnd);}
        else error(e);
        return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
LRESULT Application::viewportMessage(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    try {
        switch(msg) {
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT p{};BeginPaint(hwnd,&p);EndPaint(hwnd,&p);return 0;}
        case WM_LBUTTONDOWN:case WM_RBUTTONDOWN:case WM_MBUTTONDOWN:
            SetFocus(hwnd);SetCapture(hwnd);dragButton=msg==WM_LBUTTONDOWN ? 1:2;last=press={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};dragged=false;return 0;
        case WM_MOUSEMOVE:
            if(dragButton) {POINT next{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};float dx=static_cast<float>(next.x-last.x),dy=static_cast<float>(next.y-last.y);
                if(std::abs(next.x-press.x)+std::abs(next.y-press.y)>4) dragged=true;
                if(dragButton==1) camera.orbit(dx,dy);else {RECT rect{};GetClientRect(hwnd,&rect);camera.pan(dx,dy,rect.bottom);}last=next;}return 0;
        case WM_LBUTTONUP:case WM_RBUTTONUP:case WM_MBUTTONUP:
            if(dragButton==1 && !dragged && renderer) {int i=renderer->pick(GET_X_LPARAM(lp),GET_Y_LPARAM(lp),camera);if(i>=0) select(static_cast<std::size_t>(i));}
            dragButton=0;ReleaseCapture();return 0;
        case WM_CAPTURECHANGED:dragButton=0;return 0;
        case WM_MOUSEWHEEL:camera.zoom(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp))/WHEEL_DELTA);return 0;
        case WM_KEYDOWN:if(wp=='F') frame();return 0;
        }
    } catch(const std::exception& e) {error(e);return 0;}
    return DefWindowProcW(hwnd,msg,wp,lp);
}
int Application::loop() {
    MSG msg{};BOOL result;
    while((result=GetMessageW(&msg,nullptr,0,0))>0) {
        if(circuit::dispatchCircuitMessage(msg)) continue;
        if(msg.message==WM_KEYDOWN && (GetKeyState(VK_CONTROL)&0x8000)) {
            try {if(msg.wParam=='S') {command(Save);continue;}if(msg.wParam=='Z') {command(Undo);continue;}if(msg.wParam=='Y') {command(Redo);continue;}}
            catch(const std::exception& e) {error(e);}
        }
        if(!IsDialogMessageW(window,&msg)) {TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    return result==-1 ? 1:static_cast<int>(msg.wParam);
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int) {
    std::filesystem::path circuitPath;
    Application app;int count{};LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(args) {for(int i=1;i<count;++i) {if(std::wstring(args[i])==L"--warp") app.warp=true;if(std::wstring(args[i])==L"--smoke-test") app.smoke=true;if(std::wstring(args[i])==L"--circuit-trace" && i+1<count) circuitPath=args[++i];}LocalFree(args);}
    try {if(!circuitPath.empty()) return evolve::circuit::runCircuitWindow(circuitPath);app.create(instance);return app.loop();}
    catch(const std::exception& error) {
        if(app.smoke) std::ofstream("smoke-test-error.log")<<error.what();
        else MessageBoxW(nullptr,wide(error.what()).c_str(),L"Evolve.ai could not start",MB_OK|MB_ICONERROR);
        return 1;
    }
}
