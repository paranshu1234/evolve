#include "desktop/CircuitWindow.h"
#include "core/CircuitTrace.h"
#include "core/CircuitScene.h"
#include <commctrl.h>
#include <commdlg.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace evolve::circuit {
namespace {
constexpr wchar_t WindowClass[] = L"EvolveCircuitInspector";
constexpr int Play = 500, Reset = 501, Open = 502, Timeline = 503, Speed = 504, Label = 505;
constexpr int SliderSteps = 10000;
constexpr int ContentWidth = 1100, ContentHeight = 800, ControlsY = 738;
constexpr COLORREF Background = RGB(10, 18, 29), Foreground = RGB(227, 237, 245);

std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!count) return L"[invalid UTF-8 label]";
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}
std::filesystem::path packagedExample() {
    std::vector<wchar_t> buffer(32768);
    DWORD count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!count || count == buffer.size()) return {};
    return std::filesystem::path(std::wstring(buffer.data(), count)).parent_path() / L"circuit" / L"repressilator.csv";
}
std::filesystem::path chooseTrace(HWND owner) {
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"Evolve circuit traces (*.csv)\0*.csv\0\0";
    dialog.lpstrFile = path; dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.lpstrTitle = L"Open a local Evolve circuit trace";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) {
        const auto error = CommDlgExtendedError();
        if (error) throw std::runtime_error("The trace file dialog could not open (error " + std::to_string(error) + ").");
        return {};
    }
    return path;
}
CircuitTrace loadTrace(const std::filesystem::path& path) {
    // Filesystem streams preserve Unicode Windows paths; the parser does not use locale-dependent numbers.
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open the selected circuit trace.");
    return CircuitTrace::read(input, "selected circuit trace");
}

class Inspector {
public:
    HWND window{};
    bool standalone{};
    explicit Inspector(CircuitTrace trace) : trace_(std::move(trace)), clock_(trace_.duration(), 10.0) {}
    ~Inspector() {
        if (window && IsWindow(window)) DestroyWindow(window);
        if (font_) DeleteObject(font_);
        DeleteObject(brush_);
    }
    void create(HWND owner);
    void load(const std::filesystem::path& path) {
        auto candidate = loadTrace(path); // Transactional: bad/cancelled imports retain the previous run.
        trace_ = std::move(candidate);
        clock_ = PlaybackController(trace_.duration(), clock_.speedMinutesPerSecond());
        lastTick_ = std::chrono::steady_clock::now();
        refresh();
    }
    void command(int id);
    LRESULT message(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);
    bool dispatch(MSG& message);
private:
    CircuitTrace trace_;
    PlaybackController clock_;
    HFONT font_{};
    HBRUSH brush_{CreateSolidBrush(Background)};
    double scale_{1.0};
    double dpiScale_{1.0};
    int offsetX_{}, offsetY_{};
    bool minimized_{};
    std::chrono::steady_clock::time_point lastTick_{};
    int px(double value) const { return static_cast<int>(std::lround(value * scale_)); }
    HWND control(int id) const { return GetDlgItem(window, id); }
    void layout();
    void paint();
    void refresh();
    void tick();
    HWND add(const wchar_t* type, const wchar_t* label, int id, DWORD style);
    void report(const std::exception& error) { MessageBoxW(window, wide(error.what()).c_str(), L"Evolve circuit inspector", MB_OK | MB_ICONERROR); }
};
std::unique_ptr<Inspector> inspector;

LRESULT CALLBACK windowProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto* current = reinterpret_cast<Inspector*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        current = static_cast<Inspector*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        current->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(current));
    }
    return current ? current->message(hwnd, message, wp, lp) : DefWindowProcW(hwnd, message, wp, lp);
}
HWND Inspector::add(const wchar_t* type, const wchar_t* label, int id, DWORD style) {
    const auto child = CreateWindowExW(0, type, label, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1,
        window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    if (!child) throw std::runtime_error("Cannot create a circuit inspector control.");
    return child;
}
void Inspector::create(HWND owner) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
    if (!InitCommonControlsEx(&controls)) throw std::runtime_error("Cannot initialize circuit playback controls.");
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = windowProcedure; wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = nullptr; wc.lpszClassName = WindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throw std::runtime_error("Cannot register the circuit inspector window.");
    const auto screen = GetDC(nullptr);
    if (screen) { dpiScale_ = static_cast<double>(GetDeviceCaps(screen, LOGPIXELSX)) / 96.0; ReleaseDC(nullptr, screen); }
    RECT area{0, 0, static_cast<LONG>(ContentWidth * dpiScale_), static_cast<LONG>(ContentHeight * dpiScale_)};
    AdjustWindowRectEx(&area, WS_OVERLAPPEDWINDOW, FALSE, 0);
    window = CreateWindowExW(0, WindowClass, L"Evolve | Repressilator circuit inspector", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, area.right - area.left, area.bottom - area.top, owner, nullptr, wc.hInstance, this);
    if (!window) throw std::runtime_error("Cannot create the circuit inspector window.");
    add(L"BUTTON", L"Play", Play, WS_TABSTOP | BS_PUSHBUTTON);
    add(L"BUTTON", L"Reset", Reset, WS_TABSTOP | BS_PUSHBUTTON);
    add(L"BUTTON", L"Open trace...", Open, WS_TABSTOP | BS_PUSHBUTTON);
    add(TRACKBAR_CLASSW, L"Simulation time", Timeline, WS_TABSTOP | TBS_HORZ | TBS_NOTICKS);
    SendMessageW(control(Timeline), TBM_SETRANGE, TRUE, MAKELPARAM(0, SliderSteps));
    SendMessageW(control(Timeline), TBM_SETPAGESIZE, 0, SliderSteps / 20);
    add(L"STATIC", L"min / real second", Label, 0);
    add(L"COMBOBOX", L"Playback speed in minutes per real second", Speed, WS_TABSTOP | CBS_DROPDOWNLIST);
    for (const auto* value : {L"1", L"5", L"10", L"30", L"60"}) SendMessageW(control(Speed), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    SendMessageW(control(Speed), CB_SETCURSEL, 2, 0);
    layout(); refresh();
    lastTick_ = std::chrono::steady_clock::now();
    if (!SetTimer(window, 1, 33, nullptr)) throw std::runtime_error("Cannot start the circuit presentation timer.");
    ShowWindow(window, SW_SHOW); UpdateWindow(window);
}
void Inspector::layout() {
    if (!control(Play)) return;
    RECT client{}; GetClientRect(window, &client);
    scale_ = std::min(static_cast<double>(client.right) / ContentWidth, static_cast<double>(client.bottom) / ContentHeight);
    scale_ = std::max(0.1, scale_);
    offsetX_ = (client.right - px(ContentWidth)) / 2;
    offsetY_ = (client.bottom - px(ContentHeight)) / 2;
    auto place = [&](int id, int x, int y, int width, int height) {
        MoveWindow(control(id), offsetX_ + px(x), offsetY_ + px(y), px(width), px(height), TRUE);
    };
    // Replace only after all controls can select the new font; no selected GDI handle is deleted.
    const auto nextFont = CreateFontW(-px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (nextFont) {
        for (int id : {Play, Reset, Open, Timeline, Speed, Label}) SendMessageW(control(id), WM_SETFONT, reinterpret_cast<WPARAM>(nextFont), TRUE);
        if (font_) DeleteObject(font_);
        font_ = nextFont;
    }
    place(Play, 24, ControlsY, 80, 32); place(Reset, 112, ControlsY, 80, 32); place(Open, 200, ControlsY, 130, 32);
    place(Timeline, 344, ControlsY, 475, 32); place(Speed, 835, ControlsY, 68, 180); place(Label, 916, ControlsY + 7, 165, 26);
    InvalidateRect(window, nullptr, FALSE);
}
void Inspector::refresh() {
    if (!window) return;
    SetWindowTextW(control(Play), clock_.playing() ? L"Pause" : L"Play");
    SendMessageW(control(Timeline), TBM_SETPOS, TRUE,
        static_cast<LPARAM>(std::lround((clock_.simulationMinutes() / trace_.duration()) * SliderSteps)));
    InvalidateRect(window, nullptr, FALSE);
}
void Inspector::tick() {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(now - lastTick_).count();
    lastTick_ = now;
    if (!minimized_ && clock_.playing()) { clock_.advance(elapsed); refresh(); }
}
void Inspector::command(int id) {
    tick(); // Account for elapsed time under the old speed/play state before changing it.
    if (id == Play) { if (clock_.playing()) clock_.pause(); else clock_.play(); }
    if (id == Reset) clock_.reset();
    if (id == Open) {
        clock_.pause(); refresh();
        const auto path = chooseTrace(window);
        if (!path.empty()) load(path);
    }
    lastTick_ = std::chrono::steady_clock::now();
    refresh();
}

// Rendering adapter is defined below; it consumes the same portable commands as SVG export.
void Inspector::paint() {
    // Build before acquiring GDI resources; outer error handling validates the paint region.
    const auto scene = buildCircuitScene(trace_, clock_.simulationMinutes());
    struct PaintSession {
        HWND window; PAINTSTRUCT paint{}; HDC target;
        explicit PaintSession(HWND value) : window(value), target(BeginPaint(value, &paint)) {}
        ~PaintSession() { EndPaint(window, &paint); }
    } session(window);
    const auto target = session.target;
    RECT client{}; GetClientRect(window, &client);
    struct BackBuffer {
        HDC dc{}; HBITMAP bitmap{}; HGDIOBJ old{};
        ~BackBuffer() {
            if (old) SelectObject(dc, old);
            if (bitmap) DeleteObject(bitmap);
            if (dc) DeleteDC(dc);
        }
    } buffer;
    buffer.dc = CreateCompatibleDC(target);
    buffer.bitmap = CreateCompatibleBitmap(target, std::max(1L, client.right), std::max(1L, client.bottom));
    if (!buffer.dc || !buffer.bitmap) return;
    const auto dc = buffer.dc;
    buffer.old = SelectObject(dc, buffer.bitmap);
    FillRect(dc, &client, brush_); SetBkMode(dc, TRANSPARENT);
    auto color = [](const std::string& hex) {
        unsigned value = 0;
        if (hex.size() == 7 && hex.front() == '#') {
            for (std::size_t i = 1; i < hex.size(); ++i) {
                const auto c = hex[i];
                value = value * 16U + static_cast<unsigned>(c >= '0' && c <= '9' ? c - '0' :
                    c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0);
            }
        }
        return RGB((value >> 16U) & 255U, (value >> 8U) & 255U, value & 255U);
    };
    for (const auto& command : scene.commands) {
        if (command.type == SceneCommandType::Text) {
            const auto label = wide(command.text);
            const auto face = CreateFontW(-std::max(1, px(command.fontSize)), 0, 0, 0,
                command.bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            const auto oldFont = face ? SelectObject(dc, face) : nullptr;
            SetTextColor(dc, color(command.fill));
            SetTextAlign(dc, TA_BASELINE | (command.textAnchor == TextAnchor::Middle ? TA_CENTER :
                command.textAnchor == TextAnchor::End ? TA_RIGHT : TA_LEFT));
            TextOutW(dc, offsetX_ + px(command.x), offsetY_ + px(command.y), label.c_str(), static_cast<int>(label.size()));
            if (face) { SelectObject(dc, oldFont); DeleteObject(face); }
            continue;
        }
        const bool hasFill = command.fill != "none" && !command.fill.empty();
        const bool hasStroke = command.stroke != "none" && !command.stroke.empty();
        // Finish all potentially throwing allocations before selecting any GDI handles.
        std::vector<POINT> points; points.reserve(command.points.size());
        for (const auto& point : command.points) points.push_back({offsetX_ + px(point.x), offsetY_ + px(point.y)});
        std::vector<DWORD> pattern;
        for (double length : command.dashPattern) pattern.push_back(static_cast<DWORD>(std::max(1, px(length))));
        const auto fill = hasFill ? CreateSolidBrush(color(command.fill)) : nullptr;
        const auto oldBrush = SelectObject(dc, fill ? fill : GetStockObject(NULL_BRUSH));
        HPEN pen{};
        if (hasStroke) {
            LOGBRUSH penBrush{BS_SOLID, color(command.stroke), 0};
            pen = ExtCreatePen(PS_GEOMETRIC | PS_ENDCAP_FLAT | PS_JOIN_ROUND | (pattern.empty() ? PS_SOLID : PS_USERSTYLE),
                static_cast<DWORD>(std::max(1, px(command.strokeWidth))), &penBrush,
                static_cast<DWORD>(pattern.size()), pattern.empty() ? nullptr : pattern.data());
        }
        const auto oldPen = SelectObject(dc, pen ? pen : GetStockObject(NULL_PEN));
        if (command.type == SceneCommandType::Rect) {
            RoundRect(dc, offsetX_ + px(command.x), offsetY_ + px(command.y),
                offsetX_ + px(command.x + command.width), offsetY_ + px(command.y + command.height),
                px(2 * command.radius), px(2 * command.radius));
        } else {
            if (points.size() >= 2) Polyline(dc, points.data(), static_cast<int>(points.size()));
        }
        SelectObject(dc, oldPen); SelectObject(dc, oldBrush);
        if (pen) DeleteObject(pen); if (fill) DeleteObject(fill);
    }
    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
}
LRESULT Inspector::message(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    try {
        switch (message) {
        case WM_GETMINMAXINFO: {
            RECT bounds{0, 0, static_cast<LONG>(ContentWidth * dpiScale_), static_cast<LONG>(ContentHeight * dpiScale_)}; AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, FALSE, 0);
            auto* info = reinterpret_cast<MINMAXINFO*>(lp);
            info->ptMinTrackSize = {bounds.right - bounds.left, bounds.bottom - bounds.top}; return 0;
        }
        case WM_SIZE:
            minimized_ = wp == SIZE_MINIMIZED;
            if (minimized_) clock_.pause();
            lastTick_ = std::chrono::steady_clock::now(); layout(); refresh(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint(); return 0;
        case WM_TIMER: if (wp == 1) tick(); return 0;
        case WM_CTLCOLORSTATIC: {
            const auto dc = reinterpret_cast<HDC>(wp); SetTextColor(dc, Foreground); SetBkColor(dc, Background);
            return reinterpret_cast<LRESULT>(brush_);
        }
        case WM_COMMAND:
            if (HIWORD(wp) == BN_CLICKED) { command(LOWORD(wp)); return 0; }
            if (LOWORD(wp) == Speed && HIWORD(wp) == CBN_SELCHANGE) {
                const auto selection = SendMessageW(control(Speed), CB_GETCURSEL, 0, 0);
                constexpr double speeds[]{1, 5, 10, 30, 60};
                if (selection >= 0 && selection < 5) { tick(); clock_.setSpeed(speeds[selection]); refresh(); }
                return 0;
            }
            break;
        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lp) == control(Timeline)) {
                clock_.seek(trace_.duration() * (static_cast<double>(SendMessageW(control(Timeline), TBM_GETPOS, 0, 0)) / SliderSteps));
                lastTick_ = std::chrono::steady_clock::now(); refresh(); return 0;
            }
            break;
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
        case WM_DESTROY:
            KillTimer(hwnd, 1); window = nullptr;
            if (standalone) PostQuitMessage(0); return 0;
        }
    } catch (const std::exception& error) {
        clock_.pause();
        if (message == WM_TIMER || message == WM_PAINT) KillTimer(hwnd, 1);
        if (message == WM_PAINT) ValidateRect(hwnd, nullptr);
        report(error); return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
bool Inspector::dispatch(MSG& message) {
    if (!window || (message.hwnd != window && !IsChild(window, message.hwnd))) return false;
    if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) { SendMessageW(window, WM_CLOSE, 0, 0); return true; }
    if (message.message == WM_KEYDOWN && message.wParam == 'O' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        try { command(Open); } catch (const std::exception& error) { report(error); }
        return true;
    }
    if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    return true;
}
} // namespace

void showCircuitWindow(HWND owner, const std::filesystem::path& requestedPath) {
    if (inspector && inspector->window) {
        if (!requestedPath.empty()) inspector->load(requestedPath);
        ShowWindow(inspector->window, SW_RESTORE); SetForegroundWindow(inspector->window); return;
    }
    auto path = requestedPath;
    if (path.empty()) {
        path = packagedExample();
        if (path.empty() || !std::filesystem::is_regular_file(path)) path = chooseTrace(owner);
    }
    if (path.empty()) return;
    auto candidate = std::make_unique<Inspector>(loadTrace(path));
    candidate->create(owner);
    inspector = std::move(candidate);
}
bool dispatchCircuitMessage(MSG& message) { return inspector && inspector->dispatch(message); }
int runCircuitWindow(const std::filesystem::path& path) {
    SetProcessDPIAware();
    showCircuitWindow(nullptr, path);
    if (!inspector || !inspector->window) return 0;
    inspector->standalone = true;
    MSG message{}; BOOL status;
    while ((status = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (!dispatchCircuitMessage(message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    inspector.reset();
    return status == -1 ? 1 : static_cast<int>(message.wParam);
}
} // namespace evolve::circuit
