#include "TabStrip.h"

#include "services/UrlFixup.h"

#include <algorithm>

namespace meridian::ui {

std::wstring Tab::displayTitle() const
{
    if (!title.empty())
        return title;
    if (url.empty() || url == L"about:blank")
        return L"New Tab";
    return services::displayHost(url);
}

static constexpr float kInset = 8;
static constexpr float kVPad = 4;
static constexpr float kCloseSize = 18;

void TabStrip::layout(const std::vector<std::unique_ptr<Tab>>& tabs, float top, float width)
{
    m_bounds = D2D1::RectF(0, top, width, top + tokens::kTabStripHeight);
    m_rects.clear();
    int pinned = 0;
    for (auto& t : tabs)
        pinned += t->pinned;
    int normal = int(tabs.size()) - pinned;
    float x = kInset;
    float available = width - 2 * kInset - pinned * tokens::kPinnedTabWidth;
    float w = normal ? std::max(36.f, available / normal) : 0;
    for (auto& t : tabs) {
        float tw = t->pinned ? tokens::kPinnedTabWidth : w;
        m_rects.push_back(D2D1::RectF(x, top + kVPad, x + tw, top + tokens::kTabStripHeight - kVPad));
        x += tw;
    }
}

static bool inside(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

static D2D1_RECT_F closeRect(const D2D1_RECT_F& tab)
{
    float cy = (tab.top + tab.bottom) / 2;
    return D2D1::RectF(tab.left + 7, cy - kCloseSize / 2, tab.left + 7 + kCloseSize, cy + kCloseSize / 2);
}

TabStrip::Hit TabStrip::hitTest(float x, float y) const
{
    if (!inside(m_bounds, x, y))
        return {};
    for (int i = 0; i < int(m_rects.size()); ++i) {
        auto r = m_rects[i];
        r.top = m_bounds.top;
        r.bottom = m_bounds.bottom;
        if (inside(r, x, y)) {
            bool pinnedTab = (m_rects[i].right - m_rects[i].left) <= tokens::kPinnedTabWidth + 0.5f;
            return { i, !pinnedTab && (m_rects[i].right - m_rects[i].left) > 60 && inside(closeRect(m_rects[i]), x, y) };
        }
    }
    return {};
}

int TabStrip::dropIndex(const std::vector<std::unique_ptr<Tab>>& tabs, int dragging, float x) const
{
    // Pinned tabs reorder among pinned tabs; normal among normal.
    bool pinned = tabs[dragging]->pinned;
    int first = -1, last = -1;
    for (int i = 0; i < int(tabs.size()); ++i)
        if (tabs[i]->pinned == pinned) {
            if (first < 0)
                first = i;
            last = i;
        }
    int target = first;
    for (int i = first; i <= last; ++i) {
        if (i == dragging)
            continue;
        float mid = (m_rects[i].left + m_rects[i].right) / 2;
        if (x > mid)
            target = i < dragging ? i + 1 : i;
    }
    return std::clamp(target, first, last);
}

void TabStrip::paintTab(Painter& p, const Palette& pal, IDWriteTextFormat* format, Tab& tab, D2D1_RECT_F r,
    bool active, bool hover, bool hoverClose) const
{
    auto pill = D2D1::RoundedRect(D2D1::RectF(r.left + 1, r.top, r.right - 1, r.bottom), tokens::kTabRadius, tokens::kTabRadius);
    if (active)
        p.fill(pill, pal.tabActive);
    else if (hover)
        p.fill(pill, pal.hover);

    float cy = (r.top + r.bottom) / 2;
    float width = r.right - r.left;
    bool compact = tab.pinned || width < 60;

    auto drawIcon = [&](float x) {
        D2D1_POINT_2F c = D2D1::Point2F(x + tokens::kFaviconSize / 2, cy);
        if (tab.loading) {
            p.spinner(c, 6, tab.progress, pal.accent);
        } else if (tab.faviconBitmap) {
            float h = tokens::kFaviconSize / 2;
            p.rt->DrawBitmap(tab.faviconBitmap.Get(), D2D1::RectF(c.x - h, c.y - h, c.x + h, c.y + h), 1.0f,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            p.globe(c, 6.5f, pal.secondary);
        }
    };

    if (compact) {
        drawIcon((r.left + r.right) / 2 - tokens::kFaviconSize / 2);
        return;
    }

    // Favicon + title, centered as a group (Safari centers tab titles).
    std::wstring title = tab.displayTitle();
    float textMax = std::max(10.f, width - 2 * (kCloseSize + 12) - tokens::kFaviconSize - 6);
    ComPtr<IDWriteTextLayout> layout;
    p.dwrite->CreateTextLayout(title.c_str(), UINT32(title.size()), format, textMax, r.bottom - r.top, layout.GetAddressOf());
    DWRITE_TEXT_METRICS m {};
    layout->GetMetrics(&m);
    float group = tokens::kFaviconSize + 6 + std::min(m.widthIncludingTrailingWhitespace, textMax);
    float x = std::max(r.left + kCloseSize + 12, (r.left + r.right) / 2 - group / 2);
    drawIcon(x);
    p.brush->SetColor(active ? pal.text : pal.secondary);
    p.rt->DrawTextLayout(D2D1::Point2F(x + tokens::kFaviconSize + 6, r.top), layout.Get(), p.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);

    if (hover) {
        auto cr = closeRect(r);
        D2D1_POINT_2F c = D2D1::Point2F((cr.left + cr.right) / 2, (cr.top + cr.bottom) / 2);
        if (hoverClose)
            p.fill(D2D1::RoundedRect(cr, kCloseSize / 2, kCloseSize / 2), pal.pressed);
        p.cross(c, 3.6f, 1.5f, pal.text);
    }
}

void TabStrip::paint(Painter& p, const Palette& pal, IDWriteTextFormat* format, std::vector<std::unique_ptr<Tab>>& tabs,
    const State& s) const
{
    p.fill(m_bounds, pal.stripBackground);
    // Separators between inactive, non-hovered neighbours.
    for (int i = 1; i < int(m_rects.size()); ++i) {
        bool skip = i == s.active || i - 1 == s.active || i == s.hover || i - 1 == s.hover || i == s.dragging || i - 1 == s.dragging;
        if (skip)
            continue;
        float x = m_rects[i].left;
        float cy = (m_rects[i].top + m_rects[i].bottom) / 2;
        p.brush->SetColor(pal.tabSeparator);
        p.rt->DrawLine(D2D1::Point2F(x, cy - 7), D2D1::Point2F(x, cy + 7), p.brush, 1.0f);
    }
    for (int i = 0; i < int(m_rects.size()); ++i) {
        if (i == s.dragging)
            continue;
        paintTab(p, pal, format, *tabs[i], m_rects[i], i == s.active, i == s.hover, i == s.hover && s.hoverClose);
    }
    if (s.dragging >= 0 && s.dragging < int(m_rects.size())) {
        auto r = m_rects[s.dragging];
        float w = r.right - r.left;
        float left = std::clamp(s.dragX - s.dragGrabOffset, m_bounds.left, m_bounds.right - w);
        D2D1_RECT_F moved = D2D1::RectF(left, r.top, left + w, r.bottom);
        auto shadow = moved;
        shadow.top += 1.5f;
        shadow.bottom += 1.5f;
        p.fill(D2D1::RoundedRect(shadow, tokens::kTabRadius, tokens::kTabRadius), pal.shadow);
        paintTab(p, pal, format, *tabs[s.dragging], moved, true, false, false);
    }
}

} // namespace meridian::ui
