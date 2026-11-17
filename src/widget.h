#pragma once
// A small transparent strip on the taskbar, just left of the notification area, showing
// "[icon] 30%  [icon] 93%". Implemented as a layered child window of the taskbar (Shell_TrayWnd).
#include <windows.h>
#include <string>
#include <vector>
#include "devices.h"

struct WidgetItem {
    DeviceKind kind = DeviceKind::Other;
    int  percent  = -1;     // -1 = unknown
    std::wstring text;      // shown instead of the percentage when set (coarse levels like "Low")
    bool charging = false;
    bool dim      = false;  // asleep / stale value
    bool low      = false;
    bool bluetooth = false;  // picks the Bluetooth icon when the device type is unknown
};

namespace widget {
// Creates the widget on the primary taskbar. Clicks are reported by posting `clickMsg` to `notifyWnd`.
bool Create(HINSTANCE inst, HWND notifyWnd, UINT clickMsg, bool lightTheme);
void Destroy();
bool Alive();
void SetItems(const std::vector<WidgetItem>& items, const std::wstring& tooltip);
void SetLightTheme(bool light);
// Draws a rounded plate in the taskbar's colours behind the text (kept across Create/Destroy).
void SetBackground(bool on);
// Re-checks the taskbar layout and moves the widget if needed; call about once a second.
// Returns false when the taskbar it was attached to is gone.
bool Reposition();

// The same "[icon] 30%  [icon] 93%" strip as premultiplied BGRA pixels, drawn in light colours with a
// dark outline so it reads over any game (used by the on-screen overlay). `plateAlpha` > 0 adds a dark
// rounded plate behind it, `border` an accent frame (shown while the overlay is being moved).
struct StripImage {
    int w = 0, h = 0;
    std::vector<DWORD> px;
};
StripImage RenderStrip(const std::vector<WidgetItem>& items, int dpi, int plateAlpha, bool border);
}
