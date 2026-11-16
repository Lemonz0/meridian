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
| Chrome | Compact toolbar, back/forward | Implemented | Back and forward checked through `WM_APPCOMMAND` (automation). Mouse clicks and keyboard checked by the user on 2026-11-16 |
| Chrome | Caption buttons (min/max/close), windowed and maximized | Implemented | User found that clicks failed when maximized. Fixed, and `tools/probe-caption.ps1` 7/7 checks all buttons plus the screen corner |
| Chrome | Smart Search field: host when idle, full URL on focus | Partial | Idle host display checked (screenshot). Typing plus Enter checked. The visual switch to the full URL on click isn't checked yet |
| Chrome | Lock icon for HTTPS, progress fill in field | Partial | Lock checked (screenshot). Progress fill not captured mid-load yet |
| Chrome | Custom frame, follows dark mode, rounded corners | Partial | Dark mode checked, both windowed and maximized. Light mode and live theme switching not checked. Caption buttons are on the right, following the Windows convention |
| Chrome | Mica backdrop | Partial | Turned on through DWM. In dark mode it looks nearly flat; the effect isn't measured yet |
| Security | Certificate errors block the load | Implemented | expired, wrong host, self-signed and untrusted root (badssl.com) all blocked, with Windows' reason shown in a banner. A full interstitial page is planned for M6 |
| Security | Valid sites whose root isn't installed locally | Partial | Found by reproducing a real failure (example.com). Fixed by verifying with Windows and retrying with full verification. The retry path couldn't be re-run after the fix because the root was already installed by then; it needs a clean machine or VM |
| Chrome | Liquid Glass (refraction, rim light, morphing) | Planned | See GLASS_NOTES.md |
| Chrome | Known artifact | — | The top 1–2 px of the window can show the window behind it. It's the DWM resize-border band, which gets no Mica. Not fixed yet |
| Tabs | Multiple tabs, tab bar (shown when 2+ tabs, as Safari does by default) | Implemented | `tools/smoke-m3.ps1` 23/23 |
| Tabs | Favicons in tabs | Partial | PNG/ICO/JPEG/GIF/WebP through WIC, fetched with WinHTTP with limits (no cookies, 512 KB, 5 s, http/https/data only). **SVG favicons aren't supported yet**, and some sites only offer SVG, so they show a globe |
| Tabs | Titles, close on hover, middle-click close | Partial | Close checked through automation. Hover and middle-click aren't automated |
| Tabs | Pinned tabs | Implemented | Pin, unpin, pinned-first ordering, and survival across restart all checked |
| Tabs | Drag to reorder | Partial | The reorder logic is checked through automation. The mouse-drag gesture isn't automated. There's no reorder animation yet (spring motion is planned for the glass/motion work) |
| Tabs | Drag out to a new window, "Move Tab to New Window" | Implemented | Checked: the page moves with its live web view (`WKViewSetParentWindow`), with no reload |
| Tabs | Tab overview grid with thumbnails | Partial | Grid, scrolling, keyboard focus, click to switch and close all work. Thumbnails come from Windows.Graphics.Capture, so a tab only has one after it has been visible; others show a favicon placeholder. Thumbnails aren't saved across restarts. There's no zoom-in/out animation yet |
| Tabs | Session restore (windows, tabs, pinned, back/forward history) | Implemented | Checked across a full quit and relaunch, including Back after restart. Restored background tabs stay unloaded until selected, as in Safari |
| Tabs | Reopen closed tab (Ctrl+Shift+T), duplicate, close others/right | Implemented | Reopen and duplicate checked; close-others/right are menu paths |
| Tabs | Ctrl+click / middle-click opens link in new tab | Partial | Implemented by tracking input in the message loop, because WebKit's C API doesn't expose modifiers on navigation actions. Not automated yet |
| Tabs | `target=_blank` / `window.open` | Partial | Opens as a new tab next to the opener (`createNewPage`). Sized popup windows also open as tabs for now |
| Tabs | Compact tab layout (tabs inside the toolbar) | Planned | Safari's alternative layout; currently only the separate tab bar exists |
| Tabs | Tab Groups | Planned | M5 |
| Search | Smart Search dropdown: Top Hit, history and bookmark matches ranked by frecency | Partial | M4 code written in a cloud session; **not built or run yet**. Ranking and matching are unit tested (MinGW/Wine). Plain substring/word matching; non-ASCII matching is case-sensitive (SQLite `LIKE` folds ASCII only) |
| Search | Inline autocomplete of host, then path after a `/` | Partial | Unit tested; UI not run yet. Suppressed after Backspace/Delete and when the caret isn't at the end. IME composition with completion not checked |
| Search | Keyboard: ↑/↓, Enter, Esc (twice), Shift+Delete removes a history row | Partial | Written and covered by `tools/smoke-m4.ps1` (through automation commands, not real key presses); not run yet |
| Search | Search-engine suggestions | Partial | **Opt-in, off by default.** Toggle from the start page's context menu until Settings exists (M7). HTTPS only, no cookies, 64 KB / 3 s caps, stale answers dropped, never for private contexts. Uses DuckDuckGo's `ac` endpoint; that endpoint's format isn't verified against the live service yet (the parser accepts both array and object forms) |
| Search | Start page for new tabs: Favorites, Frequently Visited | Partial | Native, local data only. Folders inside Favorites aren't shown yet. Reading List section arrives with M5. Not run yet |
| Library | History: recorded on main-frame commit, typed counts, titles, site icons | Partial | Restored-session pages aren't counted as new visits. Every commit counts, reloads included. Private windows (M6) won't record: the check is in place, but all windows are non-private today. Not run yet |
| Library | History view (Ctrl+H): grouped by day, search, open, delete, clear | Partial | Native view in the content area. Safari uses ⌘Y; Ctrl+Y is Redo on Windows, so Ctrl+H. Clearing history also clears saved site icons. Not run yet |
| Library | Bookmarks: folder tree, Favorites folder, Ctrl+D | Partial | Tree, ordering, moves are unit tested. Ctrl+D adds straight to Favorites with a banner; Safari's add sheet (choose folder/title) isn't built. No bookmarks editor or Favorites bar yet |
| Library | Bookmark import/export (Netscape HTML) | Partial | Unit tested, including a foreign file shape, `javascript:` links dropped, nesting capped at 32 levels, 32 MB file cap. Context menu on the start page. Not run yet |
| Library | Sidebar, Reading List, Reader mode, Tab Groups | Planned | M5 |
| Privacy | Private windows (non-persistent data store) | Planned | Engine support exists (`WKWebsiteDataStoreCreateNonPersistentDataStore`), already wired in `EngineContext` |
| Privacy | Content blockers, permissions, HTTPS upgrade, privacy report | Planned | M6 |
| Privacy | HTTPS-first for typed hosts | Partial | Typed bare hosts get `https://` (unit tested). Upgrading `http://` links isn't done yet |
| Data | Passwords / AutoFill (DPAPI) | Planned | M6 |
| Data | Downloads manager | Planned | M6 |
| Dev | Web Inspector | Partial | Uses WebKit's own inspector (`WKInspectorShow`) on F12. The user said "everything else works" on 2026-11-16, but whether the inspector specifically was tried isn't confirmed |
| System | Installer, default-browser registration | Planned | M8 |
| Apple-only | iCloud Keychain / iCloud Tabs / Handoff / Continuity | Not possible on Windows | Proprietary Apple services with no Windows API |
| Apple-only | Apple Pay | Not possible on Windows | Needs Apple's platform PassKit / Secure Enclave |
| Apple-only | FairPlay DRM (Apple TV+, some streaming) | Not possible on Windows | Proprietary to Apple's platforms; WinCairo has no FairPlay CDM |
| Apple-only | Safari Web Extensions | Not possible on Windows | WebKit's extension runtime is Cocoa-only |
| Apple-only | SF Symbols / San Francisco font | Not possible on Windows | Licensed for Apple platforms only. Segoe UI Variable and original glyphs are used instead |
