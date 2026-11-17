#pragma once
// app/: process-wide state: engine context, windows, session persistence,
// favicon cache and the recently-closed-tabs stack.
#include "engine/WebView.h"
#include "services/Database.h"
#include "services/Bookmarks.h"
#include "services/Favicons.h"
#include "services/History.h"
#include "services/IconStore.h"
#include "services/Reader.h"
#include "services/ReadingList.h"
#include "services/SearchSuggestions.h"
#include "services/SessionStore.h"
#include "services/Settings.h"
#include "services/TabGroups.h"

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

    // Profile data. Null if the profile database could not be opened; every
    // caller must cope (browsing works without them). Service calls can throw
    // on SQLite errors, so UI code goes through the wrappers below or catches.
    services::History* history() { return m_history.get(); }
    services::Bookmarks* bookmarks() { return m_bookmarks.get(); }
    services::Settings* settings() { return m_settings.get(); }
    services::IconStore* icons() { return m_icons.get(); }
    services::ReadingList* readingList() { return m_readingList.get(); }
    services::TabGroups* tabGroups() { return m_tabGroups.get(); }
    services::SearchSuggestions& searchSuggestions() { return m_searchSuggestions; }

    // No-ops for private (ephemeral) engine contexts.
    void recordVisit(const engine::EngineContext&, const std::wstring& url, bool typed);
    void updateHistoryTitle(const engine::EngineContext&, const std::wstring& url, const std::wstring& title);
    void storeIcon(const engine::EngineContext&, const std::wstring& host, const services::Bitmap&);
    const services::Bitmap& siteIcon(const std::wstring& host); // empty bitmap if unknown
    bool searchSuggestionsEnabled();
    void setSearchSuggestionsEnabled(bool);

    static void writeTextFile(const std::wstring& path, const std::wstring& text); // UTF-8

    // Reader appearance (shared by all windows; persisted in settings).
    services::ReaderStyle readerStyle();
    void setReaderStyle(const services::ReaderStyle&);

    // Tab Groups: a group is shown by at most one window at a time.
    ui::BrowserWindow* windowShowingGroup(int64_t groupId, const ui::BrowserWindow* except = nullptr);
    // Bookmarks, Reading List or Tab Groups changed: every window refreshes
    // its sidebar and start page.
    void libraryChanged();

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
    std::unique_ptr<services::History> m_history;
    std::unique_ptr<services::Bookmarks> m_bookmarks;
    std::unique_ptr<services::Settings> m_settings;
    std::unique_ptr<services::IconStore> m_icons;
    std::unique_ptr<services::ReadingList> m_readingList;
    std::unique_ptr<services::TabGroups> m_tabGroups;
    services::SearchSuggestions m_searchSuggestions;
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
