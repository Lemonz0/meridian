#include "StartPage.h"

#include <algorithm>

namespace meridian::ui {

namespace {
constexpr float kMaxWidth = 760;
constexpr float kMargin = 40;
constexpr float kTile = 64;
constexpr float kCellWidth = 92;
constexpr float kCellHeight = 104; // tile + two-line-free label
constexpr float kHeading = 34;
constexpr float kSectionGap = 26;
constexpr float kLabelHeight = 30;
}

void StartPage::setContent(std::vector<Item> favorites, std::vector<Item> frequent)
{
    m_favorites = std::move(favorites);
    m_frequent = std::move(frequent);
    relayout();
}

void StartPage::layout(D2D1_RECT_F area)
{
    m_area = area;
    relayout();
}

void StartPage::scrollBy(float dy)
{
    float maxScroll = std::max(0.f, m_contentHeight - (m_area.bottom - m_area.top));
    m_scroll = std::clamp(m_scroll + dy, 0.f, maxScroll);
    relayout();
}

void StartPage::relayout()
{
    float areaWidth = m_area.right - m_area.left;
    float width = std::min(kMaxWidth, std::max(kCellWidth, areaWidth - 2 * kMargin));
    int columns = std::max(1, int(width / kCellWidth));
    float left = m_area.left + (areaWidth - columns * kCellWidth) / 2;
    float y = m_area.top + kMargin - m_scroll;

    auto place = [&](const std::vector<Item>& items, std::vector<D2D1_RECT_F>& tiles) {
        tiles.clear();
        for (int i = 0; i < int(items.size()); ++i) {
            float x = left + (i % columns) * kCellWidth + (kCellWidth - kTile) / 2;
            float ty = y + (i / columns) * kCellHeight;
            tiles.push_back(D2D1::RectF(x, ty, x + kTile, ty + kTile));
        }
        int rows = items.empty() ? 1 : (int(items.size()) + columns - 1) / columns; // empty: one row for the hint
        y += rows * kCellHeight;
    };

    m_favoritesHeading = y;
    y += kHeading;
    place(m_favorites, m_favoriteTiles);
    m_frequentHeading = -1;
    if (!m_frequent.empty()) {
        y += kSectionGap;
        m_frequentHeading = y;
        y += kHeading;
        place(m_frequent, m_frequentTiles);
    } else
        m_frequentTiles.clear();
    m_contentHeight = y + m_scroll - m_area.top + kMargin;
}

StartPage::Hit StartPage::hitTest(float x, float y) const
{
    auto test = [&](const std::vector<D2D1_RECT_F>& tiles, Section s) -> Hit {
        for (int i = 0; i < int(tiles.size()); ++i) {
            auto t = tiles[i];
            // Tile plus its label.
            if (x >= t.left - 10 && x < t.right + 10 && y >= t.top && y < t.bottom + kLabelHeight)
                return { s, i };
        }
        return {};
    };
    if (y < m_area.top || y >= m_area.bottom)
        return {};
    if (auto h = test(m_favoriteTiles, Section::Favorites); h.valid())
        return h;
    return test(m_frequentTiles, Section::Frequent);
}

const StartPage::Item* StartPage::item(const Hit& h) const
{
    auto& list = items(h.section);
    return h.index >= 0 && h.index < int(list.size()) ? &list[h.index] : nullptr;
}

void StartPage::paint(Painter& p, const Palette& pal, IDWriteTextFormat* heading, IDWriteTextFormat* label,
    IDWriteTextFormat* letter, IDWriteTextFormat* hint, const Hit& hover, const IconLookup& icon) const
{
    p.fill(m_area, pal.overviewBackground);
    p.rt->PushAxisAlignedClip(m_area, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    float areaWidth = m_area.right - m_area.left;
    float width = std::min(kMaxWidth, std::max(kCellWidth, areaWidth - 2 * kMargin));
    int columns = std::max(1, int(width / kCellWidth));
    float left = m_area.left + (areaWidth - columns * kCellWidth) / 2;
    float right = left + columns * kCellWidth;

    auto drawHeading = [&](const wchar_t* text, float y) {
        p.brush->SetColor(pal.text);
        p.rt->DrawTextW(text, UINT32(wcslen(text)), heading, D2D1::RectF(left + 14, y, right, y + kHeading - 6), p.brush);
    };
    auto drawTiles = [&](const std::vector<Item>& items, const std::vector<D2D1_RECT_F>& tiles, Section s) {
        for (int i = 0; i < int(tiles.size()) && i < int(items.size()); ++i) {
            auto t = tiles[i];
            if (t.bottom + kLabelHeight < m_area.top || t.top > m_area.bottom)
                continue;
            bool hovered = hover.section == s && hover.index == i;
            if (hovered) {
                auto h = D2D1::RectF(t.left - 12, t.top - 6, t.right + 12, t.bottom + kLabelHeight + 2);
                p.fill(D2D1::RoundedRect(h, 10, 10), pal.hover);
            }
            p.siteTile(t, icon ? icon(items[i].host) : nullptr, items[i].host, letter, pal.cardBackground, pal.secondary);
            const std::wstring& text = items[i].title.empty() ? items[i].host : items[i].title;
            p.brush->SetColor(pal.text);
            p.rt->DrawTextW(text.c_str(), UINT32(text.size()), label,
                D2D1::RectF(t.left - 12, t.bottom + 4, t.right + 12, t.bottom + kLabelHeight), p.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    };

    drawHeading(L"Favorites", m_favoritesHeading);
    if (m_favorites.empty()) {
        const wchar_t* text = L"Press Ctrl+D on a page to add it to Favorites.";
        float y = m_favoritesHeading + kHeading;
        p.brush->SetColor(pal.secondary);
        p.rt->DrawTextW(text, UINT32(wcslen(text)), hint, D2D1::RectF(left + 14, y, right, y + 28), p.brush);
    } else
        drawTiles(m_favorites, m_favoriteTiles, Section::Favorites);
    if (m_frequentHeading >= 0) {
        drawHeading(L"Frequently Visited", m_frequentHeading);
        drawTiles(m_frequent, m_frequentTiles, Section::Frequent);
    }
    p.rt->PopAxisAlignedClip();
}

} // namespace meridian::ui
