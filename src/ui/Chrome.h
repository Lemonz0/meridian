#pragma once
// ui/: shared design tokens, palette and vector glyphs for the browser chrome.
// Glyphs are original vector drawings (no SF Symbols); text uses Segoe UI Variable.
#include "platform/SystemTheme.h"
#include "services/Favicons.h"

#include <d2d1_1.h>
#include <dwrite_3.h>
#include <wrl/client.h>

namespace meridian::ui {

using Microsoft::WRL::ComPtr;

namespace tokens {
constexpr float kToolbarHeight = 52;
constexpr float kTabStripHeight = 34;
constexpr float kButtonSize = 30;
constexpr float kButtonRadius = 8;
constexpr float kLeftInset = 12;
constexpr float kCaptionReserve = 140;   // system caption buttons (min/max/close)
constexpr float kFieldHeight = 32;
constexpr float kFieldMaxWidth = 640;
constexpr float kFieldMinWidth = 220;
constexpr float kGlyphStroke = 1.7f;
constexpr float kBannerHeight = 32;
constexpr float kBodyFontSize = 13.5f;
constexpr float kTabFontSize = 12.5f;
constexpr float kPinnedTabWidth = 44;
constexpr float kTabMinWidth = 96;
constexpr float kTabRadius = 8;
constexpr float kFaviconSize = 16;
constexpr float kOverviewCardWidth = 280;
constexpr float kOverviewGap = 28;
constexpr float kOverviewRadius = 12;
}

struct Palette {
    D2D1_COLOR_F toolbarOpaque, fieldIdle, fieldFocused, text, secondary, hover, pressed, accent, banner;
    D2D1_COLOR_F stripBackground, tabActive, tabSeparator, overviewBackground, cardBackground, shadow;
};

Palette paletteFor(const platform::ThemeState&);
COLORREF toColorRef(const D2D1_COLOR_F&);

// Per-render-target drawing helpers.
struct Painter {
    ID2D1RenderTarget* rt;
    ID2D1Factory* factory;
    ID2D1SolidColorBrush* brush;
    IDWriteFactory* dwrite;

    void fill(const D2D1_ROUNDED_RECT&, const D2D1_COLOR_F&);
    void fill(const D2D1_RECT_F&, const D2D1_COLOR_F&);
    void chevron(D2D1_RECT_F box, bool pointsLeft, const D2D1_COLOR_F&);
    void reload(D2D1_POINT_2F center, const D2D1_COLOR_F&);
    void cross(D2D1_POINT_2F center, float half, float stroke, const D2D1_COLOR_F&);
    void lock(D2D1_POINT_2F center, const D2D1_COLOR_F&);
    void plus(D2D1_POINT_2F center, float half, const D2D1_COLOR_F&);
    void tabGrid(D2D1_POINT_2F center, const D2D1_COLOR_F&);
    void globe(D2D1_POINT_2F center, float radius, const D2D1_COLOR_F&); // fallback favicon
    void spinner(D2D1_POINT_2F center, float radius, double progress, const D2D1_COLOR_F&);
};

ComPtr<ID2D1Bitmap> makeBitmap(ID2D1RenderTarget*, const services::Bitmap&);

} // namespace meridian::ui
