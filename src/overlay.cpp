#include "overlay.h"

#include <algorithm>
#include <cstring>

namespace {

HWND g_wnd, g_notify;
UINT g_clickMsg;
bool g_unlocked, g_moving;
std::vector<WidgetItem> g_items;

const wchar_t* kClass = L"WirelessBatteryOverlay";
const wchar_t* kKey = L"Software\\WirelessBattery";

bool ReadDword(const wchar_t* name, DWORD& v) {
    DWORD sz = sizeof(v);
    return RegGetValueW(HKEY_CURRENT_USER, kKey, name, RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS;
}
DWORD ReadDwordOr(const wchar_t* name, DWORD def) {
    DWORD v = def;
    return ReadDword(name, v) ? v : def;
}
void WriteDword(const wchar_t* name, DWORD v) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kKey, name, REG_DWORD, &v, sizeof(v));
}

int Dpi() {
    UINT d = g_wnd ? GetDpiForWindow(g_wnd) : 0;
    return d ? (int)d : 96;
}

// Top-left of the primary monitor, leaving room above for the NVIDIA performance overlay
// (FPS / GPU / CPU lines). Drag the overlay once to fit your HUD's exact height.
POINT DefaultPos() {
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
    int dpi = (int)GetDpiForSystem();
    return { mi.rcMonitor.left + MulDiv(12, dpi, 96), mi.rcMonitor.top + MulDiv(130, dpi, 96) };
}

// The dragged position, if there is one and it is still on a connected monitor.
bool SavedPos(POINT& p) {
    DWORD x, y;
    if (!ReadDword(L"OverlayX", x) || !ReadDword(L"OverlayY", y)) return false;
    p = { (LONG)(int)x, (LONG)(int)y };
    return MonitorFromPoint(p, MONITOR_DEFAULTTONULL) != nullptr;
}

void Place() {
    if (!g_wnd) return;
    POINT p;
    if (!SavedPos(p)) p = DefaultPos();
    SetWindowPos(g_wnd, HWND_TOPMOST, p.x, p.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

void Paint() {
    if (!g_wnd) return;
    int scale = std::clamp((int)ReadDwordOr(L"OverlayScale", 100), 50, 400);
    int plate = g_unlocked ? 210 : ReadDwordOr(L"OverlayBackground", 0) ? 140 : 0;
    auto img = widget::RenderStrip(g_items, MulDiv(Dpi(), scale, 100), plate, g_unlocked);
    if (img.w <= 0 || img.h <= 0) return;

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), img.w, -img.h, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) {
        memcpy(bits, img.px.data(), (size_t)img.w * img.h * 4);
        HGDIOBJ ob = SelectObject(mem, bmp);
        SIZE sz{ img.w, img.h };
        POINT src{ 0, 0 };
        BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        UpdateLayeredWindow(g_wnd, screen, nullptr, &sz, mem, &src, 0, &bf, ULW_ALPHA);  // keeps the position
        SelectObject(mem, ob);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

void ApplyClickThrough() {
    if (!g_wnd) return;
    LONG_PTR ex = GetWindowLongPtrW(g_wnd, GWL_EXSTYLE);
    LONG_PTR want = g_unlocked ? (ex & ~(LONG_PTR)WS_EX_TRANSPARENT) : (ex | WS_EX_TRANSPARENT);
    if (want != ex) {
        SetWindowLongPtrW(g_wnd, GWL_EXSTYLE, want);
        SetWindowPos(g_wnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
}

LRESULT CALLBACK OverlayProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCHITTEST:
            // Locked: let every click through to the game. Unlocked: the whole strip is a drag handle.
            return g_unlocked ? HTCAPTION : HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ENTERSIZEMOVE:
            g_moving = true;
            return 0;
        case WM_EXITSIZEMOVE: {
            g_moving = false;
            RECT r;
            GetWindowRect(h, &r);
            WriteDword(L"OverlayX", (DWORD)r.left);
            WriteDword(L"OverlayY", (DWORD)r.top);
            return 0;
        }
        case WM_NCRBUTTONUP:
        case WM_RBUTTONUP:
            if (g_notify) PostMessageW(g_notify, g_clickMsg, 0, 0);
            return 0;
        case WM_CONTEXTMENU:
            return 0;
        case WM_DPICHANGED:  // moved to a monitor with another scale: redraw at the new size, same spot
            Paint();
            return 0;
        case WM_DISPLAYCHANGE:
            if (!g_moving) Place();
            Paint();
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

namespace overlay {

bool Create(HINSTANCE inst, HWND notifyWnd, UINT clickMsg) {
    if (Alive()) return true;
    g_notify = notifyWnd;
    g_clickMsg = clickMsg;
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = OverlayProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    // Tool window: no taskbar button and not in Alt+Tab. Never activated, so it never steals focus from a game.
    g_wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                            kClass, L"Wireless Battery overlay", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, inst, nullptr);
    if (!g_wnd) return false;
    ApplyClickThrough();
    Place();  // before painting, so the size follows the target monitor's DPI
    Paint();
    ShowWindow(g_wnd, SW_SHOWNOACTIVATE);
    return true;
}

void Destroy() {
    if (g_wnd) DestroyWindow(g_wnd);
    g_wnd = nullptr;
    g_moving = false;
}

bool Alive() { return g_wnd && IsWindow(g_wnd); }

void SetItems(const std::vector<WidgetItem>& items) {
    g_items = items;
    Paint();
}

void SetUnlocked(bool on) {
    g_unlocked = on;
    ApplyClickThrough();
    Paint();
}

bool Unlocked() { return g_unlocked; }

void ResetPosition() {
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kKey, L"OverlayX");
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kKey, L"OverlayY");
    Place();
    Paint();
}

void KeepOnTop() {
    if (!Alive() || g_moving) return;
    if (!MonitorFromWindow(g_wnd, MONITOR_DEFAULTTONULL)) {  // its monitor was unplugged
        Place();
        Paint();
        return;
    }
    // Games and other always-on-top windows can end up above us; step back in front without activating.
    SetWindowPos(g_wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

}  // namespace overlay
