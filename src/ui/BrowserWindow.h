#pragma once
// ui/: top-level browser window. Chrome is custom-drawn with Direct2D/DirectWrite
// over a DWM Mica backdrop. The GPU glass pipeline replaces this in a later
// milestone; see docs/GLASS_NOTES.md.
#include "Chrome.h"
#include "HistoryView.h"
#include "StartPage.h"
#include "SuggestionPopup.h"
#include "Tab.h"
#include "TabOverview.h"
#include "TabStrip.h"
#include "services/SessionStore.h"
#include "services/Suggestions.h"

#include <map>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>

namespace meridian::app {
class App;
}

namespace meridian::ui {

class BrowserWindow {
public:
    // restore: tabs to recreate (lazily). bounds: initial window rect (screen).
    static BrowserWindow* create(app::App&, const services::SessionWindow* restore, const RECT* bounds, bool addDefaultTab);
    HWND hwnd() const { return m_hwnd; }

    bool handleShortcut(const MSG&);
    services::SessionWindow sessionSnapshot() const;

    Tab* openTab(const std::wstring& url, bool activate, int index = -1);
    void attachTab(std::unique_ptr<Tab>, int index, bool activate);
    int tabCount() const { return int(m_tabs.size()); }
    int activeIndex() const { return m_active; }
    const Tab& tab(int i) const { return *m_tabs[i]; }

private:
    enum class Hit { None, Back, Forward, Reload, Address, NewTab, Overview, Caption, Strip, Content };

    BrowserWindow(app::App&);
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK addressProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT handle(UINT, WPARAM, LPARAM);

    // Tabs
    Tab* active() { return m_active >= 0 && m_active < int(m_tabs.size()) ? m_tabs[m_active].get() : nullptr; }
    Tab* findTab(uint64_t id, int* index = nullptr);
    void ensureView(Tab&);
    void wire(Tab&);
    void activate(int index);
    void closeTab(int index);
    std::unique_ptr<Tab> detachTab(int index);
    void moveTabToNewWindow(int index, POINT screen);
    void togglePin(int index);
    void duplicateTab(int index);
    void reopenClosedTab();
    void showTabMenu(int index, POINT screen);
    void captureThumbnail(Tab&, bool force);
    void tabsChanged();

    void automate(const std::wstring& command); // see App::enableAutomation
    void dumpUi(const std::wstring& path);      // automation: field, suggestions, start page, history

    // The engine context this window's tabs use. Private windows (M6) will
    // have their own; history, icons and search suggestions are off for those.
    engine::EngineContext& context();

    // Content area: the active tab's page, or one of the native views.
    bool showingStartPage() const;
    void updateContentView();
    void focusContent();
    void refreshStartPage();
    void showStartPageMenu(POINT screen, StartPage::Hit);

    // History view (Ctrl+H)
    void setHistory(bool open);
    void refreshHistory();
    void openHistoryEntry(int index, bool newTab);
    void deleteHistoryEntry(int index);
    void clearHistory();
    void showHistoryMenu(int index, POINT screen);

    // Bookmarks
    void addBookmark();                                // Ctrl+D: current page to Favorites
    void importBookmarks(std::wstring path = {});      // empty: ask with a file dialog
    void exportBookmarks(std::wstring path = {});
    ID2D1Bitmap* siteIconBitmap(const std::wstring& host);

    // Smart Search
    void navigate(const std::wstring& url, bool typed);
    std::wstring addressText() const;
    void setAddressText(const std::wstring&);         // caret at the end; not treated as typing
    void onAddressEdited();                            // EN_CHANGE from the user
    void refreshSuggestions(bool allowCompletion);
    void showSuggestions(int selected);
    void hideSuggestions();
    void moveSuggestionSelection(int delta);
    void activateSuggestion(int index);
    void removeSelectedSuggestionFromHistory();
    bool handleAddressKey(WPARAM vk, bool shift);     // true if consumed

    // Overview
    void setOverview(bool open);
    void moveOverviewFocus(int dx, int dy);

    // Chrome
    void onCreate();
    void layout();
    void paint();
    void createDeviceResources();
    void discardDeviceResources();
    void updateTheme();
    void updateWindowTitle();
    void syncAddressFromTab();
    void showBanner(Tab&, const std::wstring&);
    bool stripVisible() const;
    float chromeHeightDip() const;
    D2D1_RECT_F contentRectDip() const;
    RECT contentRectPx() const;
    float dpiScale() const { return m_dpi / 96.0f; }
    int px(float dip) const { return static_cast<int>(dip * dpiScale() + 0.5f); }
    D2D1_RECT_F addressRectDip() const;
    D2D1_RECT_F toolbarButtonRect(Hit) const;
    Hit hitTest(POINT clientPx) const;
    void navigateFromAddressBar();
    void beginEditingAddress(bool selectAll);
    void endEditingAddress(bool revert);

    app::App& m_app;
    HWND m_hwnd { nullptr };
    HWND m_address { nullptr };
    std::vector<std::unique_ptr<Tab>> m_tabs;
    int m_active { -1 };
    platform::ThemeState m_theme;
    UINT m_dpi { 96 };

    TabStrip m_strip;
    TabStrip::State m_stripState;
    struct Drag {
        int index { -1 };
        POINT start {};
        bool moved { false };
        bool tearOff { false };
    } m_drag;

    SuggestionPopup m_popup;
    services::SuggestionList m_suggestions;
    std::wstring m_typed;              // what the user typed, without the inline completion
    bool m_settingAddress { false };   // our own SetWindowText: not typing
    bool m_deleteKey { false };        // the next edit came from Backspace/Delete: don't complete

    StartPage m_startPage;
    StartPage::Hit m_startHover;

    HistoryView m_historyView;
    bool m_historyOpen { false };
    HWND m_historySearch { nullptr };
    int m_historyHover { -1 };
    int m_historySelected { -1 };

    std::map<std::wstring, ComPtr<ID2D1Bitmap>> m_siteIcons; // per render target

    TabOverview m_overview;
    bool m_overviewOpen { false };
    int m_overviewHover { -1 };
    bool m_overviewHoverClose { false };
    int m_overviewFocus { -1 };

    bool m_editing { false };
    Hit m_hover { Hit::None }, m_pressed { Hit::None };
    bool m_closing { false };

    ComPtr<ID2D1Factory1> m_d2d;
    ComPtr<IDWriteFactory3> m_dwrite;
    ComPtr<ID2D1HwndRenderTarget> m_rt;
    ComPtr<IDWriteTextFormat> m_hostFormat, m_tabFormat, m_cardTitleFormat, m_cardBigFormat;
    ComPtr<IDWriteTextFormat> m_headingFormat, m_letterFormat, m_listFormat;
    HFONT m_editFont { nullptr };
    HBRUSH m_editBrush { nullptr };
};

} // namespace meridian::ui
