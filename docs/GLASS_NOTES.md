# Liquid Glass notes

## Status
Not started. M2 chrome uses Direct2D over a DWM Mica backdrop (option (c) below) as a flat placeholder.

## Backdrop-access options (to evaluate)
| Option | What it gives | Status |
|---|---|---|
| (a) WebKit renders offscreen into a texture we own, then we composite it with the chrome in our own D3D11 scene | Real refraction and blur of page content under the glass | **Lead:** the public WinCairo C API exports `WKViewSetUsesOffscreenRendering(WKViewRef, bool)` (`Source/WebKit/UIProcess/API/C/win/WKView.h`). Still to find out how to get the pixels (a shared D3D surface vs. a CPU readback) and what it costs. To be measured once the engine is built. |
| (b) Windows.Graphics.Capture of our own window | Fallback backdrop source | Not evaluated |
| (c) DWM system backdrop (`DWMWA_SYSTEMBACKDROP_TYPE`, Mica/Acrylic) | Outer window material only; can't refract page content | Used in M2 |

## Unverified design details
Apple's HIG, "Adopting Liquid Glass", WWDC25 sessions and any macOS 27 changes have **not** been studied yet. Any detail not confirmed against those sources will be listed here as "unverified".

## Profiling
No measurements yet.
