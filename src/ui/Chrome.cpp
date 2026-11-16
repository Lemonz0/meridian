#include "Chrome.h"

#include <cmath>

namespace meridian::ui {

static D2D1_COLOR_F rgba(UINT32 rgb, float a = 1) { return D2D1::ColorF(rgb, a); }

static D2D1_COLOR_F sys(int index, float a = 1)
{
    COLORREF c = GetSysColor(index);
    return D2D1::ColorF(GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f, a);
}

Palette paletteFor(const platform::ThemeState& t)
{
    if (t.highContrast) {
        auto bg = sys(COLOR_WINDOW), fg = sys(COLOR_WINDOWTEXT), hi = sys(COLOR_HIGHLIGHT);
        return { bg, bg, bg, fg, sys(COLOR_GRAYTEXT), sys(COLOR_HIGHLIGHT, 0.3f), sys(COLOR_HIGHLIGHT, 0.5f), hi,
                 sys(COLOR_INFOBK), bg, sys(COLOR_HIGHLIGHT, 0.25f), fg, bg, bg, rgba(0, 0) };
    }
    if (t.dark) {
        return { rgba(0x202022), rgba(0xFFFFFF, 0.08f), rgba(0x2C2C2E), rgba(0xF5F5F7), rgba(0xF5F5F7, 0.55f),
                 rgba(0xFFFFFF, 0.08f), rgba(0xFFFFFF, 0.14f), rgba(0x3B8CFF), rgba(0x3A3A3C),
                 rgba(0x000000, 0.28f), rgba(0xFFFFFF, 0.11f), rgba(0xFFFFFF, 0.12f),
                 rgba(0x161618), rgba(0x2A2A2C), rgba(0x000000, 0.5f) };
    }
    return { rgba(0xF3F3F5), rgba(0x000000, 0.055f), rgba(0xFFFFFF), rgba(0x1D1D1F), rgba(0x1D1D1F, 0.5f),
             rgba(0x000000, 0.06f), rgba(0x000000, 0.11f), rgba(0x0A6CFF), rgba(0xFFF4D6),
             rgba(0x000000, 0.06f), rgba(0xFFFFFF, 0.85f), rgba(0x000000, 0.12f),
             rgba(0xE9E9EC), rgba(0xFFFFFF), rgba(0x000000, 0.18f) };
}

COLORREF toColorRef(const D2D1_COLOR_F& c)
{
    return RGB(BYTE(c.r * 255), BYTE(c.g * 255), BYTE(c.b * 255));
}

static ComPtr<ID2D1StrokeStyle> roundStroke(ID2D1Factory* f)
{
    ComPtr<ID2D1StrokeStyle> style;
    f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
        D2D1_LINE_JOIN_ROUND), nullptr, 0, style.GetAddressOf());
    return style;
}

void Painter::fill(const D2D1_ROUNDED_RECT& r, const D2D1_COLOR_F& c)
{
    brush->SetColor(c);
    rt->FillRoundedRectangle(r, brush);
}

void Painter::fill(const D2D1_RECT_F& r, const D2D1_COLOR_F& c)
{
    brush->SetColor(c);
    rt->FillRectangle(r, brush);
}

void Painter::chevron(D2D1_RECT_F r, bool left, const D2D1_COLOR_F& c)
{
    float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, s = 5.5f;
    float dir = left ? 1.f : -1.f;
    ComPtr<ID2D1PathGeometry> g;
    factory->CreatePathGeometry(g.GetAddressOf());
    ComPtr<ID2D1GeometrySink> sink;
    g->Open(sink.GetAddressOf());
    sink->BeginFigure(D2D1::Point2F(cx + dir * s * 0.55f, cy - s), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(D2D1::Point2F(cx - dir * s * 0.55f, cy));
    sink->AddLine(D2D1::Point2F(cx + dir * s * 0.55f, cy + s));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    brush->SetColor(c);
    rt->DrawGeometry(g.Get(), brush, tokens::kGlyphStroke + 0.3f, roundStroke(factory).Get());
}

void Painter::reload(D2D1_POINT_2F c, const D2D1_COLOR_F& color)
{
    const float r = 5.6f;
    ComPtr<ID2D1PathGeometry> g;
    factory->CreatePathGeometry(g.GetAddressOf());
    ComPtr<ID2D1GeometrySink> sink;
    g->Open(sink.GetAddressOf());
    auto at = [&](float deg) {
        float rad = deg * 3.14159265f / 180;
        return D2D1::Point2F(c.x + r * cosf(rad), c.y + r * sinf(rad));
    };
    sink->BeginFigure(at(-60), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(at(-110), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    auto tip = at(-60);
    sink->BeginFigure(D2D1::Point2F(tip.x - 3.6f, tip.y - 1.2f), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(tip);
    sink->AddLine(D2D1::Point2F(tip.x + 0.6f, tip.y - 3.8f));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    brush->SetColor(color);
    rt->DrawGeometry(g.Get(), brush, tokens::kGlyphStroke - 0.2f, roundStroke(factory).Get());
}

void Painter::cross(D2D1_POINT_2F c, float s, float stroke, const D2D1_COLOR_F& color)
{
    brush->SetColor(color);
    auto style = roundStroke(factory);
    rt->DrawLine(D2D1::Point2F(c.x - s, c.y - s), D2D1::Point2F(c.x + s, c.y + s), brush, stroke, style.Get());
    rt->DrawLine(D2D1::Point2F(c.x - s, c.y + s), D2D1::Point2F(c.x + s, c.y - s), brush, stroke, style.Get());
}

void Painter::lock(D2D1_POINT_2F c, const D2D1_COLOR_F& color)
{
    brush->SetColor(color);
    rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(c.x - 4, c.y - 1, c.x + 4, c.y + 5.5f), 1.5f, 1.5f), brush);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(c.x - 2.6f, c.y - 5.5f, c.x + 2.6f, c.y + 1.5f), 2.6f, 2.6f), brush, 1.4f);
}

void Painter::plus(D2D1_POINT_2F c, float s, const D2D1_COLOR_F& color)
{
    brush->SetColor(color);
    auto style = roundStroke(factory);
    rt->DrawLine(D2D1::Point2F(c.x - s, c.y), D2D1::Point2F(c.x + s, c.y), brush, tokens::kGlyphStroke, style.Get());
    rt->DrawLine(D2D1::Point2F(c.x, c.y - s), D2D1::Point2F(c.x, c.y + s), brush, tokens::kGlyphStroke, style.Get());
}

void Painter::tabGrid(D2D1_POINT_2F c, const D2D1_COLOR_F& color)
{
    // 2x2 rounded squares: "show all tabs".
    brush->SetColor(color);
    const float s = 5.2f, gap = 1.6f;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            float l = c.x - s - gap / 2 + x * (s + gap), t = c.y - s - gap / 2 + y * (s + gap);
            rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(l, t, l + s, t + s), 1.4f, 1.4f), brush, 1.3f);
        }
}

void Painter::globe(D2D1_POINT_2F c, float r, const D2D1_COLOR_F& color)
{
    brush->SetColor(color);
    rt->DrawEllipse(D2D1::Ellipse(c, r, r), brush, 1.2f);
    rt->DrawEllipse(D2D1::Ellipse(c, r * 0.42f, r), brush, 1.0f);
    rt->DrawLine(D2D1::Point2F(c.x - r, c.y), D2D1::Point2F(c.x + r, c.y), brush, 1.0f);
}

void Painter::spinner(D2D1_POINT_2F c, float r, double progress, const D2D1_COLOR_F& color)
{
    // Determinate ring: faint track plus an arc for the estimated progress.
    auto track = color;
    track.a *= 0.25f;
    brush->SetColor(track);
    rt->DrawEllipse(D2D1::Ellipse(c, r, r), brush, 2.0f);
    float sweep = float(std::fmax(0.08, std::fmin(progress, 0.999)) * 360.0);
    ComPtr<ID2D1PathGeometry> g;
    factory->CreatePathGeometry(g.GetAddressOf());
    ComPtr<ID2D1GeometrySink> sink;
    g->Open(sink.GetAddressOf());
    auto at = [&](float deg) {
        float rad = (deg - 90) * 3.14159265f / 180;
        return D2D1::Point2F(c.x + r * cosf(rad), c.y + r * sinf(rad));
    };
    sink->BeginFigure(at(0), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(at(sweep), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE,
        sweep > 180 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    brush->SetColor(color);
    rt->DrawGeometry(g.Get(), brush, 2.0f, roundStroke(factory).Get());
}

ComPtr<ID2D1Bitmap> makeBitmap(ID2D1RenderTarget* rt, const services::Bitmap& bmp)
{
    ComPtr<ID2D1Bitmap> out;
    if (!bmp)
        return out;
    auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    rt->CreateBitmap(D2D1::SizeU(bmp.width, bmp.height), bmp.bgra.data(), bmp.width * 4, props, out.GetAddressOf());
    return out;
}

} // namespace meridian::ui
