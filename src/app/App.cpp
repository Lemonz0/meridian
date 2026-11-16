#include "App.h"

#include "config/Brand.h"
#include "platform/Log.h"
#include "platform/UiDispatcher.h"
#include "platform/WindowCapture.h"
#include "ui/BrowserWindow.h"

#include <algorithm>
#include <filesystem>
#include <shlobj.h>
#include <thread>
#include <winrt/base.h>

namespace meridian::app {

namespace {
constexpr UINT_PTR kSaveTimer = 1;
constexpr UINT kSaveDelayMs = 1000;
constexpr size_t kMaxClosedTabs = 50;

std::filesystem::path defaultProfileDirectory()
{
    PWSTR base = nullptr;
    std::filesystem::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)))
        dir = std::filesystem::path(base) / L"Meridian" / L"Default";
    CoTaskMemFree(base);
    return dir;
}
}

void App::setProfileDirectory(const std::filesystem::path& dir)
{
    m_profile = dir;
}

App& App::instance()
{
    static App app;
    return app;
}

LRESULT CALLBACK App::timerProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_TIMER && w == kSaveTimer) {
        KillTimer(h, kSaveTimer);
        instance().saveSessionNow();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int App::run(const std::wstring& initialURL)
{
    platform::initUiDispatcher();
    std::thread([] {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        platform::requestBorderlessCapture();
    }).detach();

    WNDCLASSW wc {};
    wc.lpfnWndProc = timerProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MeridianAppTimer";
    RegisterClassW(&wc);
    m_timerWindow = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);

    if (m_profile.empty())
        m_profile = defaultProfileDirectory();
    std::error_code ec;
    std::filesystem::create_directories(m_profile, ec);
    if (m_automation)
        platform::setLogFile((m_profile / L"meridian.log").wstring());

    try {
        m_db = std::make_unique<services::Database>(m_profile / L"Meridian.db");
        m_session = std::make_unique<services::SessionStore>(*m_db);
    } catch (const std::exception&) {
        m_session.reset(); // session restore disabled; browsing still works
        m_db.reset();
    }

    m_engine = std::make_unique<engine::EngineContext>(false, m_profile);
    platform::windowCaptureSupported(); // probe once, outside any sent message (result is cached)

    std::vector<services::SessionWindow> restored;
    if (m_session) {
        try {
            restored = m_session->load();
            platform::log(L"session loaded: %zu windows", restored.size());
        } catch (const std::exception& e) {
            platform::log(L"session load FAILED: %hs", e.what());
        }
    }
    for (auto& w : restored)
        newWindow(&w, nullptr, false);
    if (m_windows.empty())
        newWindow(nullptr, nullptr, initialURL.empty());
    if (!initialURL.empty())
        m_windows.front()->openTab(initialURL, true);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        engine::WebView::noteInput(msg);
        if (routeMessage(msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    saveSessionNow();
    return 0;
}

bool App::routeMessage(MSG& msg)
{
    if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN && msg.message != WM_XBUTTONUP)
        return false;
    HWND root = GetAncestor(msg.hwnd, GA_ROOT);
    for (auto* w : m_windows)
        if (w->hwnd() == root)
            return w->handleShortcut(msg);
    return false;
}

ui::BrowserWindow* App::newWindow(const services::SessionWindow* restore, const RECT* bounds, bool addDefaultTab)
{
    // Register before creating so tabs restored during creation can save.
    auto* w = ui::BrowserWindow::create(*this, restore, bounds, addDefaultTab);
    if (w && std::find(m_windows.begin(), m_windows.end(), w) == m_windows.end())
        m_windows.push_back(w);
    sessionChanged();
    return w;
}

void App::windowClosing(ui::BrowserWindow* w)
{
    platform::log(L"window closing (%zu open)", m_windows.size());
    if (m_windows.size() == 1 && m_windows.front() == w) {
        // Closing the last window: that window is the session to restore.
        m_lastWindowSnapshot = { w->sessionSnapshot() };
    }
    m_windows.erase(std::remove(m_windows.begin(), m_windows.end(), w), m_windows.end());
    saveSessionNow();
}

void App::windowDestroyed(ui::BrowserWindow* w)
{
    m_windows.erase(std::remove(m_windows.begin(), m_windows.end(), w), m_windows.end());
    if (m_windows.empty())
        PostQuitMessage(0);
}

void App::dumpState(const std::wstring& path)
{
    std::wstring out;
    for (size_t w = 0; w < m_windows.size(); ++w) {
        auto* win = m_windows[w];
        WINDOWPLACEMENT wp { sizeof(wp) };
        GetWindowPlacement(win->hwnd(), &wp);
        out += L"window " + std::to_wstring(w) + L" active=" + std::to_wstring(win->activeIndex())
            + L" maximized=" + std::to_wstring(wp.showCmd == SW_SHOWMAXIMIZED) + L"\n";
        for (int i = 0; i < win->tabCount(); ++i) {
            auto& t = win->tab(i);
            out += L"tab " + std::to_wstring(i) + L" pinned=" + std::to_wstring(t.pinned) + L" view=" + std::to_wstring(bool(t.view))
                + L" favicon=" + std::to_wstring(bool(t.favicon)) + L" thumb=" + std::to_wstring(bool(t.thumbnail))
                + L" loading=" + std::to_wstring(t.loading)
                + L" back=" + std::to_wstring(t.view && t.view->canGoBack())
                + L" state=" + std::to_wstring(t.view ? t.view->sessionState().size() : t.pendingState.size())
                + L" url=" + t.url + L" title=" + t.title + L"\n";
        }
    }
    out += L"closed " + std::to_wstring(m_closed.size()) + L"\n";
    int len = WideCharToMultiByte(CP_UTF8, 0, out.c_str(), int(out.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, out.c_str(), int(out.size()), utf8.data(), len, nullptr, nullptr);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(f, utf8.data(), DWORD(utf8.size()), &written, nullptr);
        CloseHandle(f);
    }
}

void App::pushClosedTab(ClosedTab tab)
{
    if (tab.url.empty() || tab.url == L"about:blank")
        return;
    m_closed.push_back(std::move(tab));
    if (m_closed.size() > kMaxClosedTabs)
        m_closed.erase(m_closed.begin());
}

std::optional<ClosedTab> App::popClosedTab()
{
    if (m_closed.empty())
        return std::nullopt;
    auto t = std::move(m_closed.back());
    m_closed.pop_back();
    return t;
}

void App::sessionChanged()
{
    if (m_timerWindow)
        SetTimer(m_timerWindow, kSaveTimer, kSaveDelayMs, nullptr);
}

void App::saveSessionNow()
{
    if (!m_session)
        return;
    std::vector<services::SessionWindow> snapshot;
    if (m_windows.empty())
        snapshot = m_lastWindowSnapshot;
    else
        for (auto* w : m_windows)
            if (w->tabCount())
                snapshot.push_back(w->sessionSnapshot());
    try {
        m_session->save(snapshot);
        size_t tabs = 0;
        for (auto& w : snapshot)
            tabs += w.tabs.size();
        platform::log(L"session saved: %zu windows, %zu tabs", snapshot.size(), tabs);
    } catch (const std::exception& e) {
        platform::log(L"session save FAILED: %hs", e.what());
    }
}

} // namespace meridian::app
