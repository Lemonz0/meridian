#pragma once
// ui/: the sidebar (Ctrl+Shift+L): Tab Groups at the top, then Bookmarks or
// the Reading List. The window builds the rows from its data; this class
// lays them out, paints them and hit-tests them.
#include "Chrome.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace meridian::ui {

class Sidebar {
public:
    enum class Kind { Header, Group, Panes, Folder, Bookmark, Reading, Placeholder };
    enum class Pane { Bookmarks, ReadingList };
    enum class Part { Row, Add, Disclosure, BookmarksPane, ReadingPane };

    struct Item {
        Kind kind { Kind::Placeholder };
        int64_t id { 0 };     // group id (0 = ungrouped tabs), bookmark/folder id, reading item id
        int depth { 0 };      // folders and bookmarks
        std::wstring title, detail, url, host;
        bool current { false };   // group shown in this window
        bool expanded { false };  // folders
        bool unread { false };    // reading items
        bool addButton { false }; // headers
    };
    struct Hit {
        int index { -1 };
        Part part { Part::Row };
        bool valid() const { return index >= 0; }
        bool operator==(const Hit& o) const { return index == o.index && part == o.part; }
    };
    using IconLookup = std::function<ID2D1Bitmap*(const std::wstring& host)>;

    void setItems(std::vector<Item>);
    void setPane(Pane p) { m_pane = p; }
    Pane pane() const { return m_pane; }
    void layout(D2D1_RECT_F area);
    void scrollBy(float dy);
    Hit hitTest(float x, float y) const;
    const Item* item(int index) const { return index >= 0 && index < int(m_items.size()) ? &m_items[index] : nullptr; }
    int count() const { return int(m_items.size()); }
    D2D1_RECT_F rowRect(int index) const; // scrolled, in window DIPs

    void paint(Painter&, const Palette&, IDWriteTextFormat* header, IDWriteTextFormat* title, IDWriteTextFormat* detail,
        const Hit& hover, const IconLookup&) const;

private:
    static float heightOf(Kind);
    void relayout();

    std::vector<Item> m_items;
    std::vector<float> m_tops; // unscrolled, relative to the area top
    Pane m_pane { Pane::Bookmarks };
    D2D1_RECT_F m_area {};
    float m_scroll { 0 }, m_contentHeight { 0 };
};

} // namespace meridian::ui
