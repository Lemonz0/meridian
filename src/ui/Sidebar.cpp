#include "Sidebar.h"

#include <algorithm>

namespace meridian::ui {

namespace {
constexpr float kInset = 10;
constexpr float kIndent = 16;
constexpr float kAddSize = 24;
constexpr float kPadTop = 8;

bool inside(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
}

float Sidebar::heightOf(Kind k)
{
    switch (k) {
    case Kind::Header: return 32;
    case Kind::Group: return 30;
    case Kind::Panes: return 46;
    case Kind::Folder:
    case Kind::Bookmark: return 28;
    case Kind::Reading: return 52;
    case Kind::Placeholder: return 64;
    }
    return 28;
}

void Sidebar::setItems(std::vector<Item> items)
{
    m_items = std::move(items);
    relayout();
}

void Sidebar::layout(D2D1_RECT_F area)
{
    m_area = area;
    relayout();
}

void Sidebar::relayout()
{
    m_tops.clear();
    float y = kPadTop;
    for (auto& it : m_items) {
        m_tops.push_back(y);
        y += heightOf(it.kind);
    }
    m_contentHeight = y + 16;
    scrollBy(0);
}

void Sidebar::scrollBy(float dy)
{
    float visible = std::max(0.f, m_area.bottom - m_area.top);
    m_scroll = std::clamp(m_scroll + dy, 0.f, std::max(0.f, m_contentHeight - visible));
}

D2D1_RECT_F Sidebar::rowRect(int i) const
{
    if (i < 0 || i >= int(m_items.size()))
        return {};
    float top = m_area.top + m_tops[i] - m_scroll;
    return D2D1::RectF(m_area.left + kInset, top, m_area.right - kInset, top + heightOf(m_items[i].kind));
}

Sidebar::Hit Sidebar::hitTest(float x, float y) const
{
    if (!inside(m_area, x, y))
        return {};
    for (int i = 0; i < int(m_items.size()); ++i) {
        auto r = rowRect(i);
        if (!inside(r, x, y))
            continue;
        const Item& it = m_items[i];
        if (it.kind == Kind::Header && it.addButton && x >= r.right - kAddSize)
            return { i, Part::Add };
        if (it.kind == Kind::Panes)
            return { i, x < (r.left + r.right) / 2 ? Part::BookmarksPane : Part::ReadingPane };
        if (it.kind == Kind::Folder) {
            float disclosureRight = r.left + 8 + it.depth * kIndent + 16;
            if (x < disclosureRight)
                return { i, Part::Disclosure };
        }
        return { i, Part::Row };
    }
    return {};
}

void Sidebar::paint(Painter& p, const Palette& pal, IDWriteTextFormat* header, IDWriteTextFormat* title,
    IDWriteTextFormat* detail, const Hit& hover, const IconLookup& icon) const
{
    p.fill(m_area, pal.toolbarOpaque);
    p.brush->SetColor(pal.tabSeparator);
    p.rt->DrawLine(D2D1::Point2F(m_area.right - 0.5f, m_area.top), D2D1::Point2F(m_area.right - 0.5f, m_area.bottom), p.brush, 1.0f);
    p.rt->PushAxisAlignedClip(m_area, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    auto drawText = [&](const std::wstring& s, IDWriteTextFormat* format, D2D1_RECT_F r, const D2D1_COLOR_F& color) {
        if (r.right <= r.left)
            return;
        p.brush->SetColor(color);
        p.rt->DrawTextW(s.c_str(), UINT32(s.size()), format, r, p.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };

    for (int i = 0; i < int(m_items.size()); ++i) {
        auto r = rowRect(i);
        if (r.bottom < m_area.top || r.top > m_area.bottom)
            continue;
        const Item& it = m_items[i];
        bool hovered = hover.index == i;
        float cy = (r.top + r.bottom) / 2;
        switch (it.kind) {
        case Kind::Header: {
            drawText(it.title, header, D2D1::RectF(r.left + 6, r.top + 6, r.right - kAddSize - 4, r.bottom), pal.secondary);
            if (it.addButton) {
                auto add = D2D1::RectF(r.right - kAddSize, cy - kAddSize / 2 + 3, r.right, cy + kAddSize / 2 + 3);
                if (hovered && hover.part == Part::Add)
                    p.fill(D2D1::RoundedRect(add, 6, 6), pal.hover);
                p.plus(D2D1::Point2F((add.left + add.right) / 2, (add.top + add.bottom) / 2), 5, pal.secondary);
            }
            break;
        }
        case Kind::Group: {
            if (it.current)
                p.fill(D2D1::RoundedRect(r, 7, 7), pal.pressed);
            else if (hovered)
                p.fill(D2D1::RoundedRect(r, 7, 7), pal.hover);
            p.tabsIcon(D2D1::Point2F(r.left + 16, cy), it.current ? pal.accent : pal.secondary);
            drawText(it.detail, detail, D2D1::RectF(r.right - 44, r.top, r.right - 8, r.bottom), pal.secondary);
            drawText(it.title, title, D2D1::RectF(r.left + 34, r.top, r.right - 46, r.bottom), pal.text);
            break;
        }
        case Kind::Panes: {
            auto box = D2D1::RectF(r.left, r.top + 8, r.right, r.bottom - 8);
            p.fill(D2D1::RoundedRect(box, 8, 8), pal.fieldIdle);
            float mid = (box.left + box.right) / 2;
            auto left = D2D1::RectF(box.left + 2, box.top + 2, mid - 1, box.bottom - 2);
            auto right = D2D1::RectF(mid + 1, box.top + 2, box.right - 2, box.bottom - 2);
            p.fill(D2D1::RoundedRect(m_pane == Pane::Bookmarks ? left : right, 6, 6), pal.tabActive);
            // Center the labels: draw into the halves with a centered layout.
            auto label = [&](const wchar_t* s, D2D1_RECT_F half) {
                ComPtr<IDWriteTextLayout> layout;
                p.dwrite->CreateTextLayout(s, UINT32(wcslen(s)), title, half.right - half.left, half.bottom - half.top, layout.GetAddressOf());
                if (!layout)
                    return;
                layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                p.brush->SetColor(pal.text);
                p.rt->DrawTextLayout(D2D1::Point2F(half.left, half.top), layout.Get(), p.brush);
            };
            label(L"Bookmarks", left);
            label(L"Reading List", right);
            break;
        }
        case Kind::Folder:
        case Kind::Bookmark: {
            if (hovered)
                p.fill(D2D1::RoundedRect(r, 6, 6), pal.hover);
            float x = r.left + 8 + it.depth * kIndent;
            if (it.kind == Kind::Folder) {
                auto c = D2D1::Point2F(x + 6, cy);
                if (it.expanded)
                    p.chevronDown(c, 3.5f, pal.secondary);
                else
                    p.chevron(D2D1::RectF(c.x - 6, cy - 9, c.x + 6, cy + 9), false, pal.secondary);
                p.folder(D2D1::Point2F(x + 26, cy), pal.secondary);
            } else {
                auto c = D2D1::Point2F(x + 26, cy);
                if (ID2D1Bitmap* bmp = icon ? icon(it.host) : nullptr)
                    p.rt->DrawBitmap(bmp, D2D1::RectF(c.x - 8, c.y - 8, c.x + 8, c.y + 8), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                else
                    p.globe(c, 6.5f, pal.secondary);
            }
            drawText(it.title, title, D2D1::RectF(x + 42, r.top, r.right - 6, r.bottom), pal.text);
            break;
        }
        case Kind::Reading: {
            if (hovered)
                p.fill(D2D1::RoundedRect(r, 7, 7), pal.hover);
            if (it.unread) {
                p.brush->SetColor(pal.accent);
                p.rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(r.left + 9, r.top + 16), 3.5f, 3.5f), p.brush);
            }
            drawText(it.title, title, D2D1::RectF(r.left + 20, r.top + 4, r.right - 6, r.top + 26), pal.text);
            std::wstring second = it.detail.empty() ? it.host : it.host + L" — " + it.detail;
            drawText(second, detail, D2D1::RectF(r.left + 20, r.top + 26, r.right - 6, r.bottom - 4), pal.secondary);
            break;
        }
        case Kind::Placeholder: {
            ComPtr<IDWriteTextLayout> layout;
            p.dwrite->CreateTextLayout(it.title.c_str(), UINT32(it.title.size()), detail, r.right - r.left - 12, r.bottom - r.top,
                layout.GetAddressOf());
            if (layout) {
                layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                p.brush->SetColor(pal.secondary);
                p.rt->DrawTextLayout(D2D1::Point2F(r.left + 6, r.top + 8), layout.Get(), p.brush);
            }
            break;
        }
        }
    }
    p.rt->PopAxisAlignedClip();
}

} // namespace meridian::ui
