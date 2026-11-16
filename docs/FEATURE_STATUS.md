# Feature status vs. Safari (macOS)

States:
- **Implemented**: built, run and checked.
- **Partial**: some of it works; the gap is stated.
- **Planned**: not built yet.
- **Not possible on Windows**: the reason is given.

Nothing is marked Implemented until it has been run and checked.

| Area | Feature | State | Notes |
|---|---|---|---|
| Engine | WebKit WinCairo engine | Planned | Source build in progress (M1) |
| Engine | Multi-process (UI / WebContent / Network) | Planned | Built into WebKit2 |
| Engine | Crash recovery ("reloaded because a problem occurred") | Planned | Code written in M2 using `webProcessDidCrash`; not run yet |
| Chrome | Compact toolbar, back/forward | Planned | Code written (M2) |
| Chrome | Smart Search field: host when idle, full URL on focus | Planned | Code written (M2) |
| Chrome | Lock icon for HTTPS, progress fill in field | Planned | Code written (M2) |
| Chrome | Mica title area, rounded corners, follows light/dark mode | Planned | Code written (M2) |
| Chrome | Liquid Glass (refraction, rim light, morphing) | Planned | See GLASS_NOTES.md |
| Tabs | Tabs, pinned tabs, reorder, tab overview, session restore | Planned | M3 |
| Search | Suggestions, Top Hit, start page | Planned | M4 |
| Library | Sidebar, Reading List, Reader mode, Tab Groups | Planned | M5 |
| Privacy | Private windows (non-persistent data store) | Planned | Engine support exists (`WKWebsiteDataStoreCreateNonPersistentDataStore`), already wired in `EngineContext` |
| Privacy | Content blockers, permissions, HTTPS upgrade, privacy report | Planned | M6 |
| Privacy | HTTPS-first for typed hosts | Partial | Typed bare hosts get `https://` (unit tested). Upgrading `http://` links isn't done yet |
| Data | Passwords / AutoFill (DPAPI) | Planned | M6 |
| Data | Downloads manager | Planned | M6 |
| Dev | Web Inspector | Planned | Uses WebKit's own inspector (`WKInspectorShow`); wired to F12, not checked yet |
| System | Installer, default-browser registration | Planned | M8 |
| Apple-only | iCloud Keychain / iCloud Tabs / Handoff / Continuity | Not possible on Windows | Proprietary Apple services with no Windows API |
| Apple-only | Apple Pay | Not possible on Windows | Needs Apple's platform PassKit / Secure Enclave |
| Apple-only | FairPlay DRM (Apple TV+, some streaming) | Not possible on Windows | Proprietary to Apple's platforms; WinCairo has no FairPlay CDM |
| Apple-only | Safari Web Extensions | Not possible on Windows | WebKit's extension runtime is Cocoa-only |
| Apple-only | SF Symbols / San Francisco font | Not possible on Windows | Licensed for Apple platforms only. Segoe UI Variable and original glyphs are used instead |
