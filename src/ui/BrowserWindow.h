#pragma once
// ui/: top-level browser window. Chrome is custom-drawn with Direct2D/DirectWrite
// over a DWM Mica backdrop (M2). The GPU glass pipeline replaces this in a later
// milestone; see docs/GLASS_NOTES.md.
#include "engine/WebView.h"
#include "platform/SystemTheme.h"

#include <d2d1_1.h>
#include <dwrite_3.h>
#include <memory>
#include <string>
#include <windows.h>
#include <wrl/client.h>

namespace meridian::ui {

using Microsoft::WRL::ComPtr;

class BrowserWindow {
public:
    static BrowserWindow* create(engine::EngineContext&, const std::wstring& initialURL);
    HWND hwnd() const { return m_hwnd; }

    // Returns true if the key was consumed (called from the message loop
    // before dispatch, so shortcuts work while the page has focus).
    bool handleShortcut(const MSG&);

private:
    enum class Hit { None, Back, Forward, Reload, Address, Caption };

    explicit BrowserWindow(engine::EngineContext&);
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK addressProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT handle(UINT, WPARAM, LPARAM);

    void onCreate();
    void layout();
    void paint();
    void createDeviceResources();
    void discardDeviceResources();
    void updateTheme();

    float dpiScale() const { return m_dpi / 96.0f; }
    int px(float dip) const { return static_cast<int>(dip * dpiScale() + 0.5f); }
    D2D1_RECT_F addressRectDip() const;
    Hit hitTest(POINT clientPx) const;

    void navigateFromAddressBar();
    void beginEditingAddress(bool selectAll);
    void endEditingAddress(bool revert);
    void showBanner(const std::wstring& text);

    engine::EngineContext& m_engine;
    HWND m_hwnd { nullptr };
    HWND m_address { nullptr };
    std::unique_ptr<engine::WebView> m_view;
    platform::ThemeState m_theme;
    UINT m_dpi { 96 };

    // State mirrored from the page for drawing.
    std::wstring m_url, m_title, m_banner;
    double m_progress { 0 };
    bool m_loading { false };
    bool m_editing { false };
    Hit m_hover { Hit::None }, m_pressed { Hit::None };

    // Direct2D / DirectWrite.
    ComPtr<ID2D1Factory1> m_d2d;
    ComPtr<IDWriteFactory3> m_dwrite;
    ComPtr<ID2D1HwndRenderTarget> m_rt;
    ComPtr<IDWriteTextFormat> m_hostFormat;
    HFONT m_editFont { nullptr };
    HBRUSH m_editBrush { nullptr };
};

} // namespace meridian::ui
