# Build notes

A running log of every command, version and workaround, so the build can be reproduced.

## Machine (2026-11-16)
- Windows 11 Pro 10.0.29708, Intel i5-14600K (20 threads), 32 GB RAM, RTX 2060 + UHD 770
- Source: https://docs.webkit.org/Ports/WindowsPort.html (read 2026-11-16; the page is undated)

## 1. Toolchain
Installed with winget, not Chocolatey: Chocolatey wasn't present, and WebKit's docs list a winget path.
The script is `tools/install-toolchain.ps1` in the parent folder. Each machine-scope install shows a UAC prompt.

| Tool | winget id | Installed version | Notes |
|---|---|---|---|
| CMake | Kitware.CMake | (machine) | |
| Ninja | Ninja-build.Ninja | user scope | `--scope machine` fails (no applicable installer, 0x8A150019); user scope works, linked at `%LOCALAPPDATA%\Microsoft\WinGet\Links` |
| Python | Python.Python.3.11 | 3.11.9 | WebKit docs require 3.11 (3.12 breaks, webkit.org/b/261113). `pip install pywin32` |
| Ruby | RubyInstallerTeam.Ruby.3.2 | `C:\Ruby32-x64` | |
| Perl | ApacheFriends.Xampp.8.2 | 5.32.1 at `C:\xampp\perl\bin` | WebKit docs recommend XAMPP's Perl (the ActivePerl package is broken). Strawberry Perl avoided because it ships GCC |
| LLVM | LLVM.LLVM | clang-cl 22.1.8 | WebKit builds with `CC=CXX=clang-cl` |
| gperf | GnuWin32.Gperf | `C:\Program Files (x86)\GnuWin32\bin` | not added to PATH automatically |
| VS 2022 Build Tools | Microsoft.VisualStudio.2022.BuildTools | 17.14.45 | `--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended` (MSVC, Windows SDK) |

Other prerequisites:
- `git config --global core.autocrlf input`
- Windows **Developer Mode** must be on, because build-webkit creates a symlink. It's a system setting, so the user enabled it.
- GCC must not be on PATH (checked: it isn't).

## 2. WebKit source
```
git -c core.autocrlf=input -c core.longpaths=true clone --depth 1 https://github.com/WebKit/WebKit.git D:\wk\WebKit
```
- Revision: `78c26c626da9a882c878c9e055c1a148d093eb3c` (2026-11-16)
- Kept on a short path (`D:\wk`) to stay under MAX_PATH.
- The clone is shallow (depth 1), which saves several GB. Use `git fetch --deepen` if history is needed.

## 3. Building WebKit (WinCairo)
`D:\wk\WebKit\meridian-build.cmd` is a non-interactive version of the batch file in the WebKit docs. It:
- adds XAMPP Perl, CMake, LLVM, GnuWin32, Python 3.11, Ruby and Ninja to PATH
- sets `CC=CXX=clang-cl`
- calls `vcvars64.bat`
- then runs:
```
perl Tools\Scripts\build-webkit --release
```
- The current docs omit `--wincairo` because WinCairo is the only Windows port now.
- Observed: third-party dependencies (zlib-ng, ICU, cairo, curl, …) are now **built from source through vcpkg** (cache at `%LOCALAPPDATA%\vcpkg`). They are no longer downloaded prebuilt from WebKitRequirements, so the first build takes longer.
- Incremental: `perl Tools\Scripts\build-webkit --release --skip-library-update`
- **Result:** `WebKit is now built (38m:18s)`, 9,428 ninja steps. Peak RAM use was around 23 GB with 20 parallel `clang-cl` jobs.
- **Output is in `WebKitBuild\Release\bin\`, not `bin64\` as the docs say.** The import lib is `lib\WebKit2.lib` and the C API headers are in `WebKit\Headers\WebKit\`.
- **The port is now Skia-based:** `CMakeCache.txt` has `PORT=Win` and `USE_SKIA=ON`. "WinCairo" is the old name of the same port.
- Runtime needed by an app: 51 DLLs (165 MB; the largest are WebKit2 at 74 MB, JavaScriptCore at 33 MB and ICU data at 30 MB), the three `WebKit*Process.exe` helpers, and `WebKit.resources\` (16 MB). That's about 180 MB. The other ~3.2 GB in `bin\` is PDBs and test binaries.
- MiniBrowser check: `bin\MiniBrowser.exe https://webkit.org/` loaded with title "WebKit" in 5.9 s on a cold start. Screenshot: `docs/screenshots/m1-minibrowser-webkit-org.png`.

Full log: `D:\wk\build.log`.

## 4. Building Meridian
From an x64 Native Tools prompt (or after `vcvars64.bat`):
```
cmake -S meridian -B meridian/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DWEBKIT_BUILD_DIR=D:/wk/WebKit/WebKitBuild/Release
cmake --build meridian/build
meridian\build\bin\Meridian.exe [url]
```
Services-only unit tests (no WebKit needed):
```
cmake -S meridian -B meridian/build-tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DMERIDIAN_BUILD_APP=OFF
cmake --build meridian/build-tests && meridian\build-tests\bin\meridian_tests.exe
```

Smoke test (M2): `powershell -File tools\smoke-m2.ps1`. It drives the app with window messages only: no focus stealing and no input injection.

Screenshots: `tools\capture-window.ps1 -ProcessName Meridian -Screen -Out x.png`.
- `PrintWindow` returns **black** for WebKit's GPU-composited child window, so use `-Screen`.
- `-Screen` makes the window topmost for the capture. It saves **nothing** unless every sample point belongs to the target window, so other apps' content can't leak into screenshots.

## Pitfalls hit
- **MSVC and UTF-8 sources:** without `/utf-8`, `L"—"`-style literals came out as mojibake ("Couldnâ€™t"). The flag is now in `CMakeLists.txt`.
- **Wrong header name:** `WKError.h` doesn't exist; the header is `WKErrorRef.h`.
- **Custom frame:** with the toolbar painted transparent over the extended frame, DWM drew the window title and icon into it. Fixed with `SetWindowThemeAttribute(WTA_NONCLIENT, WTNCA_NODRAWCAPTION|WTNCA_NODRAWICON|WTNCA_NOSYSMENU)`.
- **TLS trust:** WebKit's curl (LibreSSL, `ssl-60.dll`) uses `CURLSSLOPT_NATIVE_CA`, which sees only roots *already installed* in the Windows store. It can't trigger Windows' on-demand root download, so example.com (SSL.com ECC Root 2022) failed until the root was installed by something else. The fix is in `platform/CertificateTrust.cpp`:
  - Reject the challenge. Accepting would make WebKit retry **with verification disabled**: `NetworkDataTaskCurl::restartWithCredential` calls `disableServerTrustEvaluation()`.
  - Verify the chain with `CertGetCertificateChain` + `CERT_CHAIN_POLICY_SSL`, which fetches missing roots.
  - Retry once with full curl verification.

## M3 notes (tabs)
- **SQLite** comes from WebKit's vcpkg tree (`vcpkg_installed/x64-windows-webkit`, 3.53.4). `sqlite3.dll` is already in the shipped WebKit runtime, so it adds no new dependency. The DB is `<profile>\Meridian.db` in WAL mode.
- **Session state:**
  - With a null `context`, `WKPageCopySessionState` returns a **`WKDataRef`**, not a `WKSessionStateRef`. This is a compatibility hack in `WKPage.cpp`.
  - On non-Cocoa ports that data is WebKit's **IPC wire format** (`LegacySessionStateCodingNone.cpp`), so it may not decode with a different WebKit build.
  - Meridian stores it as-is. On restore, a null from `WKSessionStateCreateFromData` falls back to loading the URL.
- **Thumbnails** use Windows.Graphics.Capture (`platform/WindowCapture.cpp`, C++/WinRT, links `windowsapp`). Ruled out:
  - `WM_PRINTCLIENT` to the WKView: WebKit handles it, but with GPU compositing it paints black (`tools/probe-printclient.ps1`).
  - The bundle snapshot API: it returns an `SkImage`, and Skia's pixel accessors aren't exported from WebKit2.dll.
- **WGC on our own window from its UI thread:**
  - The wait for the first frame must dispatch *sent* messages (`MsgWaitForMultipleObjects` + `PM_QS_SENDMESSAGE`).
  - WinRT calls fail with `RPC_E_CANTCALLOUT_ININPUTSYNCCALL` (0x8001010D) inside a cross-process `SendMessage`. That's why automation commands are re-posted.
  - One-shot capture takes about 32–47 ms (logged).
- **Teardown crash (fixed):** destroying a WKView fires state callbacks (`0xc000041d` in WebKit2.dll). `~WebView` now unhooks all page clients first, and tabs are moved out of the vector before they're destroyed.
- **PowerShell gotcha in test scripts:** `$null` passed to a .NET `string` parameter arrives as `""`. Declare optional `LPCWSTR` P/Invoke parameters as `IntPtr` and pass `[IntPtr]::Zero`.

## Test tools
| Tool | What it covers |
|---|---|
| `build\bin\meridian_tests.exe` | Unit tests: URL fixup, SQLite session store, favicon decoding, history/frecency, bookmarks tree + HTML import/export, suggestions ranking, search-suggestion JSON, settings, icon store |
| `tools\smoke-m2.ps1` | Navigation, search, back/forward, certificate errors (temp profile) |
| `tools\smoke-m3.ps1` | Tabs end to end, including quit and relaunch (temp profile, `--automation`) |
| `tools\smoke-m5.ps1` | Reader on an article (Wikipedia), Reading List, sidebar, Tab Groups (create, switch, pinned tabs, rename, delete), restart with a group shown |
| `node tools\reader-check.cjs` | Reader script against `tests\reader\*.html` under jsdom (dev only; needs Node and `npm install jsdom@24`, see the file header) |
| `tools\smoke-m4.ps1` | Smart Search (completion, keys, Shift+Delete), start page, History view, Ctrl+D, bookmark import/export, opt-in engine suggestions, persistence across restart. `-NoSearchSuggestions` skips the one step that sends a query to DuckDuckGo |
| `tools\probe-caption.ps1` | Min/max/close hit-testing, windowed and maximized |
| `tools\capture-window.ps1` | Screenshots. `-Screen` refuses to save unless the window is verifiably on top |

Command-line flags:
- `--profile=<dir>` uses a separate profile.
- `--automation` enables the test command channel (`WM_COPYDATA`) and writes `<profile>\meridian.log`. It's off by default, so other local programs can't drive the browser.

## Log
- 2026-11-16 21:25: started the first WebKit release build.
- 2026-11-16: services unit tests 8/8 pass. They caught a fixup bug where `http://x` was parsed as host:port with an empty port.
- 2026-11-16 22:03: WebKit build finished (38m18s). MiniBrowser loads webkit.org. **M1 verified.**
- 2026-11-16: Meridian builds and links against `WebKit2.lib`. Smoke test 13/13 PASS:
  - navigation, search fallback, back and forward
  - 4 bad-certificate sites stay blocked
  - window appears 0.11 s after launch
  - about 400–500 MB working set across UI, web, network and GPU processes after visiting 6 sites
  **M2 verified.**
- 2026-11-16: the user reported that min/max/close didn't work when maximized. Fixed with direct hit-testing of `DWMWA_CAPTION_BUTTON_BOUNDS`; probe 7/7.
- 2026-11-16: M3 tabs. Unit tests 16/16, M2 13/13, M3 23/23, caption 7/7, no crash events. Bugs found and fixed during verification:
  - close-time crash
  - session state silently empty (the `WKDataRef` hack)
  - thumbnails failing in-process
  - EDIT control translucent over Mica (fixed with `WS_EX_LAYERED`)
  - black thumbnails from tabs that hadn't painted yet

  **M3 verified** (gaps listed in FEATURE_STATUS).
- 2026-11-16 (cloud session): **M4 code written, not yet built with MSVC or run.**
  - New services: `History`, `Bookmarks`, `Suggestions`, `SearchSuggestions`, `Settings`, `IconStore`, `Text`.
  - New UI: `SuggestionPopup`, `StartPage`, `HistoryView`, plus `BrowserWindow`/`App` wiring and a `navigationCommitted` engine callback.
  - Unit tests: 49 cases. They were run by cross-compiling with MinGW-w64 (GCC 13) and running under Wine 9 against the SQLite amalgamation. All M4 tests pass. `decodeScalesToRequestedSize` (M3, passed on the PC) fails only under Wine, whose WIC lacks the scaler.
  - The browser itself was syntax-checked with MinGW against stand-in WebKit headers, not compiled with MSVC/clang-cl. Expect possible small compile fixes on the PC.
  - Bug found by the tests and fixed: typing `https://wiki` or `www.wiki` didn't match `https://www.wikipedia.org` history.
- 2026-11-17 (cloud session): **M5 code written, not yet built with MSVC or run.**
  - New services: `ReadingList`, `TabGroups`, `Reader` (style and result parsing). `SessionStore` gained a window's group and its set-aside tabs, with a migration for M3/M4 profiles.
  - New engine pieces: `reader.js` (original article detection plus the overlay), `ReaderScript`, `WebView::evaluateScript` and `loadFinished`.
  - New UI: `Sidebar` and `BrowserWindowLibrary.cpp`; the start page gained a Reading List section.
  - Build: `cmake/EmbedText.cmake` embeds `reader.js` into the binary as a byte array at build time.
  - Unit tests: 62 cases under MinGW/Wine. All M5 tests pass; the Wine-only favicon failure from M4 is unchanged.
  - Reader checks: 48 under jsdom. They found and fixed:
    - showing Reader twice left the page unscrollable afterwards
    - encyclopedia `[edit]` links and citation markers appeared in the article
  - Live pages couldn't be fetched from the cloud session (its network policy blocks them), so Reader on real sites is unverified until `smoke-m5` runs on the PC.
