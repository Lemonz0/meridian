#pragma once
// ui/: the start page shown in new tabs: Favorites, Frequently Visited and
// unread Reading List items, drawn natively from local data (nothing is
// fetched to build it).
#include "Chrome.h"

#include <functional>
#include <string>
#include <vector>

namespace meridian::ui {

class StartPage {
public:
    enum class Section { Favorites, Frequent, Reading };
    static constexpr int kSectionCount = 3;
    struct Item {
        int64_t bookmarkId { 0 }; // favorites only
        int64_t historyId { 0 };  // frequently visited only
        int64_t readingId { 0 };  // reading list only
        std::wstring title, url, host;
    };
    struct Hit {
        Section section { Section::Favorites };
        int index { -1 };
        bool valid() const { return index >= 0; }
        bool operator==(const Hit& o) const { return section == o.section && index == o.index; }
    };
    using IconLookup = std::function<ID2D1Bitmap*(const std::wstring& host)>;

    void setContent(std::vector<Item> favorites, std::vector<Item> frequent, std::vector<Item> reading);
    void layout(D2D1_RECT_F area);
    void scrollBy(float dy);
    Hit hitTest(float x, float y) const;
    const Item* item(const Hit&) const;
    const std::vector<Item>& items(Section s) const { return m_items[int(s)]; }

    void paint(Painter&, const Palette&, IDWriteTextFormat* heading, IDWriteTextFormat* label,
        IDWriteTextFormat* letter, IDWriteTextFormat* hint, const Hit& hover, const IconLookup&) const;

private:
    void relayout();

    std::vector<Item> m_items[kSectionCount];
    std::vector<D2D1_RECT_F> m_tiles[kSectionCount]; // scroll applied
    float m_heading[kSectionCount] {};               // -1: section not shown
    D2D1_RECT_F m_area {};
    float m_scroll { 0 }, m_contentHeight { 0 };
};

} // namespace meridian::ui
