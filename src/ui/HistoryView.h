#pragma once
// ui/: the History view (Ctrl+Y): visits grouped by day, newest first, with a
// search field above (the field itself is an EDIT owned by the window).
#include "Chrome.h"
#include "services/History.h"

#include <functional>
#include <string>
#include <vector>

namespace meridian::ui {

class HistoryView {
public:
    using IconLookup = std::function<ID2D1Bitmap*(const std::wstring& host)>;
    static constexpr float kSearchTop = 16;     // search field position inside the area
    static constexpr float kSearchHeight = 30;
    static constexpr float kSearchWidth = 340;

    void setEntries(std::vector<services::HistoryEntry>, int64_t now);
    void layout(D2D1_RECT_F area);
    void scrollBy(float dy);
    void scrollToShow(int entry);
    int hitTest(float x, float y) const; // entry index, or -1
    D2D1_RECT_F searchRect() const;      // where the window puts its search field
    int count() const { return int(m_entries.size()); }
    const services::HistoryEntry* entry(int i) const { return i >= 0 && i < count() ? &m_entries[i] : nullptr; }

    void paint(Painter&, const Palette&, IDWriteTextFormat* header, IDWriteTextFormat* title,
        IDWriteTextFormat* detail, IDWriteTextFormat* hint, int hover, int selected, bool searching, const IconLookup&) const;

private:
    struct Row {
        int entry { -1 };     // -1: day header
        std::wstring header;
        float top { 0 };      // unscrolled, relative to the list top
    };
    void relayout();
    float listTop() const { return m_area.top + kSearchTop + kSearchHeight + 14; }

    std::vector<services::HistoryEntry> m_entries;
    std::vector<std::wstring> m_times; // per entry, local "3:45 PM"
    std::vector<Row> m_rows;
    D2D1_RECT_F m_area {};
    float m_scroll { 0 }, m_contentHeight { 0 };
};

} // namespace meridian::ui
