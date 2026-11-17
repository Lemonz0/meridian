# Handoff: where Meridian stands (2026-11-16)

Read this first if you're picking the project up in a new chat. Details live in `README.md`, `BUILD_NOTES.md`, `docs/FEATURE_STATUS.md` and `docs/GLASS_NOTES.md`; this file ties them together.

## 1. What the project is
A Windows 10/11 x64 desktop browser built on the **real open-source WebKit Windows port** (now Skia-based, formerly "WinCairo"). Its look, feel and behavior follow Safari on macOS. It was built from a spec the user pasted; the rules from that spec still apply:

- **Branding:** never call it "Safari". No Apple logos, compass icon, SF Symbols or San Francisco font. The name is **Meridian**, kept in one constant (`src/config/Brand.h`). UI font is Segoe UI Variable (or Inter). Assets must be original or permissively licensed.
- **No fakes:** never swap in Chromium or WebView2, and never fake a feature. If something can't be done, say so plainly and document the fallback.
- **WebKit patches:** avoid them. Any patch that's unavoidable goes in `patches/` with an explanation (there are none so far).
- **Security:** passwords and autofill use DPAPI or Windows Credential Manager, never plaintext. HTTPS-first, certificate-error interstitials, safe `file://` handling. No telemetry or remote code without an explicit opt-in.
- **Storage and layout:** SQLite for persistence. Code is split into `engine/`, `ui/`, `services/` and `platform/`.
- **Working style:**
  - Small commits.
  - After each milestone: build from clean, run, screenshot, and report differences honestly.
  - Never claim parity that hasn't been verified.
  - Keep `docs/FEATURE_STATUS.md`, `BUILD_NOTES.md` and `README.md` current.
- **Only the user decides:** product name, branding, installer signing.
- **Liquid Glass design system:** a GPU shader pipeline, documented in `docs/GLASS_NOTES.md`, plus a "Glass Lab" test page. Accessibility settings must be honored: reduce transparency, high contrast, reduced motion.

### Milestones
| # | Scope | State |
|---|---|---|
| 1 | Toolchain + WebKit build | **done** |
| 2 | Shell skeleton | **done** (+ maximized caption-button fix) |
| 3 | Tabs: favicons, titles, reorder, pinned, overview, session restore | **done** |
| 4 | Smart Search, start page, history/bookmarks with real suggestions | **code written, not built yet** (cloud session). See §6 |
| 5 | Sidebar, Reading List, Reader mode, Tab Groups | **code written, not built yet** (cloud session). See §7 |
| 6 | Privacy: private windows, content blocking, permissions, downloads, passwords/autofill | planned |
| 7 | Settings, Web Inspector integration, keyboard/gesture parity, DPI + dark-mode polish | planned |
| 8 | Installer, crash handling, performance pass, automated test suite | planned |
| — | Liquid Glass shader pipeline + Glass Lab | not started (research blocked, see §5) |

## 2. Environment (the user's PC; it can't be reproduced in a cloud session)
- **Machine:** Windows 11 Pro, i5-14600K, 32 GB RAM, RTX 2060.
- **Toolchain:**
  - VS 2022 Build Tools 17.14, clang-cl 22.1.8 (LLVM), CMake, Ninja (user scope).
  - Python 3.11, Ruby 3.2, XAMPP Perl, gperf.
  - Developer Mode is on.
- **WebKit:**
  - Shallow clone at `D:\wk\WebKit`, revision `78c26c626da9a882c878c9e055c1a148d093eb3c`.
  - Build it with `D:\wk\WebKit\meridian-build.cmd` (≈38 min, ~23 GB RAM peak).
  - Output goes to `D:\wk\WebKit\WebKitBuild\Release` (`bin\`, `lib\WebKit2.lib`, headers in `WebKit\Headers\WebKit\`).
- **Meridian repo:** `C:\Users\lemon\Desktop\safari\meridian`, branch `main`. GitHub: `Lemonz0/meridian` (private, GPLv3 LICENSE).
  - Don't commit in `C:\Users\lemon`; it's an unrelated repo.
- **Build:** from a vcvars64 prompt:
  ```
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DWEBKIT_BUILD_DIR=D:/wk/WebKit/WebKitBuild/Release
  cmake --build build
  ```
  The post-build step stages about 180 MB of WebKit runtime into `build\bin`.
- **Run:** `build\bin\Meridian.exe [url]`.
- **Default profile:** `%LOCALAPPDATA%\Meridian\Default`.
- **Cloud sessions:** they run on Linux. They can't build or run Meridian (no WebKit build, no Win32), so cloud work is unverified until it's pulled and built on the PC.

## 3. What exists (code map)
- `src/config/Brand.h`: name, window class, home URL (webkit.org), search prefix (DuckDuckGo), version.
- `src/app/`:
  - `main.cpp`: DPI awareness v2, OLE init, and the `--profile=<dir>` and `--automation` flags.
  - `App.cpp`: singleton.
    - Owns the profile, SQLite DB (`Meridian.db`), session store, engine context and windows.
    - Saves the session on a 1 s debounce.
    - Keeps the closed-tab stack (50).
    - Routes keyboard shortcuts.
    - Writes the automation state dump.
- `src/engine/`:
  - `WKPtr.h`: RAII for WK refs, plus string helpers.
  - `WebView.cpp`: wraps WKView/WKPage through the **public C API only**.
    - Clients: state, navigation, UI.
    - Ctrl/middle-click opens a link in a new tab (detected from the input tracked in the message loop).
    - Finds favicons via JS after load.
    - Certificate-trust fallback.
    - Session-state save and restore.
    - A safe teardown order (unhook the clients first).
- `src/platform/`:
  - `CertificateTrust.cpp`: verifies the server chain with CryptoAPI.
  - `SystemTheme.cpp`: dark mode, high contrast, animations, Mica.
  - `UiDispatcher.cpp`: `postToUi`.
  - `Log.cpp`: log written only in automation mode.
  - `WindowCapture.cpp`: Windows.Graphics.Capture, used for tab thumbnails.
- `src/services/`:
  - `UrlFixup`: HTTPS-first; non-URLs become searches.
  - `Database`: SQLite RAII wrapper.
  - `SessionStore`: the windows and tabs tables.
  - `Favicons`: WIC decode, WinHTTP fetch with limits, cache.
- `src/ui/`:
  - `Chrome.cpp`: design tokens, palettes, D2D painter glyphs.
  - `TabStrip.cpp`: layout, hit-testing, drag drop index, painting.
  - `TabOverview.cpp`: thumbnail grid, scrolling, focus.
  - `BrowserWindow.cpp`:
    - Custom frame and toolbar with address capsule, progress and lock.
    - Tab operations: open, close, pin, duplicate, move, tear-off, reopen.
    - Shortcuts, caption hit-testing, automation commands.
- `tests/`: dependency-free runner; 16 unit tests (URL fixup, session store, favicon decode).
- `tools/`:
  - `smoke-m2.ps1` (13 checks) and `smoke-m3.ps1` (23 checks, including quit and relaunch). Both drive the app through `WM_COPYDATA` window messages and never steal focus.
  - `probe-caption.ps1` (7).
  - `capture-window.ps1 -Screen`: refuses to save unless the window is verifiably on top. It exists because one capture once grabbed another window; that capture was deleted.
  - `probe-printclient.ps1`, `capture_probe.cpp` (dev probes).
- **Last verified state (M3):** unit 16/16, M2 13/13, M3 23/23, caption 7/7, no crash events.

## 4. Hard-won lessons (don't relearn these)
1. **TLS:**
   - Never *accept* a server-trust challenge: WebKit then retries with verification **disabled**.
   - Cancel it, verify with CryptoAPI (this installs missing roots), and reload once.
2. **Session state:** `WKPageCopySessionState(page, nullptr, nullptr)` returns a `WKDataRef`, not a `WKSessionStateRef`. It's the IPC wire format, so it can be build-specific; fall back to the URL if it doesn't decode.
3. **Teardown:** unhook the page clients before destroying a WKView, and move tabs out of the vector before they're destroyed. Otherwise you get crash `0xc000041d`.
4. **Page pixels:**
   - `PrintWindow`, `WM_PRINTCLIENT` and GDI all give black (GPU compositing).
   - Bundle snapshots need Skia symbols that aren't exported.
   - Only Windows.Graphics.Capture works.
     - It has to pump *sent* messages while waiting.
     - It fails with `RPC_E_CANTCALLOUT_ININPUTSYNCCALL` inside a cross-process `SendMessage`, which is why automation commands are re-posted.
5. **Win32 chrome:**
   - `WTNCA_NODRAWCAPTION|NODRAWICON|NOSYSMENU`, or DWM draws a title into the toolbar.
   - Hit-test caption buttons with `DWMWA_CAPTION_BUTTON_BOUNDS` (this was the maximized bug).
   - The address EDIT needs `WS_EX_LAYERED` at alpha 255 over Mica.
6. **MSVC:** compile with `/utf-8`. The header is `WKErrorRef.h`, not `WKError.h`.
7. **PowerShell P/Invoke:** `$null` passed to a `string` parameter arrives as `""`. Use `IntPtr`.

## 5. Known gaps and open items
- **SVG favicons** aren't supported (a globe is shown).
- **No animations** anywhere yet (reorder, overview zoom). Spring motion is planned with the glass work.
- **Tab thumbnails:** not persisted across restarts; a tab has one only after it has been visible.
- **Not automated:**
  - Mouse drag-reorder, hover close, middle-click close, Ctrl+click on links.
  - Whether a yellow WGC capture border ever shows. The user should be asked.
- **Sized popups** open as tabs.
- **Compact tab layout** (tabs inside the toolbar) isn't built.
- The top 1–2 px band can show the window behind it (the DWM resize border).
- The missing-root TLS retry path needs a clean machine or VM to re-verify.
- Light mode and live theme switching aren't checked. Progress fill hasn't been captured mid-load.
- **Liquid Glass research:**
  - Apple's HIG, "Adopting Liquid Glass" and the WWDC25 pages render with JavaScript, so a plain fetch returned only titles. Read them in a real browser (the built-in browser tool) before designing.
  - Until then, every glass detail is marked "unverified".
  - Backdrop lead: `WKViewSetUsesOffscreenRendering`. Still to find out how to get its pixels (shared D3D surface vs. CPU readback) and what it costs.
- **Private windows:** the engine support is already wired (`EngineContext(ephemeral=true)`), but there's no UI yet (M6).
- **`Brand.h` `kVersion`** is `0.4.0-m4` (bumped with the M4 code).

## 6. Milestone 4: written, waiting for a Windows build
All M4 code was written in a cloud session on 2026-11-16 and pushed to branch `claude/bold-bohr-0sgcne`. **It hasn't been built with MSVC or run.**

What was verified there:
- **Unit tests:** cross-compiled with MinGW and run under Wine; 49 cases pass. The exception is the M3 favicon scaling test, which fails only under Wine.
- **UI and app code:** syntax-checked against stand-in WebKit headers.

What exists:
- **Services** (SQLite, `services/`):
  - `History`: per-URL rows plus visit rows, frecency, clearing.
  - `Bookmarks`: tree, Favorites/Bookmarks roots, Netscape HTML import/export.
  - `Suggestions`: Top Hit, completion, ranking.
  - `SearchSuggestions`: opt-in fetch and JSON parsing.
  - `Settings`, `IconStore`, `Text`.
- **UI** (`ui/`):
  - `SuggestionPopup`: non-activating dropdown.
  - `StartPage`: native.
  - `HistoryView`: Ctrl+H.
  - `BrowserWindow` wiring: typing, completion, keys, Ctrl+D, context menus, automation commands (`type`, `key`, `pick`, `dumpui`, `history*`, `bookmark`, `import/exportbookmarks`, `setting suggest`, `starttile`).
- **Engine:** a `navigationCommitted` callback (`didCommitNavigation`) records visits.
- **Tests:** `tests/HistoryTests.cpp`, `BookmarkTests.cpp`, `SuggestionTests.cpp`; `tools/smoke-m4.ps1`.

To finish M4 on the PC:
1. Pull the branch. Build from clean (the commands are in §2). Fix any compile errors MSVC/clang-cl reports; the likeliest spots are in `BrowserWindow.cpp`.
2. Run `build\bin\meridian_tests.exe`, then `smoke-m2`, `smoke-m3` and `tools\smoke-m4.ps1`. Smoke-m4 saves `m4-smart-search.png`, `m4-start-page.png` and `m4-history.png`.
3. Look at the screenshots and report the differences honestly. Things not checked yet:
   - the dropdown's look (rounded corners, shadow, dark/light)
   - real key presses rather than automation
   - IME
   - mouse hover and click in the dropdown and on tiles
4. Update `FEATURE_STATUS.md` from "Partial / not run yet" to whatever is actually verified, then mark M4 done here and in `README.md`.

Deliberate choices to confirm with the user:
- History is on **Ctrl+H**, not Ctrl+Y (that's Redo on Windows).
- **Ctrl+D** adds straight to Favorites, without Safari's sheet.
- The search-suggestion opt-in lives in the start page's right-click menu until Settings (M7).

## 7. Milestone 5: written, waiting for a Windows build
Written in a cloud session on 2026-11-17, on the same branch, after M4. **It hasn't been built with MSVC or run.**

What was verified there:
- **Unit tests:** cross-compiled with MinGW and run under Wine; 62 cases pass. The exception is the Wine-only favicon case.
- **Reader script:** 48 checks pass under jsdom (`node tools/reader-check.cjs`).
- **UI and app code:** syntax-checked against stand-in WebKit headers. `WebView.cpp` was only reviewed by eye.

What exists:
- **Services:**
  - `ReadingList`.
  - `TabGroups`: groups plus each group's saved tabs.
  - `Reader`: style settings, result parsing, JS string quoting.
  - `SessionStore`: now stores each window's `groupId` and its set-aside (`stash`) tabs; old profiles get the new columns added.
- **Engine:**
  - `reader.js`, embedded by `cmake/EmbedText.cmake` into `build/generated/ReaderScriptData.h`.
  - `ReaderScript`.
  - `WebView::evaluateScript`, guarded by a per-view serial, and `loadFinished`.
- **UI:**
  - `Sidebar`.
  - `BrowserWindowLibrary.cpp`: sidebar, groups, Reading List, Reader.
  - Toolbar: sidebar button and group picker. Address field: Reader button.
  - Start page: Reading List section.
- **Tests:**
  - `tests/LibraryTests.cpp`, `ReaderTests.cpp`.
  - `tests/reader/*.html` with `tools/reader-check.cjs`.
  - `tools/smoke-m5.ps1`.

To finish M5 on the PC (after M4's steps in §6):
1. Build from clean and fix any compile errors. The likeliest spots are `BrowserWindowLibrary.cpp`, `BrowserWindow.cpp` and `WebView.cpp`. Check that `build/generated/ReaderScriptData.h` gets generated.
2. Run the unit tests, then `smoke-m2` to `smoke-m5`. Smoke-m5 saves:
   - `m5-reader.png`
   - `m5-reader-sepia.png`
   - `m5-sidebar.png`
   - `m5-groups.png`
3. Look at the screenshots. Check by hand what automation doesn't cover:
   - **Real input:** sidebar clicks, inline rename typing, the group picker menu, the Reader button, F9 and Esc.
   - **Reader on live sites:** news sites, blogs, documentation.
   - **Page restrictions:** whether a strict Content-Security-Policy blocks Reader's styles.
4. Update `FEATURE_STATUS.md` from "Partial / not run yet" to whatever is verified.

Deliberate choices to confirm with the user:
- **Reader is F9**, not Ctrl+Shift+R.
- **Reading List doesn't save pages for offline reading**; the WebKit C API offers no page archive.
- **Sidebar state isn't remembered** across restarts.
