#pragma once
// ui/: top-level browser window. Chrome is custom-drawn with Direct2D/DirectWrite
// over a DWM Mica backdrop. The GPU glass pipeline replaces this in a later
// milestone; see docs/GLASS_NOTES.md.
#include "Chrome.h"
#include "Tab.h"
#include "TabOverview.h"
#include "TabStrip.h"
#include "services/SessionStore.h"

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
    HFONT m_editFont { nullptr };
    HBRUSH m_editBrush { nullptr };
};

} // namespace meridian::ui
