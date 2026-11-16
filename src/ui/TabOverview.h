#pragma once
// ui/: Safari-style tab overview: a grid of page thumbnails. Thumbnails come
// from Windows.Graphics.Capture snapshots taken when a tab was last visible;
// tabs never shown this session get a favicon + title card instead.
#include "Chrome.h"
#include "Tab.h"

#include <memory>
#include <vector>

namespace meridian::ui {

class TabOverview {
public:
    struct Hit {
        int tab { -1 };
        bool close { false };
    };

    // `area` is the region below the toolbar (DIPs). aspect = width/height of page area.
    void layout(int tabCount, D2D1_RECT_F area, float aspect);
    void scrollBy(float dy);            // mouse wheel
    void scrollToShow(int index);       // keyboard focus
    Hit hitTest(float x, float y) const;
    D2D1_RECT_F cardRect(int i) const { return m_cards[i]; }
    int columns() const { return m_columns; }

    void paint(Painter&, const Palette&, IDWriteTextFormat* titleFormat, IDWriteTextFormat* bigFormat,
        std::vector<std::unique_ptr<Tab>>&, int active, int hover, bool hoverClose, int keyboardFocus) const;

private:
    void relayout();

    std::vector<D2D1_RECT_F> m_cards;   // thumbnail rect per tab (scroll applied)
    D2D1_RECT_F m_area {};
    int m_columns { 1 };
    int m_count { 0 };
    float m_aspect { 1.6f };
    float m_scroll { 0 };
    float m_contentHeight { 0 };
};

} // namespace meridian::ui
