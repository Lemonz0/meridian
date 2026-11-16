#pragma once
// ui/: Safari-style tab bar below the toolbar. Tabs share the width equally;
// pinned tabs are compact favicon-only tabs at the start. Close buttons show
// on hover at the leading edge of a tab.
#include "Chrome.h"
#include "Tab.h"

#include <memory>
#include <vector>

namespace meridian::ui {

class TabStrip {
public:
    struct Hit {
        int tab { -1 };
        bool close { false };
    };

    // All geometry in DIPs; `top` is the strip's top edge, `width` the window width.
    void layout(const std::vector<std::unique_ptr<Tab>>&, float top, float width);
    Hit hitTest(float x, float y) const;
    D2D1_RECT_F tabRect(int index) const { return m_rects[index]; }
    D2D1_RECT_F bounds() const { return m_bounds; }
    int count() const { return int(m_rects.size()); }

    // Index the dragged tab should move to when the cursor is at x.
    int dropIndex(const std::vector<std::unique_ptr<Tab>>&, int dragging, float x) const;

    struct State {
        int active { -1 };
        int hover { -1 };
        bool hoverClose { false };
        int dragging { -1 };
        float dragX { 0 };         // cursor x while dragging
        float dragGrabOffset { 0 }; // cursor x - tab left at grab time
    };
    void paint(Painter&, const Palette&, IDWriteTextFormat*, std::vector<std::unique_ptr<Tab>>&, const State&) const;

private:
    void paintTab(Painter&, const Palette&, IDWriteTextFormat*, Tab&, D2D1_RECT_F, bool active, bool hover, bool hoverClose) const;

    std::vector<D2D1_RECT_F> m_rects;
    D2D1_RECT_F m_bounds {};
};

} // namespace meridian::ui
