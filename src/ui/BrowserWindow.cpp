#include "BrowserWindow.h"

#include "app/App.h"
#include "config/Brand.h"
#include "platform/Log.h"
#include "platform/WindowCapture.h"
#include "services/Text.h"
#include "services/UrlFixup.h"

#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <cwctype>
#include <dwmapi.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <windowsx.h>

namespace meridian::ui {

namespace {
constexpr UINT_PTR kBannerTimer = 1;
constexpr UINT kAutomationMessage = WM_APP + 20;
constexpr ULONGLONG kThumbnailMaxAgeMs = 3000;
constexpr int kThumbnailMaxWidthPx = 720;

constexpr size_t kMaxFavoritesShown = 24;
constexpr size_t kMaxFrequentShown = 12;
constexpr size_t kMaxHistoryShown = 1000;
constexpr size_t kMaxSearchSuggestions = 4;
constexpr uint64_t kMaxImportBytes = 32ull * 1024 * 1024;

enum MenuId : UINT {
    kMenuPin = 100, kMenuDuplicate, kMenuMoveToWindow, kMenuReload, kMenuClose, kMenuCloseOthers, kMenuCloseRight,
    kMenuOpen, kMenuOpenNewTab, kMenuRemoveFavorite, kMenuRemoveHistory, kMenuImport, kMenuExport,
    kMenuSearchSuggestions, kMenuClearHistory,
};

std::wstring hostOf(const std::wstring& url)
{
    auto h = services::displayHost(url);
    return h == url ? std::wstring() : h;
}

std::wstring trimmed(const std::wstring& s)
{
    size_t b = 0, e = s.size();
    while (b < e && iswspace(s[b])) ++b;
    while (e > b && iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::wstring windowText(HWND h)
{
    int len = GetWindowTextLengthW(h);
    std::wstring text(size_t(len), L'\0');
    if (len)
        GetWindowTextW(h, text.data(), len + 1);
    return text;
}

// What the address field shows while a suggestion is selected.
std::wstring fieldTextFor(const services::Suggestion& s)
{
    switch (s.kind) {
    case services::SuggestionKind::Search:
    case services::SuggestionKind::SearchSuggestion:
    case services::SuggestionKind::Navigate:
        return s.title;
    default:
        return s.url;
    }
}

const wchar_t* kindName(services::SuggestionKind k)
{
    switch (k) {
    case services::SuggestionKind::TopHit: return L"tophit";
    case services::SuggestionKind::Navigate: return L"navigate";
    case services::SuggestionKind::Search: return L"search";
    case services::SuggestionKind::History: return L"history";
    case services::SuggestionKind::Bookmark: return L"bookmark";
    case services::SuggestionKind::SearchSuggestion: return L"searchsuggestion";
    }
    return L"?";
}

// Shell file dialog for bookmark files. Empty result: cancelled.
std::wstring chooseBookmarkFile(HWND owner, bool save)
{
    ComPtr<IFileDialog> dialog;
    HRESULT hr = save
        ? CoCreateInstance(__uuidof(FileSaveDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf()))
        : CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf()));
    if (FAILED(hr))
        return {};
    COMDLG_FILTERSPEC types[] = { { L"Bookmarks file (HTML)", L"*.html;*.htm" }, { L"All files", L"*.*" } };
    dialog->SetFileTypes(ARRAYSIZE(types), types);
    dialog->SetDefaultExtension(L"html");
    dialog->SetTitle(save ? L"Export Bookmarks" : L"Import Bookmarks");
    if (save)
        dialog->SetFileName(L"Bookmarks.html");
    if (FAILED(dialog->Show(owner)))
        return {};
    ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    if (FAILED(dialog->GetResult(item.GetAddressOf())) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
        return {};
    std::wstring result = path;
    CoTaskMemFree(path);
    return result;
}

bool readFileBytes(const std::wstring& path, std::string& out)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size {};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart >= 0 && uint64_t(size.QuadPart) <= kMaxImportBytes;
    if (ok) {
        out.resize(size_t(size.QuadPart));
        DWORD read = 0;
        ok = out.empty() || (ReadFile(f, out.data(), DWORD(out.size()), &read, nullptr) && read == out.size());
    }
    CloseHandle(f);
    return ok;
}

bool writeFileBytes(const std::wstring& path, const std::string& data)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    bool ok = WriteFile(f, data.data(), DWORD(data.size()), &written, nullptr) && written == data.size();
    CloseHandle(f);
    return ok;
}
}

// ---- creation ---------------------------------------------------------------

BrowserWindow::BrowserWindow(app::App& app)
    : m_app(app)
{
}

BrowserWindow* BrowserWindow::create(app::App& app, const services::SessionWindow* restore, const RECT* bounds, bool addDefaultTab)
{
    static bool registered = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc { sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = wndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
        wc.lpszClassName = brand::kWindowClass;
        RegisterClassExW(&wc);
        registered = true;
    }

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 1280, h = 860;
    RECT r {};
    if (restore && restore->right > restore->left)
        r = { restore->left, restore->top, restore->right, restore->bottom };
    else if (bounds)
        r = *bounds;
    if (r.right > r.left) {
        // Keep the window on a monitor that still exists.
        HMONITOR mon = MonitorFromRect(&r, MONITOR_DEFAULTTONULL);
        if (mon) {
            x = r.left;
            y = r.top;
            w = r.right - r.left;
            h = r.bottom - r.top;
        }
    }

    auto* self = new BrowserWindow(app);
    HWND hwnd = CreateWindowExW(0, brand::kWindowClass, brand::kProductName, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, w, h, nullptr, nullptr, inst, self);
    if (!hwnd) {
        delete self;
        return nullptr;
    }

    if (restore) {
        for (auto& st : restore->tabs) {
            auto tab = std::make_unique<Tab>();
            tab->id = app.nextTabId();
            tab->url = st.url;
            tab->title = st.title;
            tab->pinned = st.pinned;
            tab->pendingState = st.state;
            tab->startPage = st.url.empty() || st.url == L"about:blank";
            self->m_tabs.push_back(std::move(tab));
        }
        // Pinned tabs always lead.
        std::stable_partition(self->m_tabs.begin(), self->m_tabs.end(), [](auto& t) { return t->pinned; });
        if (!self->m_tabs.empty())
            self->activate(std::clamp(restore->selected, 0, int(self->m_tabs.size()) - 1));
    } else if (addDefaultTab)
        self->openTab(brand::kDefaultHomeURL, true);

    ShowWindow(hwnd, restore && restore->maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    return self;
}

LRESULT CALLBACK BrowserWindow::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    BrowserWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<BrowserWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else
        self = reinterpret_cast<BrowserWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (!self)
        return DefWindowProcW(hwnd, msg, wp, lp);

    LRESULT result = self->handle(msg, wp, lp);
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete self;
    }
    return result;
}

void BrowserWindow::onCreate()
{
    m_dpi = GetDpiForWindow(m_hwnd);
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2d.GetAddressOf());
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3), reinterpret_cast<IUnknown**>(m_dwrite.GetAddressOf()));

    // WS_EX_LAYERED + full alpha: the EDIT sits in the DWM-extended frame, where
    // GDI's zero alpha would otherwise let the backdrop bleed through the field.
    m_address = CreateWindowExW(WS_EX_LAYERED, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | ES_LEFT, 0, 0, 0, 0, m_hwnd,
        nullptr, GetModuleHandleW(nullptr), nullptr);
    SetLayeredWindowAttributes(m_address, 0, 255, LWA_ALPHA);
    SetWindowSubclass(m_address, addressProc, 1, reinterpret_cast<DWORD_PTR>(this));

    m_historySearch = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | ES_LEFT, 0, 0, 0, 0, m_hwnd,
        nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(m_historySearch, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search History"));

    SuggestionPopup::Callbacks callbacks;
    callbacks.activate = [this](int index) { activateSuggestion(index); };
    callbacks.icon = [this](const std::wstring& host) -> const services::Bitmap& { return m_app.siteIcon(host); };
    m_popup.create(m_hwnd, std::move(callbacks));
    updateTheme();
}

engine::EngineContext& BrowserWindow::context()
{
    return m_app.engine();
}

// ---- tabs -------------------------------------------------------------------

Tab* BrowserWindow::findTab(uint64_t id, int* index)
{
    for (int i = 0; i < int(m_tabs.size()); ++i)
        if (m_tabs[i]->id == id) {
            if (index)
                *index = i;
            return m_tabs[i].get();
        }
    return nullptr;
}

Tab* BrowserWindow::openTab(const std::wstring& url, bool activateTab, int index)
{
    auto tab = std::make_unique<Tab>();
    tab->id = m_app.nextTabId();
    tab->url = url;
    tab->startPage = url.empty() || url == L"about:blank";
    Tab* raw = tab.get();
    if (index < 0 || index > int(m_tabs.size()))
        index = int(m_tabs.size());
    attachTab(std::move(tab), index, activateTab);
    if (!activateTab) {
        // Background tabs load immediately (Ctrl+click); restored tabs stay lazy.
        ensureView(*raw);
    }
    return raw;
}

void BrowserWindow::attachTab(std::unique_ptr<Tab> tab, int index, bool activateTab)
{
    // Keep pinned tabs ahead of normal tabs.
    int firstNormal = int(std::find_if(m_tabs.begin(), m_tabs.end(), [](auto& t) { return !t->pinned; }) - m_tabs.begin());
    index = tab->pinned ? std::min(index, firstNormal) : std::max(index, firstNormal);
    index = std::clamp(index, 0, int(m_tabs.size()));
    Tab* raw = tab.get();
    if (raw->view) {
        raw->view->setParentWindow(m_hwnd);
        raw->view->setVisible(false);
        wire(*raw);
    }
    raw->faviconBitmap.Reset();
    raw->thumbnailBitmap.Reset();
    m_tabs.insert(m_tabs.begin() + index, std::move(tab));
    if (m_active >= index)
        ++m_active;
    if (activateTab || m_active < 0)
        activate(index);
    tabsChanged();
}

void BrowserWindow::ensureView(Tab& tab)
{
    if (tab.view)
        return;
    tab.view = std::make_unique<engine::WebView>(m_app.engine(), m_hwnd);
    tab.view->setVisible(false);
    wire(tab);
    bool restored = !tab.pendingState.empty() && tab.view->restoreSessionState(tab.pendingState);
    tab.pendingState.clear();
    tab.skipNextVisit = restored; // showing a restored page again is not a new visit
    if (!restored && !tab.url.empty())
        tab.view->loadURL(tab.url); // fresh tab, or session data from another WebKit build
}

void BrowserWindow::wire(Tab& tab)
{
    uint64_t id = tab.id;
    engine::WebViewCallbacks cb;
    cb.titleChanged = [this, id](const std::wstring& t) {
        int i;
        if (Tab* tab = findTab(id, &i)) {
            tab->title = t;
            m_app.updateHistoryTitle(context(), tab->url, t);
            if (i == m_active)
                updateWindowTitle();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            m_app.sessionChanged();
        }
    };
    cb.urlChanged = [this, id](const std::wstring& u) {
        int i;
        if (Tab* tab = findTab(id, &i)) {
            if (hostOf(u) != tab->faviconHost) {
                tab->favicon = {};
                tab->faviconBitmap.Reset();
            }
            tab->url = u;
            if (tab->startPage && !u.empty() && u != L"about:blank") {
                tab->startPage = false; // the page navigated itself (e.g. a script); show it
                if (i == m_active)
                    updateContentView();
            }
            if (i == m_active && !m_editing)
                setAddressText(u);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            m_app.sessionChanged();
        }
    };
    cb.progressChanged = [this, id](double p) {
        if (Tab* tab = findTab(id)) {
            tab->progress = p;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    };
    cb.loadingChanged = [this, id](bool loading) {
        if (Tab* tab = findTab(id)) {
            tab->loading = loading;
            if (!loading)
                tab->progress = 0;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    };
    cb.backForwardChanged = [this] { InvalidateRect(m_hwnd, nullptr, FALSE); };
    cb.loadFailed = [this, id](const std::wstring& url, const std::wstring& desc) {
        if (Tab* tab = findTab(id); tab && !desc.empty())
            showBanner(*tab, L"Couldn’t open " + services::displayHost(url) + L": " + desc);
    };
    cb.webProcessCrashed = [this, id] {
        if (Tab* tab = findTab(id)) {
            showBanner(*tab, L"This webpage was reloaded because a problem occurred.");
            tab->view->reload();
        }
    };
    cb.navigationCommitted = [this, id](const std::wstring& u) {
        Tab* tab = findTab(id);
        if (!tab || !services::isRecordableURL(u))
            return; // about:blank and friends don't consume the typed flag
        bool typed = tab->typedPending;
        tab->typedPending = false;
        if (tab->skipNextVisit) {
            tab->skipNextVisit = false;
            return;
        }
        m_app.recordVisit(context(), u, typed);
    };
    cb.faviconURLChanged = [this, id](const std::wstring& iconURL) {
        Tab* tab = findTab(id);
        if (!tab)
            return;
        std::wstring host = hostOf(tab->url);
        m_app.favicons().load(iconURL, 32, [this, id, host, hwnd = m_hwnd](const services::Bitmap& bmp) {
            if (!IsWindow(hwnd))
                return;
            Tab* t = findTab(id);
            if (!t || hostOf(t->url) != host)
                return;
            t->favicon = bmp;
            t->faviconHost = host;
            t->faviconBitmap.Reset();
            if (bmp) {
                m_app.storeIcon(context(), host, bmp);
                m_siteIcons.erase(host);
            }
            InvalidateRect(m_hwnd, nullptr, FALSE);
        });
    };
    cb.createNewPage = [this, id](WKPageConfigurationRef conf) -> engine::WebView* {
        int opener = -1;
        findTab(id, &opener);
        auto tab = std::make_unique<Tab>();
        tab->id = m_app.nextTabId();
        tab->view = std::make_unique<engine::WebView>(conf, m_hwnd);
        engine::WebView* view = tab->view.get();
        attachTab(std::move(tab), opener + 1, true);
        return view;
    };
    cb.openInNewTab = [this, id](const std::wstring& url, bool foreground) {
        int opener = -1;
        findTab(id, &opener);
        openTab(url, foreground, opener + 1);
    };
    cb.closeRequested = [this, id] {
        int i;
        if (findTab(id, &i))
            closeTab(i);
    };
    tab.view->setCallbacks(std::move(cb));
}

void BrowserWindow::captureThumbnail(Tab& tab, bool force)
{
    if (!tab.view || !platform::windowCaptureSupported() || IsIconic(m_hwnd) || !IsWindowVisible(tab.view->hwnd())) {
        platform::log(L"thumbnail skipped: view=%d supported=%d iconic=%d visible=%d", bool(tab.view),
            platform::windowCaptureSupported(), IsIconic(m_hwnd), tab.view && IsWindowVisible(tab.view->hwnd()));
        return;
    }
    ULONGLONG now = GetTickCount64();
    if (!force && tab.thumbnailURL == tab.url && now - tab.thumbnailTick < kThumbnailMaxAgeMs)
        return;
    if (auto bmp = platform::captureClientRect(m_hwnd, contentRectPx(), kThumbnailMaxWidthPx)) {
        // A WebKit view that has not presented its first frame yet is pure
        // black; such a capture is worse than the favicon placeholder.
        size_t black = 0, pixels = bmp->bgra.size() / 4;
        for (size_t i = 0; i < bmp->bgra.size(); i += 4)
            black += (bmp->bgra[i] | bmp->bgra[i + 1] | bmp->bgra[i + 2]) < 8;
        if (pixels && black * 100 / pixels >= 97) {
            platform::log(L"thumbnail discarded: %zu%% black (page not painted yet)", black * 100 / pixels);
            return;
        }
        tab.thumbnail = std::move(*bmp);
        tab.thumbnailURL = tab.url;
        tab.thumbnailTick = now;
        tab.thumbnailBitmap.Reset();
    }
}

void BrowserWindow::activate(int index)
{
    if (index < 0 || index >= int(m_tabs.size()))
        return;
    if (index == m_active && m_tabs[index]->view && IsWindowVisible(m_tabs[index]->view->hwnd()))
        return;
    endEditingAddress(true);
    if (m_historyOpen)
        setHistory(false);
    if (Tab* old = active(); old && old->view && index != m_active) {
        captureThumbnail(*old, false);
        old->view->setVisible(false);
    }
    m_active = index;
    Tab& tab = *m_tabs[index];
    ensureView(tab);
    if (tab.startPage)
        refreshStartPage();
    layout();
    updateContentView();
    if (!m_overviewOpen)
        focusContent();
    syncAddressFromTab();
    updateWindowTitle();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    m_app.sessionChanged();
}

void BrowserWindow::closeTab(int index)
{
    if (index < 0 || index >= int(m_tabs.size()))
        return;
    auto& tab = *m_tabs[index];
    app::ClosedTab closed { tab.url, tab.title, tab.view ? tab.view->sessionState() : tab.pendingState, tab.pinned };
    m_app.pushClosedTab(std::move(closed));

    if (m_tabs.size() == 1) {
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0); // Safari closes the window with its last tab
        return;
    }
    bool wasActive = index == m_active;
    auto dying = std::move(m_tabs[index]);
    m_tabs.erase(m_tabs.begin() + index);
    dying.reset(); // destroyed after the tab list is consistent again
    if (m_drag.index == index)
        m_drag = {};
    if (wasActive) {
        m_active = -1;
        activate(std::min(index, int(m_tabs.size()) - 1)); // the tab to the right takes its place
    } else if (index < m_active)
        --m_active;
    tabsChanged();
}

std::unique_ptr<Tab> BrowserWindow::detachTab(int index)
{
    auto tab = std::move(m_tabs[index]);
    m_tabs.erase(m_tabs.begin() + index);
    if (tab->view) {
        captureThumbnail(*tab, false);
        tab->view->setVisible(false);
    }
    if (index == m_active) {
        m_active = -1;
        if (!m_tabs.empty())
            activate(std::min(index, int(m_tabs.size()) - 1));
    } else if (index < m_active)
        --m_active;
    tabsChanged();
    return tab;
}

void BrowserWindow::moveTabToNewWindow(int index, POINT screen)
{
    if (m_tabs.size() < 2)
        return;
    RECT current;
    GetWindowRect(m_hwnd, &current);
    int w = current.right - current.left, h = current.bottom - current.top;
    RECT bounds { screen.x - w / 4, screen.y - px(20), screen.x - w / 4 + w, screen.y - px(20) + h };
    auto tab = detachTab(index);
    if (auto* window = m_app.newWindow(nullptr, &bounds, false))
        window->attachTab(std::move(tab), 0, true);
}

void BrowserWindow::togglePin(int index)
{
    auto tab = std::move(m_tabs[index]);
    bool wasActive = index == m_active;
    m_tabs.erase(m_tabs.begin() + index);
    if (index < m_active)
        --m_active;
    tab->pinned = !tab->pinned;
    int firstNormal = int(std::find_if(m_tabs.begin(), m_tabs.end(), [](auto& t) { return !t->pinned; }) - m_tabs.begin());
    int dest = firstNormal; // newly pinned -> end of pinned; unpinned -> start of normal
    m_tabs.insert(m_tabs.begin() + dest, std::move(tab));
    if (wasActive)
        m_active = dest;
    else if (dest <= m_active)
        ++m_active;
    tabsChanged();
}

void BrowserWindow::duplicateTab(int index)
{
    Tab& src = *m_tabs[index];
    auto tab = std::make_unique<Tab>();
    tab->id = m_app.nextTabId();
    tab->url = src.url;
    tab->title = src.title;
    tab->favicon = src.favicon;
    tab->faviconHost = src.faviconHost;
    tab->pendingState = src.view ? src.view->sessionState() : src.pendingState;
    attachTab(std::move(tab), index + 1, true);
}

void BrowserWindow::reopenClosedTab()
{
    auto closed = m_app.popClosedTab();
    if (!closed)
        return;
    auto tab = std::make_unique<Tab>();
    tab->id = m_app.nextTabId();
    tab->url = closed->url;
    tab->title = closed->title;
    tab->pinned = closed->pinned;
    tab->pendingState = std::move(closed->state);
    attachTab(std::move(tab), m_active + 1, true);
}

void BrowserWindow::showTabMenu(int index, POINT screen)
{
    const Tab& tab = *m_tabs[index];
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuReload, L"Reload Tab");
    AppendMenuW(menu, MF_STRING, kMenuDuplicate, L"Duplicate Tab");
    AppendMenuW(menu, MF_STRING, kMenuPin, tab.pinned ? L"Unpin Tab" : L"Pin Tab");
    AppendMenuW(menu, MF_STRING | (m_tabs.size() < 2 ? MF_GRAYED : 0), kMenuMoveToWindow, L"Move Tab to New Window");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuClose, L"Close Tab\tCtrl+W");
    AppendMenuW(menu, MF_STRING | (m_tabs.size() < 2 ? MF_GRAYED : 0), kMenuCloseOthers, L"Close Other Tabs");
    AppendMenuW(menu, MF_STRING | (index + 1 >= int(m_tabs.size()) ? MF_GRAYED : 0), kMenuCloseRight, L"Close Tabs to the Right");
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);

    uint64_t id = tab.id;
    switch (cmd) {
    case kMenuReload:
        ensureView(*m_tabs[index]);
        m_tabs[index]->view->reload();
        break;
    case kMenuDuplicate: duplicateTab(index); break;
    case kMenuPin: togglePin(index); break;
    case kMenuMoveToWindow: moveTabToNewWindow(index, screen); break;
    case kMenuClose: closeTab(index); break;
    case kMenuCloseOthers:
        for (int i = int(m_tabs.size()) - 1; i >= 0; --i)
            if (m_tabs[i]->id != id && !m_tabs[i]->pinned)
                closeTab(i);
        break;
    case kMenuCloseRight:
        for (int i = int(m_tabs.size()) - 1; i > index; --i)
            if (!m_tabs[i]->pinned)
                closeTab(i);
        break;
    }
}

void BrowserWindow::tabsChanged()
{
    layout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    m_app.sessionChanged();
}

services::SessionWindow BrowserWindow::sessionSnapshot() const
{
    services::SessionWindow w;
    WINDOWPLACEMENT wp { sizeof(wp) };
    GetWindowPlacement(m_hwnd, &wp);
    w.left = wp.rcNormalPosition.left;
    w.top = wp.rcNormalPosition.top;
    w.right = wp.rcNormalPosition.right;
    w.bottom = wp.rcNormalPosition.bottom;
    w.maximized = wp.showCmd == SW_SHOWMAXIMIZED;
    w.selected = std::max(0, m_active);
    for (auto& t : m_tabs)
        w.tabs.push_back({ t->url, t->title, t->pinned, t->view ? t->view->sessionState() : t->pendingState });
    return w;
}

void BrowserWindow::automate(const std::wstring& command)
{
    auto space = command.find(L' ');
    std::wstring verb = command.substr(0, space);
    std::wstring arg = space == std::wstring::npos ? L"" : command.substr(space + 1);
    int n = int(m_tabs.size());
    auto index = [&](const std::wstring& s) { return std::clamp(_wtoi(s.c_str()), 0, std::max(0, n - 1)); };

    if (verb == L"open") openTab(arg, true, n);
    else if (verb == L"openbg") openTab(arg, false, m_active + 1);
    else if (verb == L"nav" && active()) {
        SetWindowTextW(m_address, arg.c_str());
        navigateFromAddressBar();
    }
    else if (verb == L"activate") activate(index(arg));
    else if (verb == L"close") closeTab(index(arg));
    else if (verb == L"pin") togglePin(index(arg));
    else if (verb == L"dup") duplicateTab(index(arg));
    else if (verb == L"reopen") reopenClosedTab();
    else if (verb == L"overview") setOverview(arg == L"1");
    else if (verb == L"newwindow") m_app.newWindow(nullptr, nullptr, true);
    else if (verb == L"closewindow") PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    else if (verb == L"move") {
        auto sp = arg.find(L' ');
        int from = index(arg.substr(0, sp)), to = index(arg.substr(sp + 1));
        if (from != to && m_tabs[from]->pinned == m_tabs[to]->pinned) {
            auto moving = std::move(m_tabs[from]);
            m_tabs.erase(m_tabs.begin() + from);
            m_tabs.insert(m_tabs.begin() + to, std::move(moving));
            if (m_active == from)
                m_active = to;
            else if (from < m_active && to >= m_active)
                --m_active;
            else if (from > m_active && to <= m_active)
                ++m_active;
            tabsChanged();
        }
    }
    else if (verb == L"tearoff") {
        RECT r;
        GetWindowRect(m_hwnd, &r);
        moveTabToNewWindow(index(arg), { r.left + 200, r.top + 120 });
    }
    else if (verb == L"thumb" && active()) captureThumbnail(*active(), true);
    else if (verb == L"dump") m_app.dumpState(arg);
    // M4: Smart Search, start page, history, bookmarks.
    else if (verb == L"type") {
        // Replace the field's text as if typed (caret at the end), then react like EN_CHANGE.
        beginEditingAddress(true);
        setAddressText(arg);
        onAddressEdited();
    }
    else if (verb == L"key" && m_editing) {
        if (arg == L"down") handleAddressKey(VK_DOWN, false);
        else if (arg == L"up") handleAddressKey(VK_UP, false);
        else if (arg == L"enter") handleAddressKey(VK_RETURN, false);
        else if (arg == L"esc") handleAddressKey(VK_ESCAPE, false);
        else if (arg == L"shiftdel") handleAddressKey(VK_DELETE, true);
        else if (arg == L"back") {
            // Backspace: removes the selection (e.g. the completion) or one character.
            std::wstring text = addressText();
            DWORD a = 0, b = 0;
            SendMessageW(m_address, EM_GETSEL, reinterpret_cast<WPARAM>(&a), reinterpret_cast<LPARAM>(&b));
            if (a == b && a > 0)
                --a;
            text.erase(std::min<size_t>(a, text.size()), std::min<size_t>(b, text.size()) - std::min<size_t>(a, text.size()));
            m_deleteKey = true;
            setAddressText(text);
            onAddressEdited();
        }
    }
    else if (verb == L"pick") activateSuggestion(_wtoi(arg.c_str()));
    else if (verb == L"dumpui") dumpUi(arg);
    else if (verb == L"history") setHistory(arg == L"1");
    else if (verb == L"historysearch") {
        setHistory(true);
        SetWindowTextW(m_historySearch, arg.c_str()); // EN_CHANGE refreshes
    }
    else if (verb == L"historyopen") openHistoryEntry(_wtoi(arg.c_str()), false);
    else if (verb == L"historydelete") deleteHistoryEntry(_wtoi(arg.c_str()));
    else if (verb == L"bookmark") addBookmark();
    else if (verb == L"importbookmarks" && !arg.empty()) importBookmarks(arg);
    else if (verb == L"exportbookmarks" && !arg.empty()) exportBookmarks(arg);
    else if (verb == L"setting" && arg.rfind(L"suggest ", 0) == 0) m_app.setSearchSuggestionsEnabled(arg.substr(8) == L"1");
    else if (verb == L"starttile" && showingStartPage()) {
        // "starttile f 0" (favorites) / "starttile v 0" (frequently visited)
        StartPage::Hit h { arg.rfind(L"v", 0) == 0 ? StartPage::Section::Frequent : StartPage::Section::Favorites,
            arg.size() > 2 ? _wtoi(arg.c_str() + 2) : 0 };
        if (auto* item = m_startPage.item(h)) {
            std::wstring url = item->url;
            navigate(url, false);
        }
    }
}

void BrowserWindow::dumpUi(const std::wstring& path)
{
    auto line = [](std::wstring& out, const std::wstring& text) { out += text + L"\n"; };
    std::wstring out;
    DWORD a = 0, b = 0;
    SendMessageW(m_address, EM_GETSEL, reinterpret_cast<WPARAM>(&a), reinterpret_cast<LPARAM>(&b));
    std::wstring text = addressText();
    line(out, L"editing=" + std::to_wstring(m_editing) + L" sel=" + std::to_wstring(a) + L"," + std::to_wstring(b));
    line(out, L"text=" + text);
    line(out, L"typed=" + m_typed);
    line(out, L"popup visible=" + std::to_wstring(m_popup.visible()) + L" selected=" + std::to_wstring(m_popup.selected())
        + L" count=" + std::to_wstring(m_popup.items().size()));
    for (size_t i = 0; i < m_popup.items().size(); ++i) {
        auto& it = m_popup.items()[i];
        line(out, L"item " + std::to_wstring(i) + L" kind=" + kindName(it.kind) + L" history=" + std::to_wstring(it.historyId != 0)
            + L" bookmarked=" + std::to_wstring(it.bookmarked) + L" url=" + it.url + L" title=" + it.title);
    }
    line(out, L"startpage visible=" + std::to_wstring(showingStartPage()));
    for (auto section : { StartPage::Section::Favorites, StartPage::Section::Frequent }) {
        auto& items = m_startPage.items(section);
        for (size_t i = 0; i < items.size(); ++i)
            line(out, std::wstring(section == StartPage::Section::Favorites ? L"fav " : L"freq ") + std::to_wstring(i)
                + L" url=" + items[i].url + L" title=" + items[i].title);
    }
    line(out, L"historyview open=" + std::to_wstring(m_historyOpen) + L" count=" + std::to_wstring(m_historyView.count())
        + L" selected=" + std::to_wstring(m_historySelected));
    for (int i = 0; i < m_historyView.count(); ++i) {
        auto* e = m_historyView.entry(i);
        line(out, L"hist " + std::to_wstring(i) + L" visits=" + std::to_wstring(e->visitCount) + L" typed="
            + std::to_wstring(e->typedCount) + L" url=" + e->url + L" title=" + e->title);
    }
    app::App::writeTextFile(path, out);
}

// ---- content views: start page, history ------------------------------------

bool BrowserWindow::showingStartPage() const
{
    const Tab* t = m_active >= 0 && m_active < int(m_tabs.size()) ? m_tabs[m_active].get() : nullptr;
    return t && t->startPage && !m_overviewOpen && !m_historyOpen;
}

void BrowserWindow::updateContentView()
{
    Tab* t = active();
    if (t && t->view)
        t->view->setVisible(!m_overviewOpen && !m_historyOpen && !t->startPage);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void BrowserWindow::focusContent()
{
    if (m_editing)
        return;
    if (m_historyOpen) {
        SetFocus(m_historySearch);
        return;
    }
    Tab* t = active();
    if (t && t->view && IsWindowVisible(t->view->hwnd()))
        SetFocus(t->view->hwnd());
    else
        SetFocus(m_hwnd);
}

void BrowserWindow::refreshStartPage()
{
    std::vector<StartPage::Item> favorites, frequent;
    try {
        if (auto* bookmarks = m_app.bookmarks())
            for (auto& b : bookmarks->children(services::Bookmarks::kFavoritesId)) {
                if (b.folder)
                    continue; // folders inside Favorites aren't shown on the start page yet
                favorites.push_back({ b.id, 0, b.title, b.url, services::displayHost(b.url) });
                if (favorites.size() >= kMaxFavoritesShown)
                    break;
            }
        if (auto* history = m_app.history())
            for (auto& e : history->frequentlyVisited(kMaxFrequentShown + favorites.size())) {
                bool isFavorite = std::any_of(favorites.begin(), favorites.end(), [&](auto& f) { return f.url == e.url; });
                if (isFavorite)
                    continue;
                frequent.push_back({ 0, e.id, e.title, e.url, services::displayHost(e.url) });
                if (frequent.size() >= kMaxFrequentShown)
                    break;
            }
    } catch (const std::exception& e) {
        platform::log(L"start page data FAILED: %hs", e.what());
    }
    m_startPage.setContent(std::move(favorites), std::move(frequent));
    m_startPage.layout(contentRectDip());
    m_startHover = {};
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void BrowserWindow::showStartPageMenu(POINT screen, StartPage::Hit hit)
{
    const StartPage::Item* item = m_startPage.item(hit);
    StartPage::Item target = item ? *item : StartPage::Item {};
    HMENU menu = CreatePopupMenu();
    if (item) {
        AppendMenuW(menu, MF_STRING, kMenuOpenNewTab, L"Open in New Tab");
        if (hit.section == StartPage::Section::Favorites)
            AppendMenuW(menu, MF_STRING, kMenuRemoveFavorite, L"Remove from Favorites");
        else
            AppendMenuW(menu, MF_STRING, kMenuRemoveHistory, L"Remove from History");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    UINT dataFlags = m_app.bookmarks() ? 0 : MF_GRAYED;
    AppendMenuW(menu, MF_STRING | dataFlags, kMenuImport, L"Import Bookmarks…");
    AppendMenuW(menu, MF_STRING | dataFlags, kMenuExport, L"Export Bookmarks…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    // The one place to opt in until the Settings window exists (M7). Says plainly what it does.
    AppendMenuW(menu, MF_STRING | (m_app.settings() ? 0 : MF_GRAYED) | (m_app.searchSuggestionsEnabled() ? MF_CHECKED : 0),
        kMenuSearchSuggestions, L"Search Engine Suggestions (sends what you type to the search engine)");
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);

    try {
        switch (cmd) {
        case kMenuOpenNewTab: openTab(target.url, false, m_active + 1); break;
        case kMenuRemoveFavorite:
            if (m_app.bookmarks())
                m_app.bookmarks()->remove(target.bookmarkId);
            refreshStartPage();
            break;
        case kMenuRemoveHistory:
            if (m_app.history())
                m_app.history()->remove(target.historyId);
            refreshStartPage();
            break;
        case kMenuImport: importBookmarks(); break;
        case kMenuExport: exportBookmarks(); break;
        case kMenuSearchSuggestions: m_app.setSearchSuggestionsEnabled(!m_app.searchSuggestionsEnabled()); break;
        }
    } catch (const std::exception& e) {
        platform::log(L"start page action FAILED: %hs", e.what());
    }
}

void BrowserWindow::setHistory(bool open)
{
    if (open == m_historyOpen)
        return;
    endEditingAddress(true);
    if (open) {
        if (m_overviewOpen)
            setOverview(false);
        m_historyOpen = true;
        m_historySelected = m_historyHover = -1;
        SetWindowTextW(m_historySearch, L""); // EN_CHANGE refreshes (m_historyOpen is set)
        refreshHistory();
        ShowWindow(m_historySearch, SW_SHOW);
    } else {
        m_historyOpen = false;
        ShowWindow(m_historySearch, SW_HIDE);
        if (Tab* t = active(); t && t->startPage)
            refreshStartPage();
    }
    layout();
    updateContentView();
    focusContent();
}

void BrowserWindow::refreshHistory()
{
    std::vector<services::HistoryEntry> entries;
    try {
        if (auto* history = m_app.history())
            entries = history->search(windowText(m_historySearch), kMaxHistoryShown);
    } catch (const std::exception& e) {
        platform::log(L"history query FAILED: %hs", e.what());
    }
    m_historyView.setEntries(std::move(entries), services::unixNow());
    m_historyView.layout(contentRectDip());
    m_historySelected = std::min(m_historySelected, m_historyView.count() - 1);
    m_historyHover = -1;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void BrowserWindow::openHistoryEntry(int index, bool newTab)
{
    auto* e = m_historyView.entry(index);
    if (!e)
        return;
    std::wstring url = e->url;
    if (newTab)
        openTab(url, false, m_active + 1); // stay in History, like Ctrl+click on a link
    else
        navigate(url, false);
}

void BrowserWindow::deleteHistoryEntry(int index)
{
    auto* e = m_historyView.entry(index);
    if (!e || !m_app.history())
        return;
    try {
        m_app.history()->remove(e->id);
    } catch (const std::exception& ex) {
        platform::log(L"history delete FAILED: %hs", ex.what());
    }
    refreshHistory();
    m_historySelected = std::min(index, m_historyView.count() - 1);
}

void BrowserWindow::clearHistory()
{
    if (!m_app.history())
        return;
    if (MessageBoxW(m_hwnd, L"Clear all browsing history? Site icons saved for history are removed too. This can’t be undone.",
            brand::kProductName, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
        return;
    try {
        m_app.history()->clear();
        if (m_app.icons())
            m_app.icons()->clear();
    } catch (const std::exception& e) {
        platform::log(L"history clear FAILED: %hs", e.what());
    }
    m_siteIcons.clear();
    refreshHistory();
}

void BrowserWindow::showHistoryMenu(int index, POINT screen)
{
    HMENU menu = CreatePopupMenu();
    if (index >= 0) {
        m_historySelected = index;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        AppendMenuW(menu, MF_STRING, kMenuOpen, L"Open");
        AppendMenuW(menu, MF_STRING, kMenuOpenNewTab, L"Open in New Tab");
        AppendMenuW(menu, MF_STRING, kMenuRemoveHistory, L"Delete\tDel");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menu, MF_STRING, kMenuClearHistory, L"Clear History…");
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
    switch (cmd) {
    case kMenuOpen: openHistoryEntry(index, false); break;
    case kMenuOpenNewTab: openHistoryEntry(index, true); break;
    case kMenuRemoveHistory: deleteHistoryEntry(index); break;
    case kMenuClearHistory: clearHistory(); break;
    }
}

// ---- bookmarks -----------------------------------------------------------------

void BrowserWindow::addBookmark()
{
    Tab* t = active();
    auto* bookmarks = m_app.bookmarks();
    if (!t || t->startPage || m_historyOpen || m_overviewOpen)
        return;
    if (!bookmarks || !services::isRecordableURL(t->url)) {
        showBanner(*t, L"This page can’t be added to Favorites.");
        return;
    }
    try {
        if (auto existing = bookmarks->findByURL(t->url)) {
            showBanner(*t, existing->parent == services::Bookmarks::kFavoritesId ? L"This page is already in Favorites."
                                                                                 : L"This page is already in Bookmarks.");
            return;
        }
        bookmarks->add(services::Bookmarks::kFavoritesId, t->displayTitle(), t->url);
        showBanner(*t, L"Added “" + t->displayTitle() + L"” to Favorites.");
    } catch (const std::exception& e) {
        platform::log(L"bookmark add FAILED: %hs", e.what());
        showBanner(*t, L"Couldn’t save the bookmark.");
    }
}

void BrowserWindow::importBookmarks(std::wstring path)
{
    auto* bookmarks = m_app.bookmarks();
    Tab* t = active();
    if (!bookmarks || !t)
        return;
    if (path.empty())
        path = chooseBookmarkFile(m_hwnd, false);
    if (path.empty())
        return;
    std::string html;
    if (!readFileBytes(path, html)) {
        showBanner(*t, L"Couldn’t read that file (bookmark files over 32 MB aren’t imported).");
        return;
    }
    try {
        size_t added = bookmarks->importHTML(html);
        showBanner(*t, L"Imported " + std::to_wstring(added) + (added == 1 ? L" bookmark." : L" bookmarks."));
        platform::log(L"bookmarks imported: %zu", added);
    } catch (const std::exception& e) {
        platform::log(L"bookmark import FAILED: %hs", e.what());
        showBanner(*t, L"Couldn’t import bookmarks from that file.");
    }
    if (t->startPage)
        refreshStartPage();
}

void BrowserWindow::exportBookmarks(std::wstring path)
{
    auto* bookmarks = m_app.bookmarks();
    Tab* t = active();
    if (!bookmarks || !t)
        return;
    if (path.empty())
        path = chooseBookmarkFile(m_hwnd, true);
    if (path.empty())
        return;
    try {
        bool ok = writeFileBytes(path, bookmarks->exportHTML());
        showBanner(*t, ok ? L"Bookmarks exported." : L"Couldn’t write the bookmarks file.");
    } catch (const std::exception& e) {
        platform::log(L"bookmark export FAILED: %hs", e.what());
        showBanner(*t, L"Couldn’t export bookmarks.");
    }
}

ID2D1Bitmap* BrowserWindow::siteIconBitmap(const std::wstring& host)
{
    if (host.empty() || !m_rt)
        return nullptr;
    auto it = m_siteIcons.find(host);
    if (it == m_siteIcons.end()) {
        const services::Bitmap& bmp = m_app.siteIcon(host);
        it = m_siteIcons.emplace(host, bmp ? makeBitmap(m_rt.Get(), bmp) : nullptr).first;
    }
    return it->second.Get();
}

// ---- overview ---------------------------------------------------------------

void BrowserWindow::setOverview(bool open)
{
    if (open == m_overviewOpen)
        return;
    endEditingAddress(true);
    if (open) {
        if (m_historyOpen)
            setHistory(false);
        if (Tab* t = active())
            captureThumbnail(*t, true);
        m_overviewOpen = true;
        m_overviewFocus = m_active;
        updateContentView();
        SetFocus(m_hwnd);
    } else {
        m_overviewOpen = false;
        if (Tab* t = active(); t && t->startPage)
            refreshStartPage();
        updateContentView();
        focusContent();
    }
    layout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void BrowserWindow::moveOverviewFocus(int dx, int dy)
{
    int n = int(m_tabs.size());
    if (!n)
        return;
    int f = m_overviewFocus < 0 ? m_active : m_overviewFocus;
    f += dx + dy * m_overview.columns();
    m_overviewFocus = std::clamp(f, 0, n - 1);
    m_overview.scrollToShow(m_overviewFocus);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ---- theme / resources -------------------------------------------------------

void BrowserWindow::updateTheme()
{
    m_theme = platform::queryTheme();
    platform::applyWindowMaterial(m_hwnd, m_theme);

    WTA_OPTIONS nc { WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON | WTNCA_NOSYSMENU,
                     WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON | WTNCA_NOSYSMENU };
    SetWindowThemeAttribute(m_hwnd, WTA_NONCLIENT, &nc, sizeof(nc));

    if (m_editFont)
        DeleteObject(m_editFont);
    m_editFont = CreateFontW(-px(tokens::kBodyFontSize), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable Text");
    SendMessageW(m_address, WM_SETFONT, reinterpret_cast<WPARAM>(m_editFont), TRUE);
    SendMessageW(m_historySearch, WM_SETFONT, reinterpret_cast<WPARAM>(m_editFont), TRUE);
    if (m_editBrush)
        DeleteObject(m_editBrush);
    m_editBrush = CreateSolidBrush(toColorRef(paletteFor(m_theme).fieldFocused));

    auto makeFormat = [&](ComPtr<IDWriteTextFormat>& out, float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align) {
        out.Reset();
        m_dwrite->CreateTextFormat(L"Segoe UI Variable Text", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, size, L"", out.GetAddressOf());
        out->SetTextAlignment(align);
        out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        ComPtr<IDWriteInlineObject> ellipsis;
        m_dwrite->CreateEllipsisTrimmingSign(out.Get(), ellipsis.GetAddressOf());
        DWRITE_TRIMMING trim { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        out->SetTrimming(&trim, ellipsis.Get());
    };
    makeFormat(m_hostFormat, tokens::kBodyFontSize, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER);
    makeFormat(m_tabFormat, tokens::kTabFontSize, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);
    makeFormat(m_cardTitleFormat, 12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING);
    makeFormat(m_cardBigFormat, 13.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    makeFormat(m_headingFormat, 17.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING);
    makeFormat(m_letterFormat, 24.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    makeFormat(m_listFormat, 13.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);

    discardDeviceResources();
    layout();
    RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

void BrowserWindow::createDeviceResources()
{
    if (m_rt)
        return;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), float(m_dpi), float(m_dpi));
    m_d2d->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(m_hwnd, D2D1::SizeU(rc.right, rc.bottom)),
        m_rt.GetAddressOf());
}

void BrowserWindow::discardDeviceResources()
{
    m_rt.Reset();
    m_siteIcons.clear();
    for (auto& t : m_tabs) {
        t->faviconBitmap.Reset();
        t->thumbnailBitmap.Reset();
    }
}

void BrowserWindow::updateWindowTitle()
{
    Tab* t = active();
    std::wstring caption = t ? t->displayTitle() + L" — " + brand::kProductName : brand::kProductName;
    SetWindowTextW(m_hwnd, caption.c_str());
}

void BrowserWindow::syncAddressFromTab()
{
    Tab* t = active();
    setAddressText(t ? t->url : L"");
}

void BrowserWindow::showBanner(Tab& tab, const std::wstring& text)
{
    tab.banner = text;
    layout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    SetTimer(m_hwnd, kBannerTimer, 6000, nullptr);
}

// ---- geometry ---------------------------------------------------------------

bool BrowserWindow::stripVisible() const
{
    // Safari's default: the tab bar appears once there is more than one tab.
    return m_tabs.size() > 1 || (!m_tabs.empty() && m_tabs[0]->pinned);
}

float BrowserWindow::chromeHeightDip() const
{
    return tokens::kToolbarHeight + (stripVisible() ? tokens::kTabStripHeight : 0);
}

D2D1_RECT_F BrowserWindow::contentRectDip() const
{
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    float top = chromeHeightDip();
    const Tab* t = m_active >= 0 && m_active < int(m_tabs.size()) ? m_tabs[m_active].get() : nullptr;
    if (t && !t->banner.empty() && !m_overviewOpen)
        top += tokens::kBannerHeight;
    return D2D1::RectF(0, top, rc.right / dpiScale(), rc.bottom / dpiScale());
}

RECT BrowserWindow::contentRectPx() const
{
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    auto d = contentRectDip();
    return { 0, px(d.top), rc.right, rc.bottom };
}

D2D1_RECT_F BrowserWindow::toolbarButtonRect(Hit which) const
{
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    float width = rc.right / dpiScale();
    float top = (tokens::kToolbarHeight - tokens::kButtonSize) / 2;
    auto at = [&](float left) { return D2D1::RectF(left, top, left + tokens::kButtonSize, top + tokens::kButtonSize); };
    float rightEdge = width - tokens::kCaptionReserve - 6;
    switch (which) {
    case Hit::Back: return at(tokens::kLeftInset);
    case Hit::Forward: return at(tokens::kLeftInset + tokens::kButtonSize + 2);
    case Hit::Overview: return at(rightEdge - tokens::kButtonSize);
    case Hit::NewTab: return at(rightEdge - 2 * tokens::kButtonSize - 2);
    default: return {};
    }
}

D2D1_RECT_F BrowserWindow::addressRectDip() const
{
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    float width = rc.right / dpiScale();
    float leftLimit = toolbarButtonRect(Hit::Forward).right + 16;
    float rightLimit = toolbarButtonRect(Hit::NewTab).left - 16;
    float fieldWidth = std::min(tokens::kFieldMaxWidth, std::max(tokens::kFieldMinWidth, width * 0.46f));
    float left = std::max((width - fieldWidth) / 2, leftLimit);
    float right = std::min(left + fieldWidth, rightLimit);
    float top = (tokens::kToolbarHeight - tokens::kFieldHeight) / 2;
    return D2D1::RectF(left, top, std::max(right, left + 80), top + tokens::kFieldHeight);
}

static bool contains(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

BrowserWindow::Hit BrowserWindow::hitTest(POINT pt) const
{
    float x = pt.x / dpiScale(), y = pt.y / dpiScale();
    if (y >= tokens::kToolbarHeight) {
        if (stripVisible() && y < chromeHeightDip())
            return m_strip.hitTest(x, y).tab >= 0 ? Hit::Strip : Hit::Caption;
        return Hit::Content;
    }
    for (Hit h : { Hit::Back, Hit::Forward, Hit::NewTab, Hit::Overview })
        if (contains(toolbarButtonRect(h), x, y))
            return h;
    auto field = addressRectDip();
    if (contains(field, x, y)) {
        auto reload = D2D1::RectF(field.right - tokens::kFieldHeight, field.top, field.right, field.bottom);
        return contains(reload, x, y) && !m_editing ? Hit::Reload : Hit::Address;
    }
    return Hit::Caption;
}

void BrowserWindow::layout()
{
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    float width = rc.right / dpiScale();

    MARGINS margins { 0, 0, px(chromeHeightDip()), 0 };
    DwmExtendFrameIntoClientArea(m_hwnd, &margins);

    m_strip.layout(m_tabs, tokens::kToolbarHeight, width);
    m_overview.layout(int(m_tabs.size()), contentRectDip(),
        float(rc.right) / std::max(1.f, float(rc.bottom - px(chromeHeightDip()))));
    m_startPage.layout(contentRectDip());
    m_historyView.layout(contentRectDip());
    if (m_historySearch) {
        auto r = m_historyView.searchRect();
        int h = px(tokens::kBodyFontSize * 1.45f);
        SetWindowPos(m_historySearch, HWND_TOP, px(r.left + 32), px((r.top + r.bottom) / 2) - h / 2,
            std::max(0, px(r.right - r.left - 46)), h, SWP_NOACTIVATE);
    }

    if (Tab* t = active(); t && t->view)
        t->view->setBounds(contentRectPx());

    auto f = addressRectDip();
    int editHeight = px(tokens::kBodyFontSize * 1.45f);
    SetWindowPos(m_address, nullptr, px(f.left + 14), px((f.top + f.bottom) / 2) - editHeight / 2,
        px(f.right - f.left - 28), editHeight, SWP_NOZORDER | SWP_NOACTIVATE);

    if (m_rt)
        m_rt->Resize(D2D1::SizeU(rc.right, rc.bottom));
    if (m_popup.visible())
        showSuggestions(m_popup.selected()); // follow the field
}

// ---- drawing ----------------------------------------------------------------

void BrowserWindow::paint()
{
    createDeviceResources();
    if (!m_rt)
        return;
    auto pal = paletteFor(m_theme);
    bool translucent = m_theme.transparencyEnabled && !m_theme.highContrast && !platform::isRemoteSession();

    m_rt->BeginDraw();
    m_rt->SetTransform(D2D1::Matrix3x2F::Identity());
    m_rt->Clear(translucent ? D2D1::ColorF(0, 0, 0, 0) : pal.toolbarOpaque);

    ComPtr<ID2D1SolidColorBrush> brush;
    m_rt->CreateSolidColorBrush(pal.text, brush.GetAddressOf());
    Painter p { m_rt.Get(), m_d2d.Get(), brush.Get(), m_dwrite.Get() };

    for (auto& t : m_tabs) {
        if (t->favicon && !t->faviconBitmap)
            t->faviconBitmap = makeBitmap(m_rt.Get(), t->favicon);
        if (t->thumbnail && !t->thumbnailBitmap && m_overviewOpen)
            t->thumbnailBitmap = makeBitmap(m_rt.Get(), t->thumbnail);
    }

    Tab* tab = active();
    // Back / forward.
    bool canBack = tab && tab->view && tab->view->canGoBack() && !m_overviewOpen;
    bool canForward = tab && tab->view && tab->view->canGoForward() && !m_overviewOpen;
    auto button = [&](Hit id, bool enabled) {
        auto r = toolbarButtonRect(id);
        if (enabled && (m_hover == id || m_pressed == id))
            p.fill(D2D1::RoundedRect(r, tokens::kButtonRadius, tokens::kButtonRadius), m_pressed == id ? pal.pressed : pal.hover);
        return r;
    };
    p.chevron(button(Hit::Back, canBack), true, canBack ? pal.text : pal.secondary);
    p.chevron(button(Hit::Forward, canForward), false, canForward ? pal.text : pal.secondary);
    auto nt = button(Hit::NewTab, true);
    p.plus(D2D1::Point2F((nt.left + nt.right) / 2, (nt.top + nt.bottom) / 2), 6, pal.text);
    auto ov = button(Hit::Overview, true);
    if (m_overviewOpen)
        p.fill(D2D1::RoundedRect(ov, tokens::kButtonRadius, tokens::kButtonRadius), pal.pressed);
    p.tabGrid(D2D1::Point2F((ov.left + ov.right) / 2, (ov.top + ov.bottom) / 2), pal.text);

    // Smart Search field.
    auto f = addressRectDip();
    float radius = tokens::kFieldHeight / 2;
    auto capsule = D2D1::RoundedRect(f, radius, radius);
    p.fill(capsule, m_editing ? pal.fieldFocused : pal.fieldIdle);
    if (m_editing) {
        brush->SetColor(pal.accent);
        m_rt->DrawRoundedRectangle(capsule, brush.Get(), 2.0f);
    }
    if (tab && tab->loading && tab->progress > 0 && !m_editing) {
        ComPtr<ID2D1RoundedRectangleGeometry> clip;
        m_d2d->CreateRoundedRectangleGeometry(capsule, clip.GetAddressOf());
        m_rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip.Get()), nullptr);
        auto fill = f;
        fill.right = f.left + float((f.right - f.left) * tab->progress);
        auto accent = pal.accent;
        accent.a = 0.18f;
        p.fill(fill, accent);
        m_rt->PopLayer();
    }
    if (!m_editing) {
        std::wstring url = tab ? tab->url : L"";
        bool blank = url.empty() || url == L"about:blank";
        std::wstring host = blank ? std::wstring(L"Search or enter website name") : services::displayHost(url);
        auto textRect = D2D1::RectF(f.left + 36, f.top, f.right - 36, f.bottom);
        ComPtr<IDWriteTextLayout> layoutObj;
        m_dwrite->CreateTextLayout(host.c_str(), UINT32(host.size()), m_hostFormat.Get(),
            textRect.right - textRect.left, textRect.bottom - textRect.top, layoutObj.GetAddressOf());
        DWRITE_TEXT_METRICS tm {};
        layoutObj->GetMetrics(&tm);
        brush->SetColor(blank ? pal.secondary : pal.text);
        m_rt->DrawTextLayout(D2D1::Point2F(textRect.left, textRect.top), layoutObj.Get(), brush.Get());
        if (services::isSecureURL(url))
            p.lock(D2D1::Point2F(textRect.left + tm.left - 10, (f.top + f.bottom) / 2), pal.secondary);

        auto rr = D2D1::RectF(f.right - tokens::kFieldHeight, f.top, f.right, f.bottom);
        auto c = D2D1::Point2F((rr.left + rr.right) / 2, (rr.top + rr.bottom) / 2);
        if (m_hover == Hit::Reload || m_pressed == Hit::Reload) {
            brush->SetColor(m_pressed == Hit::Reload ? pal.pressed : pal.hover);
            m_rt->FillEllipse(D2D1::Ellipse(c, 13, 13), brush.Get());
        }
        if (tab && tab->loading)
            p.cross(c, 4.2f, tokens::kGlyphStroke, pal.text);
        else
            p.reload(c, pal.text);
    }

    if (stripVisible()) {
        auto s = m_stripState;
        s.active = m_active;
        s.dragging = m_drag.moved && !m_drag.tearOff ? m_drag.index : -1;
        m_strip.paint(p, pal, m_tabFormat.Get(), m_tabs, s);
    }

    if (tab && !tab->banner.empty() && !m_overviewOpen) {
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        float top = chromeHeightDip();
        auto br = D2D1::RectF(0, top, rc.right / dpiScale(), top + tokens::kBannerHeight);
        p.fill(br, pal.banner);
        brush->SetColor(pal.text);
        m_rt->DrawTextW(tab->banner.c_str(), UINT32(tab->banner.size()), m_hostFormat.Get(), br, brush.Get());
    }

    auto icon = [this](const std::wstring& host) { return siteIconBitmap(host); };
    if (showingStartPage())
        m_startPage.paint(p, pal, m_headingFormat.Get(), m_cardBigFormat.Get(), m_letterFormat.Get(), m_listFormat.Get(),
            m_startHover, icon);
    if (m_historyOpen)
        m_historyView.paint(p, pal, m_cardTitleFormat.Get(), m_listFormat.Get(), m_tabFormat.Get(), m_listFormat.Get(),
            m_historyHover, m_historySelected, !trimmed(windowText(m_historySearch)).empty(), icon);

    if (m_overviewOpen)
        m_overview.paint(p, pal, m_cardTitleFormat.Get(), m_cardBigFormat.Get(), m_tabs, m_active, m_overviewHover,
            m_overviewHoverClose, m_overviewFocus);

    if (m_rt->EndDraw() == D2DERR_RECREATE_TARGET)
        discardDeviceResources();
}

// ---- address editing --------------------------------------------------------

void BrowserWindow::beginEditingAddress(bool selectAll)
{
    if (m_overviewOpen)
        setOverview(false);
    if (m_historyOpen)
        setHistory(false);
    if (!m_editing) {
        m_editing = true;
        Tab* t = active();
        std::wstring url = t && t->url != L"about:blank" ? t->url : L"";
        setAddressText(url);
        m_typed = url;
        m_deleteKey = false;
        m_suggestions = {};
        ShowWindow(m_address, SW_SHOW);
    }
    SetFocus(m_address);
    if (selectAll)
        SendMessageW(m_address, EM_SETSEL, 0, -1);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void BrowserWindow::endEditingAddress(bool revert)
{
    if (!m_editing)
        return;
    m_editing = false;
    hideSuggestions();
    m_app.searchSuggestions().cancel();
    if (revert)
        syncAddressFromTab();
    ShowWindow(m_address, SW_HIDE);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void BrowserWindow::navigateFromAddressBar()
{
    // A highlighted row wins (this includes the Top Hit while it is being
    // autocompleted); otherwise the text is read as an address or a search.
    int selected = m_popup.visible() ? m_popup.selected() : -1;
    if (selected >= 0 && selected < int(m_popup.items().size())) {
        activateSuggestion(selected);
        return;
    }
    auto fixed = services::fixupInput(addressText(), brand::kSearchURLPrefix);
    if (!fixed.url.empty())
        navigate(fixed.url, !fixed.isSearch);
}

void BrowserWindow::navigate(const std::wstring& url, bool typed)
{
    Tab* t = active();
    if (url.empty() || !t)
        return;
    if (m_historyOpen)
        setHistory(false);
    if (m_overviewOpen)
        setOverview(false);
    t->url = url;
    t->banner.clear();
    t->startPage = false;
    t->typedPending = typed;
    endEditingAddress(false);
    if (!t->view) {
        t->pendingState.clear(); // load `url`, not the restored page
        ensureView(*t);
    } else
        t->view->loadURL(url);
    layout();
    updateContentView();
    SetFocus(t->view->hwnd());
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ---- Smart Search suggestions -------------------------------------------------

std::wstring BrowserWindow::addressText() const
{
    return windowText(m_address);
}

void BrowserWindow::setAddressText(const std::wstring& text)
{
    m_settingAddress = true;
    SetWindowTextW(m_address, text.c_str());
    SendMessageW(m_address, EM_SETSEL, text.size(), text.size());
    m_settingAddress = false;
}

void BrowserWindow::onAddressEdited()
{
    if (m_settingAddress || !m_editing)
        return;
    std::wstring text = addressText();
    bool deleting = m_deleteKey;
    m_deleteKey = false;
    DWORD selStart = 0, selEnd = 0;
    SendMessageW(m_address, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
    bool caretAtEnd = selStart == selEnd && selEnd == text.size();
    m_typed = text;
    // Completing while the user deletes would put back what they just removed.
    refreshSuggestions(!deleting && caretAtEnd);
}

void BrowserWindow::refreshSuggestions(bool allowCompletion)
{
    m_suggestions = {};
    std::wstring query = trimmed(m_typed);
    auto* history = m_app.history();
    auto* bookmarks = m_app.bookmarks();
    if (query.empty() || !history || !bookmarks) {
        hideSuggestions();
        m_app.searchSuggestions().cancel();
        return;
    }
    try {
        services::SuggestionOptions options;
        options.searchPrefix = brand::kSearchURLPrefix;
        options.allowCompletion = allowCompletion;
        m_suggestions = services::buildSuggestions(*history, *bookmarks, m_typed, options);
    } catch (const std::exception& e) {
        platform::log(L"suggestions FAILED: %hs", e.what());
        m_suggestions = {};
    }
    if (!m_suggestions.completion.empty()) {
        // Inline autocomplete: the completion is selected, so typing on replaces it.
        m_settingAddress = true;
        SetWindowTextW(m_address, (m_typed + m_suggestions.completion).c_str());
        SendMessageW(m_address, EM_SETSEL, m_typed.size(), -1);
        m_settingAddress = false;
    }
    showSuggestions(m_suggestions.defaultSelection);

    // Search-engine suggestions: opt-in only, never for private windows.
    if (!m_app.searchSuggestionsEnabled() || context().isEphemeral()) {
        m_app.searchSuggestions().cancel();
        return;
    }
    HWND hwnd = m_hwnd;
    m_app.searchSuggestions().fetch(brand::kSuggestURLPrefix, query,
        [this, hwnd](const std::wstring& q, std::vector<std::wstring> results) {
            if (!IsWindow(hwnd) || !m_editing || q != trimmed(m_typed) || results.empty())
                return;
            auto& items = m_suggestions.items;
            // After the typed-text rows, before history and bookmarks.
            size_t at = 0;
            while (at < items.size() && (items[at].kind == services::SuggestionKind::TopHit
                       || items[at].kind == services::SuggestionKind::Search
                       || items[at].kind == services::SuggestionKind::Navigate))
                ++at;
            std::vector<services::Suggestion> extra;
            for (auto& r : results) {
                if (extra.size() >= kMaxSearchSuggestions)
                    break;
                if (services::toLower(r) == services::toLower(q))
                    continue;
                extra.push_back({ services::SuggestionKind::SearchSuggestion, r,
                    brand::kSearchURLPrefix + services::percentEncodeQuery(r) });
            }
            if (extra.empty())
                return;
            items.insert(items.begin() + at, extra.begin(), extra.end());
            int selected = m_popup.visible() ? m_popup.selected() : m_suggestions.defaultSelection;
            if (selected >= int(at))
                selected += int(extra.size());
            showSuggestions(selected);
        });
}

void BrowserWindow::showSuggestions(int selected)
{
    if (!m_editing || m_suggestions.items.empty()) {
        hideSuggestions();
        return;
    }
    auto f = addressRectDip();
    RECT field { px(f.left), px(f.top), px(f.right), px(f.bottom) };
    MapWindowPoints(m_hwnd, nullptr, reinterpret_cast<POINT*>(&field), 2);
    m_popup.show(field, m_dpi, m_theme, m_suggestions.items, selected);
}

void BrowserWindow::hideSuggestions()
{
    m_popup.hide();
}

void BrowserWindow::moveSuggestionSelection(int delta)
{
    int n = int(m_popup.items().size());
    if (!m_popup.visible() || n == 0)
        return;
    // -1 is "what I typed"; wraps around through it.
    int selected = m_popup.selected() + delta;
    if (selected < -1)
        selected = n - 1;
    if (selected >= n)
        selected = -1;
    m_popup.setSelected(selected);
    setAddressText(selected < 0 ? m_typed : fieldTextFor(m_popup.items()[selected]));
}

void BrowserWindow::activateSuggestion(int index)
{
    if (index < 0 || index >= int(m_popup.items().size()))
        return;
    services::Suggestion s = m_popup.items()[index]; // copy: navigating hides the popup
    bool search = s.kind == services::SuggestionKind::Search || s.kind == services::SuggestionKind::SearchSuggestion;
    navigate(s.url, !search);
}

void BrowserWindow::removeSelectedSuggestionFromHistory()
{
    int selected = m_popup.selected();
    if (selected < 0 || selected >= int(m_popup.items().size()) || !m_app.history())
        return;
    int64_t id = m_popup.items()[selected].historyId;
    if (!id)
        return;
    try {
        m_app.history()->remove(id);
    } catch (const std::exception& e) {
        platform::log(L"history delete FAILED: %hs", e.what());
    }
    setAddressText(m_typed);
    refreshSuggestions(false);
}

bool BrowserWindow::handleAddressKey(WPARAM vk, bool shift)
{
    switch (vk) {
    case VK_DOWN:
    case VK_UP:
        moveSuggestionSelection(vk == VK_DOWN ? 1 : -1);
        return true;
    case VK_RETURN:
        navigateFromAddressBar();
        return true;
    case VK_ESCAPE:
        if (m_popup.visible()) {
            // First Escape: drop the menu and the completion, keep what was typed.
            hideSuggestions();
            m_suggestions = {};
            m_app.searchSuggestions().cancel();
            setAddressText(m_typed);
        } else {
            endEditingAddress(true);
            focusContent();
        }
        return true;
    case VK_DELETE:
        if (shift && m_popup.visible() && m_popup.selected() >= 0
            && m_popup.items()[m_popup.selected()].historyId) {
            removeSelectedSuggestionFromHistory();
            return true;
        }
        m_deleteKey = true;
        return false;
    case VK_BACK:
        m_deleteKey = true;
        return false;
    }
    return false;
}

LRESULT CALLBACK BrowserWindow::addressProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref)
{
    auto* self = reinterpret_cast<BrowserWindow*>(ref);
    switch (msg) {
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DefSubclassProc(h, msg, wp, lp);
    case WM_KEYDOWN:
        if (self->handleAddressKey(wp, (GetKeyState(VK_SHIFT) & 0x8000) != 0))
            return 0;
        break;
    case WM_CHAR:
        if (wp == VK_RETURN || wp == VK_ESCAPE)
            return 0;
        break;
    case WM_KILLFOCUS:
        self->endEditingAddress(true);
        break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// ---- input ------------------------------------------------------------------

bool BrowserWindow::handleShortcut(const MSG& msg)
{
    Tab* tab = active();
    if (msg.message == WM_XBUTTONUP && tab && tab->view) {
        if (GET_XBUTTON_WPARAM(msg.wParam) == XBUTTON1)
            tab->view->goBack();
        else
            tab->view->goForward();
        return true;
    }
    if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN)
        return false;
    bool ctrl = GetKeyState(VK_CONTROL) & 0x8000;
    bool alt = GetKeyState(VK_MENU) & 0x8000;
    bool shift = GetKeyState(VK_SHIFT) & 0x8000;
    WPARAM k = msg.wParam;
    int n = int(m_tabs.size());

    // Tabs and windows.
    if (ctrl && !shift && k == 'T') {
        openTab(L"about:blank", true, n);
        beginEditingAddress(true);
        return true;
    }
    if (ctrl && shift && k == 'T') { reopenClosedTab(); return true; }
    if (ctrl && !shift && (k == 'W' || k == VK_F4)) { closeTab(m_active); return true; }
    if (ctrl && shift && k == 'W') { PostMessageW(m_hwnd, WM_CLOSE, 0, 0); return true; }
    if (ctrl && !shift && k == 'N') { m_app.newWindow(nullptr, nullptr, true); return true; }
    if (ctrl && (k == VK_TAB || k == VK_PRIOR || k == VK_NEXT || (shift && (k == VK_OEM_4 || k == VK_OEM_6)))) {
        bool prev = (k == VK_TAB && shift) || k == VK_PRIOR || k == VK_OEM_4;
        if (n > 1)
            activate((m_active + (prev ? n - 1 : 1)) % n);
        return true;
    }
    if (ctrl && !shift && !alt && k >= '1' && k <= '9') {
        activate(k == '9' ? n - 1 : std::min(int(k - '1'), n - 1));
        return true;
    }
    if (ctrl && shift && k == VK_OEM_5) { setOverview(!m_overviewOpen); return true; }
    // History: Ctrl+H, the Windows browser convention (Ctrl+Y is Redo in page text fields).
    if (ctrl && !shift && !alt && k == 'H') { setHistory(!m_historyOpen); return true; }
    if (ctrl && !shift && !alt && k == 'D') { addBookmark(); return true; }

    if (m_historyOpen) {
        int count = m_historyView.count();
        switch (k) {
        case VK_ESCAPE: setHistory(false); return true;
        case VK_DOWN:
        case VK_UP:
            if (count) {
                m_historySelected = std::clamp(m_historySelected + (k == VK_DOWN ? 1 : -1), 0, count - 1);
                m_historyView.scrollToShow(m_historySelected);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return true;
        case VK_RETURN:
            if (count)
                openHistoryEntry(m_historySelected >= 0 ? m_historySelected : 0, ctrl);
            return true;
        case VK_DELETE:
            // In the search field, Delete edits the text unless the field is empty.
            if (m_historySelected >= 0 && (GetFocus() != m_historySearch || GetWindowTextLengthW(m_historySearch) == 0)) {
                deleteHistoryEntry(m_historySelected);
                return true;
            }
            break;
        }
    }

    if (m_overviewOpen) {
        switch (k) {
        case VK_ESCAPE: setOverview(false); return true;
        case VK_LEFT: moveOverviewFocus(-1, 0); return true;
        case VK_RIGHT: moveOverviewFocus(1, 0); return true;
        case VK_UP: moveOverviewFocus(0, -1); return true;
        case VK_DOWN: moveOverviewFocus(0, 1); return true;
        case VK_RETURN:
        case VK_SPACE:
            if (m_overviewFocus >= 0) {
                activate(m_overviewFocus);
                setOverview(false);
            }
            return true;
        case VK_DELETE:
        case VK_BACK:
            if (m_overviewFocus >= 0 && n > 1) {
                closeTab(m_overviewFocus);
                m_overviewFocus = std::min(m_overviewFocus, int(m_tabs.size()) - 1);
                layout();
            }
            return true;
        }
    }

    // Navigation.
    if ((ctrl && k == 'L') || (alt && k == 'D') || k == VK_F6) { beginEditingAddress(true); return true; }
    if (!tab || !tab->view)
        return false;
    if ((ctrl && k == 'R') || k == VK_F5) { tab->view->reload(); return true; }
    if ((alt && k == VK_LEFT) || (ctrl && !shift && k == VK_OEM_4)) { tab->view->goBack(); return true; }
    if ((alt && k == VK_RIGHT) || (ctrl && !shift && k == VK_OEM_6)) { tab->view->goForward(); return true; }
    if (k == VK_F12 || (ctrl && alt && k == 'I') || (ctrl && shift && k == 'I')) { tab->view->showInspector(); return true; }
    if (k == VK_ESCAPE && tab->loading && !m_editing) { tab->view->stop(); return true; }
    return false;
}

LRESULT BrowserWindow::handle(UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT dwmResult = 0;
    if (DwmDefWindowProc(m_hwnd, msg, wp, lp, &dwmResult))
        return dwmResult;

    switch (msg) {
    case WM_CREATE:
        onCreate();
        return 0;

    case WM_NCCALCSIZE:
        if (wp) {
            auto& p = *reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
            int fx = GetSystemMetricsForDpi(SM_CXFRAME, m_dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, m_dpi);
            int fy = GetSystemMetricsForDpi(SM_CYFRAME, m_dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, m_dpi);
            p.rgrc[0].left += fx;
            p.rgrc[0].right -= fx;
            p.rgrc[0].bottom -= fy;
            if (IsZoomed(m_hwnd))
                p.rgrc[0].top += fy;
            return 0;
        }
        break;

    case WM_NCHITTEST: {
        POINT screenPt { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        // Caption buttons first: DwmDefWindowProc stops recognizing them when
        // maximized, so hit-test the DWM-reported button rect ourselves.
        RECT window, buttons;
        if (GetWindowRect(m_hwnd, &window)
            && SUCCEEDED(DwmGetWindowAttribute(m_hwnd, DWMWA_CAPTION_BUTTON_BOUNDS, &buttons, sizeof(buttons)))
            && buttons.right > buttons.left) {
            OffsetRect(&buttons, window.left, window.top);
            RECT hit = buttons;
            if (IsZoomed(m_hwnd))
                hit.right = window.right;
            if (PtInRect(&hit, screenPt)) {
                LONG third = std::max<LONG>((buttons.right - buttons.left) / 3, 1);
                LONG index = std::min<LONG>(2, (screenPt.x - buttons.left) / third);
                static constexpr LRESULT codes[] = { HTMINBUTTON, HTMAXBUTTON, HTCLOSE };
                return codes[index];
            }
        }
        POINT pt = screenPt;
        ScreenToClient(m_hwnd, &pt);
        int frame = GetSystemMetricsForDpi(SM_CYFRAME, m_dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, m_dpi);
        if (!IsZoomed(m_hwnd) && pt.y < frame)
            return HTTOP;
        LRESULT def = DefWindowProcW(m_hwnd, msg, wp, lp);
        if (def != HTCLIENT)
            return def;
        return hitTest(pt) == Hit::Caption ? HTCAPTION : HTCLIENT;
    }

    case WM_ACTIVATE:
        updateTheme();
        if (LOWORD(wp) != WA_INACTIVE && !m_editing && !m_overviewOpen)
            if (Tab* t = active(); t && t->view)
                SetFocus(t->view->hwnd());
        return 0;

    case WM_SIZE:
        layout();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;

    case WM_EXITSIZEMOVE:
        m_app.sessionChanged();
        return 0;

    case WM_DPICHANGED: {
        m_dpi = HIWORD(wp);
        auto* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        updateTheme();
        return 0;
    }

    case WM_SETTINGCHANGE:
        if (lp && wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0)
            updateTheme();
        return 0;

    case WM_THEMECHANGED:
        updateTheme();
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(m_hwnd, &ps);
        paint();
        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEMOVE: {
        POINT pt { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        float x = pt.x / dpiScale(), y = pt.y / dpiScale();
        if (m_drag.index >= 0 && (wp & MK_LBUTTON)) {
            if (!m_drag.moved && (std::abs(pt.x - m_drag.start.x) > px(4) || std::abs(pt.y - m_drag.start.y) > px(4)))
                m_drag.moved = true;
            if (m_drag.moved) {
                auto bounds = m_strip.bounds();
                m_drag.tearOff = m_tabs.size() > 1 && (y < bounds.top - 30 || y > bounds.bottom + 30);
                m_stripState.dragX = x;
                if (!m_drag.tearOff) {
                    int target = m_strip.dropIndex(m_tabs, m_drag.index, x);
                    if (target != m_drag.index) {
                        auto moving = std::move(m_tabs[m_drag.index]);
                        m_tabs.erase(m_tabs.begin() + m_drag.index);
                        m_tabs.insert(m_tabs.begin() + target, std::move(moving));
                        if (m_active == m_drag.index)
                            m_active = target;
                        m_drag.index = target;
                        layout();
                    }
                }
                SetCursor(LoadCursorW(nullptr, m_drag.tearOff ? IDC_SIZEALL : IDC_ARROW));
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        }
        Hit h = hitTest(pt);
        auto sh = stripVisible() ? m_strip.hitTest(x, y) : TabStrip::Hit {};
        int ovHover = -1;
        bool ovClose = false;
        if (m_overviewOpen && h == Hit::Content) {
            auto oh = m_overview.hitTest(x, y);
            ovHover = oh.tab;
            ovClose = oh.close;
        }
        StartPage::Hit startHover;
        if (showingStartPage() && h == Hit::Content)
            startHover = m_startPage.hitTest(x, y);
        int historyHover = m_historyOpen && h == Hit::Content ? m_historyView.hitTest(x, y) : -1;
        if (h != m_hover || sh.tab != m_stripState.hover || sh.close != m_stripState.hoverClose
            || ovHover != m_overviewHover || ovClose != m_overviewHoverClose
            || !(startHover == m_startHover) || historyHover != m_historyHover) {
            m_startHover = startHover;
            m_historyHover = historyHover;
            m_hover = h;
            m_stripState.hover = sh.tab;
            m_stripState.hoverClose = sh.close;
            m_overviewHover = ovHover;
            m_overviewHoverClose = ovClose;
            TRACKMOUSEEVENT tme { sizeof(tme), TME_LEAVE, m_hwnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        m_hover = Hit::None;
        m_stripState.hover = -1;
        m_overviewHover = -1;
        m_startHover = {};
        m_historyHover = -1;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        POINT pt { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        float x = pt.x / dpiScale(), y = pt.y / dpiScale();
        m_pressed = hitTest(pt);
        if (m_pressed == Hit::Address) {
            beginEditingAddress(true);
            m_pressed = Hit::None;
        } else if (m_pressed == Hit::Strip) {
            auto sh = m_strip.hitTest(x, y);
            if (!sh.close) {
                // Safari selects on mouse down; dragging starts after a few pixels.
                activate(sh.tab);
                m_drag = { sh.tab, pt, false, false };
                auto r = m_strip.tabRect(sh.tab);
                m_stripState.dragGrabOffset = x - r.left;
                m_stripState.dragX = x;
            }
            SetCapture(m_hwnd);
        } else
            SetCapture(m_hwnd);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONUP: {
        ReleaseCapture();
        POINT pt { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        float x = pt.x / dpiScale(), y = pt.y / dpiScale();
        Hit h = hitTest(pt);
        Tab* tab = active();
        if (m_drag.index >= 0) {
            auto drag = m_drag;
            m_drag = {};
            if (drag.moved && drag.tearOff) {
                POINT screen = pt;
                ClientToScreen(m_hwnd, &screen);
                moveTabToNewWindow(drag.index, screen);
            } else if (drag.moved)
                tabsChanged();
        } else if (h == m_pressed) {
            switch (h) {
            case Hit::Back: if (tab && tab->view) tab->view->goBack(); break;
            case Hit::Forward: if (tab && tab->view) tab->view->goForward(); break;
            case Hit::Reload:
                if (tab && tab->view)
                    tab->loading ? tab->view->stop() : tab->view->reload();
                break;
            case Hit::NewTab:
                openTab(L"about:blank", true, int(m_tabs.size()));
                beginEditingAddress(true);
                break;
            case Hit::Overview: setOverview(!m_overviewOpen); break;
            case Hit::Strip: {
                auto sh = m_strip.hitTest(x, y);
                if (sh.close)
                    closeTab(sh.tab);
                break;
            }
            case Hit::Content: {
                bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                if (m_overviewOpen) {
                    auto oh = m_overview.hitTest(x, y);
                    if (oh.tab >= 0 && oh.close && !m_tabs[oh.tab]->pinned) {
                        if (m_tabs.size() > 1)
                            closeTab(oh.tab);
                    } else if (oh.tab >= 0) {
                        activate(oh.tab);
                        setOverview(false);
                    }
                } else if (m_historyOpen) {
                    int e = m_historyView.hitTest(x, y);
                    if (e >= 0) {
                        m_historySelected = e;
                        openHistoryEntry(e, ctrl);
                    }
                } else if (showingStartPage()) {
                    if (auto* item = m_startPage.item(m_startPage.hitTest(x, y))) {
                        std::wstring url = item->url; // navigating rebuilds the start page
                        if (ctrl)
                            openTab(url, false, m_active + 1);
                        else
                            navigate(url, false);
                    }
                }
                break;
            }
            default: break;
            }
        }
        m_pressed = Hit::None;
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        float dy = -GET_WHEEL_DELTA_WPARAM(wp) / float(WHEEL_DELTA) * 90.f;
        if (m_overviewOpen)
            m_overview.scrollBy(dy);
        else if (m_historyOpen)
            m_historyView.scrollBy(dy);
        else if (showingStartPage())
            m_startPage.scrollBy(dy);
        else
            break;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_MBUTTONUP: {
        POINT pt { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        float x = pt.x / dpiScale(), y = pt.y / dpiScale();
        if (stripVisible()) {
            auto sh = m_strip.hitTest(x, y);
            if (sh.tab >= 0)
                closeTab(sh.tab);
        }
        return 0;
    }

    case WM_RBUTTONUP: {
        POINT pt { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        float x = pt.x / dpiScale(), y = pt.y / dpiScale();
        int index = -1;
        if (stripVisible())
            index = m_strip.hitTest(x, y).tab;
        if (index < 0 && m_overviewOpen)
            index = m_overview.hitTest(x, y).tab;
        POINT screen = pt;
        ClientToScreen(m_hwnd, &screen);
        if (index >= 0)
            showTabMenu(index, screen);
        else if (hitTest(pt) == Hit::Content && m_historyOpen)
            showHistoryMenu(m_historyView.hitTest(x, y), screen);
        else if (hitTest(pt) == Hit::Content && showingStartPage())
            showStartPageMenu(screen, m_startPage.hitTest(x, y));
        return 0;
    }

    case WM_COMMAND:
        if (HIWORD(wp) == EN_CHANGE) {
            HWND from = reinterpret_cast<HWND>(lp);
            if (from == m_address)
                onAddressEdited();
            else if (from == m_historySearch && m_historyOpen)
                refreshHistory();
            return 0;
        }
        break;

    case WM_MOVE:
        if (m_popup.visible())
            showSuggestions(m_popup.selected());
        break;

    case WM_CTLCOLOREDIT: {
        auto pal = paletteFor(m_theme);
        HDC dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, toColorRef(pal.text));
        SetBkColor(dc, toColorRef(pal.fieldFocused));
        return reinterpret_cast<LRESULT>(m_editBrush);
    }

    case WM_TIMER:
        if (wp == kBannerTimer) {
            KillTimer(m_hwnd, kBannerTimer);
            if (Tab* t = active())
                t->banner.clear();
            layout();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_APPCOMMAND:
        if (Tab* t = active(); t && t->view) {
            switch (GET_APPCOMMAND_LPARAM(lp)) {
            case APPCOMMAND_BROWSER_BACKWARD: t->view->goBack(); return TRUE;
            case APPCOMMAND_BROWSER_FORWARD: t->view->goForward(); return TRUE;
            case APPCOMMAND_BROWSER_REFRESH: t->view->reload(); return TRUE;
            }
        }
        break;

    case WM_COPYDATA: {
        auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lp);
        if (!m_app.automationEnabled() || !cds || cds->dwData != 0x4D52 || cds->cbData % sizeof(wchar_t))
            return FALSE;
        // A cross-process SendMessage is an input-synchronous call, in which
        // COM/WinRT calls fail (RPC_E_CANTCALLOUT_ININPUTSYNCCALL). Re-post so
        // the command runs from the message loop, exactly like real input.
        auto* command = new std::wstring(static_cast<const wchar_t*>(cds->lpData), cds->cbData / sizeof(wchar_t));
        if (!PostMessageW(m_hwnd, kAutomationMessage, 0, reinterpret_cast<LPARAM>(command)))
            delete command;
        return TRUE;
    }

    case kAutomationMessage: {
        std::unique_ptr<std::wstring> command(reinterpret_cast<std::wstring*>(lp));
        automate(*command);
        return 0;
    }

    case WM_QUERYENDSESSION:
        m_app.saveSessionNow();
        return TRUE;

    case WM_CLOSE:
        if (!m_closing) {
            m_closing = true;
            m_app.windowClosing(this);
            DestroyWindow(m_hwnd);
        }
        return 0;

    case WM_DESTROY: {
        // Destroy WebViews while the parent HWND still exists. Move them out
        // first so any late callback sees an empty tab list, not a vector
        // that is mid-destruction.
        m_popup.destroy();
        auto dying = std::move(m_tabs);
        m_tabs.clear();
        m_active = -1;
        dying.clear();
        if (m_editFont)
            DeleteObject(m_editFont);
        if (m_editBrush)
            DeleteObject(m_editBrush);
        m_app.windowDestroyed(this);
        return 0;
    }
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

} // namespace meridian::ui
