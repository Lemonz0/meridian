# Liquid Glass notes

## Status
Not started. M2 chrome uses Direct2D over a DWM Mica backdrop (option (c) below) as a flat placeholder.

## Backdrop-access options (to evaluate)
| Option | What it gives | Status |
|---|---|---|
| (a) WebKit renders offscreen into a texture we own, then we composite it with the chrome in our own D3D11 scene | Real refraction and blur of page content under the glass | **Lead:** the public WinCairo C API exports `WKViewSetUsesOffscreenRendering(WKViewRef, bool)` (`Source/WebKit/UIProcess/API/C/win/WKView.h`). Still to find out how to get the pixels (a shared D3D surface vs. a CPU readback) and what it costs. To be measured once the engine is built. |
| (b) Windows.Graphics.Capture of our own window | Fallback backdrop source | **Works and measured (M3, used for tab thumbnails):**<br>• It captures the GPU-composited page correctly.<br>• A one-shot session takes about 32–47 ms, mostly setup.<br>• For glass it would need a persistent session; on our own window that is a feedback loop, because the glass would capture itself.<br>• Borderless capture is requested; whether a yellow border is ever drawn hasn't been confirmed visually. |
| (–) `WM_PRINTCLIENT` to the WKView | — | **Ruled out:** WebKit handles it, but with GPU compositing the result is black (`tools/probe-printclient.ps1`). |
| (–) Injected-bundle snapshot (`WKBundlePageCreateSnapshot*`) | — | **Blocked:** it returns an `SkImage`, and Skia's pixel accessors aren't exported from WebKit2.dll. Using it would require WebKit internals or a patch. |
| (–) `PrintWindow` / GDI readback of the WebKit child HWND | — | **Ruled out:** it returns black. WebKit composites the page on the GPU into its own child window, so GDI can't read it (seen on 2026-11-16 with both MiniBrowser and Meridian). |
| (c) DWM system backdrop (`DWMWA_SYSTEMBACKDROP_TYPE`, Mica/Acrylic) | Outer window material only; can't refract page content | Used in M2 |

## Unverified design details
Apple's HIG, "Adopting Liquid Glass", WWDC25 sessions and any macOS 27 changes have **not** been studied yet. Any detail not confirmed against those sources will be listed here as "unverified".

- 2026-11-16: tried a plain HTTP fetch of developer.apple.com (adopting-liquid-glass, liquid-glass, HIG materials, SwiftUI `Glass`, `GlassEffectContainer`). It returned only page titles, because the bodies are rendered by JavaScript. Next attempt: read them in a real browser before starting the glass work.

## Profiling
No measurements yet.
