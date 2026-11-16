#pragma once
// app/: process-wide state: engine context, windows, session persistence,
// favicon cache and the recently-closed-tabs stack.
#include "engine/WebView.h"
#include "services/Database.h"
#include "services/Favicons.h"
#include "services/SessionStore.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>

namespace meridian::ui {
class BrowserWindow;
struct Tab;
}

namespace meridian::app {

struct ClosedTab {
    std::wstring url, title;
    std::vector<uint8_t> state;
    bool pinned { false };
};

class App {
public:
    static App& instance();

    int run(const std::wstring& initialURL);

    // Test automation over WM_COPYDATA. Only enabled by the --automation
    // command-line flag; off by default so other processes can't drive tabs.
    void enableAutomation() { m_automation = true; }
    void setProfileDirectory(const std::filesystem::path&); // --profile=<dir>; default %LOCALAPPDATA%\Meridian\Default
    bool automationEnabled() const { return m_automation; }
    void dumpState(const std::wstring& path);

    engine::EngineContext& engine() { return *m_engine; }
    services::FaviconLoader& favicons() { return m_favicons; }

    ui::BrowserWindow* newWindow(const services::SessionWindow* restore, const RECT* bounds, bool addDefaultTab);
    void windowClosing(ui::BrowserWindow*);   // before its tabs are torn down
    void windowDestroyed(ui::BrowserWindow*);
    const std::vector<ui::BrowserWindow*>& windows() const { return m_windows; }

    void pushClosedTab(ClosedTab);
    std::optional<ClosedTab> popClosedTab();

    // Debounced session save (call after any change worth persisting).
    void sessionChanged();
    void saveSessionNow();

    uint64_t nextTabId() { return ++m_lastTabId; }

private:
    App() = default;
    bool routeMessage(MSG&);
    static LRESULT CALLBACK timerProc(HWND, UINT, WPARAM, LPARAM);

    std::unique_ptr<engine::EngineContext> m_engine;
    std::unique_ptr<services::Database> m_db;
    std::unique_ptr<services::SessionStore> m_session;
    services::FaviconLoader m_favicons;
    std::vector<ui::BrowserWindow*> m_windows;
    std::vector<ClosedTab> m_closed;
    std::vector<services::SessionWindow> m_lastWindowSnapshot; // session of the last window to close
    HWND m_timerWindow { nullptr };
    uint64_t m_lastTabId { 0 };
    bool m_automation { false };
    std::filesystem::path m_profile;
};

} // namespace meridian::app
