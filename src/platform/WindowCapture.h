#pragma once
// platform/: one-shot capture of part of our own window using
// Windows.Graphics.Capture. Used for tab thumbnails, because WebKit's page is
// GPU-composited into a child HWND that GDI/PrintWindow/WM_PRINTCLIENT cannot
// read (all return black; see docs/GLASS_NOTES.md).
#include "services/Favicons.h" // services::Bitmap
#include <optional>
#include <windows.h>

namespace meridian::platform {

bool windowCaptureSupported();

// Captures `clientRect` (client coordinates, physical pixels) of `hwnd` and
// downscales to at most maxWidth pixels wide. Blocks for up to ~250 ms waiting
// for the first frame; returns nullopt on failure or timeout.
std::optional<services::Bitmap> captureClientRect(HWND hwnd, const RECT& clientRect, int maxWidth);

// Call once at startup (off the UI thread is fine): asks Windows for borderless
// capture so the yellow capture border is not drawn around our window.
void requestBorderlessCapture();

} // namespace meridian::platform
