#include "TabOverview.h"

#include "services/UrlFixup.h"

#include <algorithm>
#include <cmath>

namespace meridian::ui {

static constexpr float kTitleHeight = 28;
static constexpr float kMargin = 36;
static constexpr float kCloseSize = 22;

void TabOverview::layout(int count, D2D1_RECT_F area, float aspect)
{
    m_area = area;
    m_count = count;
    m_aspect = std::clamp(aspect, 0.8f, 2.4f);
    relayout();
}

void TabOverview::scrollBy(float dy)
{
    m_scroll += dy;
    relayout();
}

void TabOverview::scrollToShow(int index)
{
    if (index < 0 || index >= int(m_cards.size()))
        return;
    auto c = m_cards[index];
    if (c.top - kTitleHeight < m_area.top)
        m_scroll -= m_area.top - (c.top - kTitleHeight) + kMargin / 2;
    else if (c.bottom > m_area.bottom)
        m_scroll += c.bottom - m_area.bottom + kMargin / 2;
    relayout();
}

void TabOverview::relayout()
{
    D2D1_RECT_F area = m_area;
    int count = m_count;
    float aspect = m_aspect;
    m_cards.clear();
    float width = area.right - area.left - 2 * kMargin;
    m_columns = std::max(1, int((width + tokens::kOverviewGap) / (tokens::kOverviewCardWidth + tokens::kOverviewGap)));
    m_columns = std::min(m_columns, std::max(1, count));
    float cardW = std::min(360.f, (width - (m_columns - 1) * tokens::kOverviewGap) / m_columns);
    float cardH = cardW / aspect;
    float rowH = cardH + kTitleHeight + tokens::kOverviewGap;
    float gridW = m_columns * cardW + (m_columns - 1) * tokens::kOverviewGap;
    float left = area.left + (area.right - area.left - gridW) / 2;
    int rows = (count + m_columns - 1) / std::max(1, m_columns);
    m_contentHeight = 2 * kMargin + rows * rowH - tokens::kOverviewGap;
    float maxScroll = std::max(0.f, m_contentHeight - (area.bottom - area.top));
    m_scroll = std::clamp(m_scroll, 0.f, maxScroll);
    for (int i = 0; i < count; ++i) {
        int row = i / m_columns, col = i % m_columns;
        float x = left + col * (cardW + tokens::kOverviewGap);
        float y = area.top + kMargin + kTitleHeight + row * rowH - m_scroll;
        m_cards.push_back(D2D1::RectF(x, y, x + cardW, y + cardH));
    }
}

static D2D1_RECT_F closeRect(const D2D1_RECT_F& card)
{
    return D2D1::RectF(card.left + 8, card.top + 8, card.left + 8 + kCloseSize, card.top + 8 + kCloseSize);
}

TabOverview::Hit TabOverview::hitTest(float x, float y) const
{
    for (int i = 0; i < int(m_cards.size()); ++i) {
        auto c = m_cards[i];
        auto withTitle = D2D1::RectF(c.left, c.top - kTitleHeight, c.right, c.bottom);
        if (x >= withTitle.left && x < withTitle.right && y >= withTitle.top && y < withTitle.bottom) {
            auto cr = closeRect(c);
            return { i, x >= cr.left && x < cr.right && y >= cr.top && y < cr.bottom };
        }
    }
    return {};
}

void TabOverview::paint(Painter& p, const Palette& pal, IDWriteTextFormat* titleFormat, IDWriteTextFormat* bigFormat,
    std::vector<std::unique_ptr<Tab>>& tabs, int active, int hover, bool hoverClose, int focus) const
{
    p.fill(m_area, pal.overviewBackground);
    p.rt->PushAxisAlignedClip(m_area, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (int i = 0; i < int(m_cards.size()) && i < int(tabs.size()); ++i) {
        if (m_cards[i].bottom < m_area.top || m_cards[i].top - kTitleHeight > m_area.bottom)
            continue; // off-screen
        auto& tab = *tabs[i];
        auto c = m_cards[i];
        auto rr = D2D1::RoundedRect(c, tokens::kOverviewRadius, tokens::kOverviewRadius);

        // Soft shadow, then the card.
        for (int s = 3; s >= 1; --s) {
            auto sh = D2D1::RoundedRect(D2D1::RectF(c.left - s, c.top - s + 2, c.right + s, c.bottom + s + 2),
                tokens::kOverviewRadius + s, tokens::kOverviewRadius + s);
            auto col = pal.shadow;
            col.a *= 0.18f;
            p.fill(sh, col);
        }
        p.fill(rr, pal.cardBackground);

        ComPtr<ID2D1RoundedRectangleGeometry> clip;
        p.factory->CreateRoundedRectangleGeometry(rr, clip.GetAddressOf());
        p.rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip.Get()), nullptr);
        if (tab.thumbnailBitmap) {
            // Fill the card width, crop to the top of the page (like Safari).
            auto size = tab.thumbnailBitmap->GetSize();
            float scale = (c.right - c.left) / size.width;
            float h = size.height * scale;
            p.rt->DrawBitmap(tab.thumbnailBitmap.Get(), D2D1::RectF(c.left, c.top, c.right, c.top + h), 1.0f,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            // Placeholder: large favicon and host.
            D2D1_POINT_2F center = D2D1::Point2F((c.left + c.right) / 2, (c.top + c.bottom) / 2 - 10);
            if (tab.faviconBitmap)
                p.rt->DrawBitmap(tab.faviconBitmap.Get(), D2D1::RectF(center.x - 16, center.y - 16, center.x + 16, center.y + 16));
            else
                p.globe(center, 14, pal.secondary);
            std::wstring host = services::displayHost(tab.url.empty() ? L"New Tab" : tab.url);
            p.brush->SetColor(pal.secondary);
            p.rt->DrawTextW(host.c_str(), UINT32(host.size()), bigFormat,
                D2D1::RectF(c.left + 8, center.y + 22, c.right - 8, center.y + 44), p.brush);
        }
        p.rt->PopLayer();

        if (i == active || i == focus) {
            p.brush->SetColor(pal.accent);
            p.rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(c.left - 2.5f, c.top - 2.5f, c.right + 2.5f, c.bottom + 2.5f),
                tokens::kOverviewRadius + 2.5f, tokens::kOverviewRadius + 2.5f), p.brush, i == focus && i != active ? 2.0f : 3.0f);
        }

        // Title row above the card: favicon + title.
        float ty = c.top - kTitleHeight;
        float ix = c.left + 2;
        D2D1_POINT_2F ic = D2D1::Point2F(ix + 8, ty + kTitleHeight / 2 - 1);
        if (tab.faviconBitmap)
            p.rt->DrawBitmap(tab.faviconBitmap.Get(), D2D1::RectF(ic.x - 8, ic.y - 8, ic.x + 8, ic.y + 8));
        else
            p.globe(ic, 6.5f, pal.secondary);
        std::wstring title = tab.displayTitle();
        p.brush->SetColor(pal.text);
        p.rt->DrawTextW(title.c_str(), UINT32(title.size()), titleFormat,
            D2D1::RectF(ix + 22, ty, c.right, ty + kTitleHeight - 2), p.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);

        if (i == hover && !tab.pinned) {
            auto cr = closeRect(c);
            D2D1_POINT_2F cc = D2D1::Point2F((cr.left + cr.right) / 2, (cr.top + cr.bottom) / 2);
            auto bg = pal.cardBackground;
            bg.a = 0.92f;
            p.fill(D2D1::RoundedRect(cr, kCloseSize / 2, kCloseSize / 2), hoverClose ? pal.pressed : bg);
            p.cross(cc, 4.0f, 1.6f, pal.text);
        }
    }
    p.rt->PopAxisAlignedClip();
}

} // namespace meridian::ui
