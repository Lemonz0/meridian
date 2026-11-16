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
- Output: `D:\wk\WebKit\WebKitBuild\Release\bin64\MiniBrowser.exe`

Full log: `D:\wk\build.log`. Timings are recorded below once the build finishes.

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

## Log
- 2026-11-16 21:25: started the first WebKit release build.
- 2026-11-16: services unit tests 8/8 pass. They caught a fixup bug where `http://x` was parsed as host:port with an empty port.
