# Feature status vs. Safari (macOS)

States:
- **Implemented**: built, run and checked.
- **Partial**: some of it works; the gap is stated.
- **Planned**: not built yet.
- **Not possible on Windows**: the reason is given.

Nothing is marked Implemented until it has been run and checked.

| Area | Feature | State | Notes |
|---|---|---|---|
| Engine | WebKit Windows port (Skia, formerly WinCairo), built from source | Implemented | M1: 38 min build at revision 78c26c6 |
| Engine | Multi-process (UI / WebContent / Network / GPU) | Implemented | All four observed running. Several WebContent processes appear after visiting multiple sites |
| Engine | Crash recovery ("reloaded because a problem occurred") | Implemented | Checked by killing `WebKitWebProcess`: new process, page reloaded, banner shown |
| Chrome | Compact toolbar, back/forward | Partial | Back and forward checked through `WM_APPCOMMAND` (mouse side buttons, media keys). Mouse clicks on the glyphs and keyboard shortcuts aren't covered by automation yet |
| Chrome | Smart Search field: host when idle, full URL on focus | Partial | Idle host display checked (screenshot). Typing plus Enter checked. The visual switch to the full URL on click isn't checked yet |
| Chrome | Lock icon for HTTPS, progress fill in field | Partial | Lock checked (screenshot). Progress fill not captured mid-load yet |
| Chrome | Custom frame, follows dark mode, rounded corners | Partial | Dark mode checked, both windowed and maximized. Light mode and live theme switching not checked. Caption buttons are on the right, following the Windows convention |
| Chrome | Mica backdrop | Partial | Turned on through DWM. In dark mode it looks nearly flat; the effect isn't measured yet |
| Security | Certificate errors block the load | Implemented | expired, wrong host, self-signed and untrusted root (badssl.com) all blocked, with Windows' reason shown in a banner. A full interstitial page is planned for M6 |
| Security | Valid sites whose root isn't installed locally | Partial | Found by reproducing a real failure (example.com). Fixed by verifying with Windows and retrying with full verification. The retry path couldn't be re-run after the fix because the root was already installed by then; it needs a clean machine or VM |
| Chrome | Liquid Glass (refraction, rim light, morphing) | Planned | See GLASS_NOTES.md |
| Tabs | Tabs, pinned tabs, reorder, tab overview, session restore | Planned | M3 |
| Search | Suggestions, Top Hit, start page | Planned | M4 |
| Library | Sidebar, Reading List, Reader mode, Tab Groups | Planned | M5 |
| Privacy | Private windows (non-persistent data store) | Planned | Engine support exists (`WKWebsiteDataStoreCreateNonPersistentDataStore`), already wired in `EngineContext` |
| Privacy | Content blockers, permissions, HTTPS upgrade, privacy report | Planned | M6 |
| Privacy | HTTPS-first for typed hosts | Partial | Typed bare hosts get `https://` (unit tested). Upgrading `http://` links isn't done yet |
| Data | Passwords / AutoFill (DPAPI) | Planned | M6 |
| Data | Downloads manager | Planned | M6 |
| Dev | Web Inspector | Planned | Uses WebKit's own inspector (`WKInspectorShow`). Wired to F12, but not checked yet because keyboard input isn't automated |
| System | Installer, default-browser registration | Planned | M8 |
| Apple-only | iCloud Keychain / iCloud Tabs / Handoff / Continuity | Not possible on Windows | Proprietary Apple services with no Windows API |
| Apple-only | Apple Pay | Not possible on Windows | Needs Apple's platform PassKit / Secure Enclave |
| Apple-only | FairPlay DRM (Apple TV+, some streaming) | Not possible on Windows | Proprietary to Apple's platforms; WinCairo has no FairPlay CDM |
| Apple-only | Safari Web Extensions | Not possible on Windows | WebKit's extension runtime is Cocoa-only |
| Apple-only | SF Symbols / San Francisco font | Not possible on Windows | Licensed for Apple platforms only. Segoe UI Variable and original glyphs are used instead |
