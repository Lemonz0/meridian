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
