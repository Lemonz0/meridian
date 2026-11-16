# Meridian

A Windows 10/11 (x64) web browser built on the open-source **WebKit WinCairo** port. Its layout and interactions follow Safari on macOS. It isn't affiliated with Apple and uses none of Apple's assets, names or fonts.

> The product name is a single constant in `src/config/Brand.h`.

## Status
| Milestone | State |
|---|---|
| 1. Toolchain + WebKit Windows-port build + MiniBrowser loads webkit.org | **done**: 38 min build, see `docs/screenshots/m1-*` |
| 2. Shell skeleton: window, one WebKit view, address field, back/forward/reload, progress | **done**: `tools/smoke-m2.ps1` 13/13, see `docs/screenshots/m2-*` |
| 3. Tabs: favicons, titles, reorder, pinned, overview, session restore | **done**: `tools/smoke-m3.ps1` 23/23, see `docs/screenshots/m3-*` |
| 4. Smart Search, start page, history/bookmarks with real suggestions | **code written, not yet built or run** (cloud session, 2026-11-16). Services unit tests pass under a MinGW/Wine cross-build. Next: MSVC build, `tools/smoke-m4.ps1`, screenshots |
| 5–8 | not started |

Upstream's Windows port now uses **Skia** (`USE_SKIA=ON`); it was formerly "WinCairo". It's the same port, with only the graphics backend changed.

Per-feature status is in [docs/FEATURE_STATUS.md](docs/FEATURE_STATUS.md). Notes on the glass renderer are in [docs/GLASS_NOTES.md](docs/GLASS_NOTES.md).

## Layout
```
src/
  app/        entry point, manifest
  config/     branding constants
  engine/     WebKit2 C API wrapper (WKView/WKPage, clients, data stores)
  ui/         window chrome (Direct2D/DirectWrite), tabs, sidebar
  services/   URL fixup, history, bookmarks, downloads, passwords, content blocking
  platform/   Windows integration: DPI, theme, DWM materials, jump list, default browser
tests/        unit tests (dependency-free runner)
patches/      WebKit patches, if any (none so far)
docs/
```

The shell links only against WebKit's **public C API** (`WebKit/WK*.h`). It doesn't use WTF/WebCore internals, so it can follow WebKit upstream without patches.

## Building
Every exact command and version is in [BUILD_NOTES.md](BUILD_NOTES.md). Short version:
1. Install the toolchain (`../tools/install-toolchain.ps1`) and turn on Developer Mode.
2. Build WebKit: `D:\wk\WebKit\meridian-build.cmd`, which produces `WebKitBuild\Release`.
3. Build Meridian: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DWEBKIT_BUILD_DIR=<WebKitBuild\Release>` then `cmake --build build`.

## Keyboard (implemented so far)
| Action | Keys |
|---|---|
| Focus Smart Search field | Ctrl+L, Alt+D, F6 |
| Reload | Ctrl+R, F5 |
| Back / Forward | Alt+←/→, Ctrl+[ / Ctrl+], mouse X1/X2 |
| Stop loading | Esc |
| Web Inspector | F12, Ctrl+Alt+I, Ctrl+Shift+I |
| New tab / close tab / reopen closed tab | Ctrl+T / Ctrl+W / Ctrl+Shift+T |
| New window / close window | Ctrl+N / Ctrl+Shift+W |
| Next / previous tab | Ctrl+Tab / Ctrl+Shift+Tab, Ctrl+PgDn / Ctrl+PgUp, Ctrl+Shift+] / [ |
| Go to tab 1–8 / last tab | Ctrl+1…8 / Ctrl+9 |
| Tab overview | Ctrl+Shift+\ (arrows to move, Enter to open, Delete to close, Esc to exit) |
| Smart Search dropdown | ↑/↓ to choose, Enter to open, Esc to close the menu (again to stop editing), Shift+Delete removes a history row |
| History | Ctrl+H (↑/↓, Enter to open, Ctrl+Enter in a new tab, Delete to remove, Esc to close) |
| Add page to Favorites | Ctrl+D |

History is Ctrl+H (the Windows browser convention) rather than Safari's ⌘Y, because Ctrl+Y is Redo in text fields on Windows.

Mouse:
- Click a tab to select it.
- Drag tabs to reorder them, or drag a tab out of the bar to move it to a new window.
- Middle-click a tab to close it.
- Right-click a tab for Pin, Duplicate, Move to New Window, and Close Others/Right.
- Ctrl+click or middle-click a link to open it in a background tab. Ctrl+Shift+click opens it in front.
- New tabs show the start page (Favorites, Frequently Visited). Right-click it for Import/Export Bookmarks and the opt-in search-engine suggestions setting.
