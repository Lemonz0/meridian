#pragma once
// ui/: the Smart Search dropdown under the address field. A non-activating
// popup window, so the address EDIT keeps keyboard focus while it is open;
// the browser window drives all keyboard handling and owns the data.
#include "Chrome.h"
#include "services/Suggestions.h"

#include <functional>
#include <map>
#include <string>
#include <vector>
#include <windows.h>

namespace meridian::ui {

class SuggestionPopup {
public:
    struct Callbacks {
        std::function<void(int index)> activate;                    // row clicked
        std::function<const services::Bitmap&(const std::wstring& host)> icon;
    };

    void create(HWND owner, Callbacks);
    void destroy();

    // `field` is the address field in screen pixels; the popup opens below it.
    void show(const RECT& field, UINT dpi, const platform::ThemeState&, std::vector<services::Suggestion>, int selected);
    void setSelected(int index);
    void hide();
    bool visible() const { return m_hwnd && IsWindowVisible(m_hwnd); }
    HWND hwnd() const { return m_hwnd; }

    const std::vector<services::Suggestion>& items() const { return m_items; }
    int selected() const { return m_selected; }

private:
    struct Row {
        int item { -1 };          // -1 for a section header
        std::wstring header;
        D2D1_RECT_F rect {};
    };

    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);
    void buildRows();
    float contentHeight() const;
    int itemAt(float x, float y) const;
    void paint();
    void discardDeviceResources();
    float dpiScale() const { return m_dpi / 96.0f; }
    ID2D1Bitmap* iconFor(const std::wstring& host);

    HWND m_hwnd { nullptr };
    HWND m_owner { nullptr };
    Callbacks m_callbacks;
    std::vector<services::Suggestion> m_items;
    std::vector<Row> m_rows;
    int m_selected { -1 };
    int m_hover { -1 };
    UINT m_dpi { 96 };
    platform::ThemeState m_theme;

    ComPtr<ID2D1Factory1> m_d2d;
    ComPtr<IDWriteFactory3> m_dwrite;
    ComPtr<ID2D1HwndRenderTarget> m_rt;
    ComPtr<IDWriteTextFormat> m_titleFormat, m_detailFormat, m_headerFormat;
    std::map<std::wstring, ComPtr<ID2D1Bitmap>> m_icons; // per render target
};

} // namespace meridian::ui
