#include "HistoryView.h"

#include "services/UrlFixup.h"

#include <algorithm>
#include <windows.h>

namespace meridian::ui {

namespace {
constexpr float kRowHeight = 32;
constexpr float kHeaderHeight = 34;
constexpr float kMaxWidth = 820;
constexpr float kSideMargin = 24;

SYSTEMTIME localTime(int64_t unixSeconds)
{
    // Unix seconds -> FILETIME (100 ns since 1601) -> local SYSTEMTIME.
    ULARGE_INTEGER t;
    t.QuadPart = (uint64_t(unixSeconds) + 11644473600ull) * 10000000ull;
    FILETIME ft { t.LowPart, t.HighPart };
    SYSTEMTIME utc {}, local {};
    FileTimeToSystemTime(&ft, &utc);
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
        local = utc;
    return local;
}

bool sameDay(const SYSTEMTIME& a, const SYSTEMTIME& b)
{
    return a.wYear == b.wYear && a.wMonth == b.wMonth && a.wDay == b.wDay;
}

std::wstring dayLabel(const SYSTEMTIME& day, int64_t visit, int64_t now)
{
    if (sameDay(day, localTime(now)))
        return L"Today";
    if (sameDay(day, localTime(now - 86400)))
        return L"Yesterday";
    wchar_t buf[128] {};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &day, nullptr, buf, ARRAYSIZE(buf), nullptr);
    (void)visit;
    return buf;
}

std::wstring timeLabel(const SYSTEMTIME& t)
{
    wchar_t buf[64] {};
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &t, nullptr, buf, ARRAYSIZE(buf));
    return buf;
}
}

void HistoryView::setEntries(std::vector<services::HistoryEntry> entries, int64_t now)
{
    m_entries = std::move(entries);
    m_times.clear();
    m_rows.clear();
    SYSTEMTIME lastDay {};
    bool first = true;
    float y = 0;
    for (int i = 0; i < int(m_entries.size()); ++i) {
        SYSTEMTIME t = localTime(m_entries[i].lastVisit);
        m_times.push_back(timeLabel(t));
        if (first || !sameDay(t, lastDay)) {
            m_rows.push_back({ -1, dayLabel(t, m_entries[i].lastVisit, now), y });
            y += kHeaderHeight;
            lastDay = t;
            first = false;
        }
        m_rows.push_back({ i, {}, y });
        y += kRowHeight;
    }
    m_contentHeight = y + 24;
    m_scroll = 0;
}

void HistoryView::layout(D2D1_RECT_F area)
{
    m_area = area;
    scrollBy(0);
}

void HistoryView::scrollBy(float dy)
{
    float visible = std::max(0.f, m_area.bottom - listTop());
    m_scroll = std::clamp(m_scroll + dy, 0.f, std::max(0.f, m_contentHeight - visible));
}

void HistoryView::scrollToShow(int entry)
{
    for (auto& r : m_rows) {
        if (r.entry != entry)
            continue;
        float visible = m_area.bottom - listTop();
        if (r.top - m_scroll < 0)
            m_scroll = std::max(0.f, r.top - kHeaderHeight);
        else if (r.top + kRowHeight - m_scroll > visible)
            m_scroll = r.top + kRowHeight - visible;
        return;
    }
}

D2D1_RECT_F HistoryView::searchRect() const
{
    float cx = (m_area.left + m_area.right) / 2;
    float w = std::min(kSearchWidth, std::max(120.f, m_area.right - m_area.left - 2 * kSideMargin));
    float top = m_area.top + kSearchTop;
    return D2D1::RectF(cx - w / 2, top, cx + w / 2, top + kSearchHeight);
}

int HistoryView::hitTest(float x, float y) const
{
    (void)x;
    float top = listTop();
    if (y < top || y >= m_area.bottom)
        return -1;
    float local = y - top + m_scroll;
    for (auto& r : m_rows)
        if (r.entry >= 0 && local >= r.top && local < r.top + kRowHeight)
            return r.entry;
    return -1;
}

void HistoryView::paint(Painter& p, const Palette& pal, IDWriteTextFormat* header, IDWriteTextFormat* title,
    IDWriteTextFormat* detail, IDWriteTextFormat* hint, int hover, int selected, bool searching, const IconLookup& icon) const
{
    p.fill(m_area, pal.overviewBackground);
    auto field = searchRect();
    p.fill(D2D1::RoundedRect(field, kSearchHeight / 2, kSearchHeight / 2), pal.fieldFocused);
    p.magnifier(D2D1::Point2F(field.left + 16, (field.top + field.bottom) / 2), pal.secondary);

    float top = listTop();
    float width = std::min(kMaxWidth, m_area.right - m_area.left - 2 * kSideMargin);
    float left = (m_area.left + m_area.right - width) / 2, right = left + width;
    auto list = D2D1::RectF(m_area.left, top, m_area.right, m_area.bottom);

    if (m_entries.empty()) {
        const wchar_t* text = searching ? L"No history matches your search." : L"History is empty.";
        p.brush->SetColor(pal.secondary);
        p.rt->DrawTextW(text, UINT32(wcslen(text)), hint, D2D1::RectF(left, top + 20, right, top + 50), p.brush);
        return;
    }

    p.rt->PushAxisAlignedClip(list, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (auto& r : m_rows) {
        float y = top + r.top - m_scroll;
        if (y > m_area.bottom)
            break;
        if (y + kHeaderHeight < top)
            continue;
        if (r.entry < 0) {
            p.brush->SetColor(pal.text);
            p.rt->DrawTextW(r.header.c_str(), UINT32(r.header.size()), header,
                D2D1::RectF(left + 8, y + 8, right, y + kHeaderHeight), p.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            continue;
        }
        const auto& e = m_entries[r.entry];
        auto rowRect = D2D1::RectF(left, y, right, y + kRowHeight);
        if (r.entry == selected)
            p.fill(D2D1::RoundedRect(rowRect, 7, 7), pal.pressed);
        else if (r.entry == hover)
            p.fill(D2D1::RoundedRect(rowRect, 7, 7), pal.hover);

        std::wstring host = services::displayHost(e.url);
        D2D1_POINT_2F ic = D2D1::Point2F(left + 20, y + kRowHeight / 2);
        if (ID2D1Bitmap* bmp = icon ? icon(host) : nullptr)
            p.rt->DrawBitmap(bmp, D2D1::RectF(ic.x - 8, ic.y - 8, ic.x + 8, ic.y + 8), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        else
            p.globe(ic, 6.5f, pal.secondary);

        float timeWidth = 90;
        float textLeft = left + 40, textRight = right - timeWidth - 12;
        float titleRight = textLeft + (textRight - textLeft) * 0.55f;
        const std::wstring& t = e.title.empty() ? e.url : e.title;
        p.brush->SetColor(pal.text);
        p.rt->DrawTextW(t.c_str(), UINT32(t.size()), title, D2D1::RectF(textLeft, y, titleRight, y + kRowHeight), p.brush,
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
        p.brush->SetColor(pal.secondary);
        p.rt->DrawTextW(e.url.c_str(), UINT32(e.url.size()), detail, D2D1::RectF(titleRight + 16, y, textRight, y + kRowHeight),
            p.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        const std::wstring& when = m_times[r.entry];
        p.rt->DrawTextW(when.c_str(), UINT32(when.size()), detail, D2D1::RectF(right - timeWidth, y, right - 10, y + kRowHeight),
            p.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    p.rt->PopAxisAlignedClip();
}

} // namespace meridian::ui
