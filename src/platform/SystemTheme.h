#pragma once
// platform/: Windows integration — dark mode, accessibility, DWM backdrop.
#include <windows.h>

namespace meridian::platform {

struct ThemeState {
    bool dark { false };
    bool transparencyEnabled { true };  // Settings > Personalization > Colors > Transparency effects
    bool highContrast { false };
    bool animationsEnabled { true };    // Settings > Accessibility > Visual effects > Animation effects
};

ThemeState queryTheme();

// Applies DWM attributes: immersive dark title bar, rounded corners, and a
// Mica backdrop when transparency is allowed. Returns true if Mica is active.
bool applyWindowMaterial(HWND, const ThemeState&);

// True when running over Remote Desktop (glass falls back to flat material).
bool isRemoteSession();

} // namespace meridian::platform
