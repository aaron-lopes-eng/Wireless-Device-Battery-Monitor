#pragma once
// An always-on-top, click-through copy of the taskbar strip ("[icon] 30%  [icon] 93%") in a corner of
// the screen, by default the top-left just below where the NVIDIA performance overlay (HUD) sits.
// It shows over desktop apps and borderless / windowed games; exclusive-fullscreen games cover it.
//
// Settings under HKCU\Software\WirelessBattery:
//   OverlayX, OverlayY   (REG_DWORD, signed) screen position in physical pixels; set by dragging it
//   OverlayScale         (REG_DWORD) size in percent, default 100
//   OverlayBackground    (REG_DWORD) 1 = dark plate behind the text (default 0: outlined text only)
#include <windows.h>
#include <vector>
#include "widget.h"

namespace overlay {
// Shows the overlay. Right-clicks while it is unlocked are reported by posting `clickMsg` to `notifyWnd`.
bool Create(HINSTANCE inst, HWND notifyWnd, UINT clickMsg);
void Destroy();
bool Alive();
void SetItems(const std::vector<WidgetItem>& items);
// Unlocked: the overlay takes the mouse and can be dragged; the position is saved when it is dropped.
void SetUnlocked(bool on);
bool Unlocked();
// Back to the default top-left spot (forgets the dragged position).
void ResetPosition();
// Keeps it above other always-on-top windows and on a connected monitor; call about once a second.
void KeepOnTop();
}
