#include "SuggestionPopup.h"

#include "services/UrlFixup.h"

#include <algorithm>
#include <dwmapi.h>
#include <windowsx.h>

namespace meridian::ui {

namespace {
constexpr wchar_t kClassName[] = L"MeridianSuggestionPopup";
constexpr float kPadding = 6;
constexpr float kIconInset = 14;
constexpr float kTextInset = 40;

const wchar_t* headerFor(services::SuggestionKind kind)
{
    switch (kind) {
    case services::SuggestionKind::TopHit: return L"Top Hit";
    case services::SuggestionKind::Navigate:
    case services::SuggestionKind::Search: return L"Search";
    case services::SuggestionKind::SearchSuggestion: return L"Search Suggestions";
    case services::SuggestionKind::History:
    case services::SuggestionKind::Bookmark: return L"Bookmarks and History";
    }
    return L"";
}

// Readable URL for the detail column: no scheme, no "www.", no trailing slash.
std::wstring prettyURL(const std::wstring& url)
{
    std::wstring s = url;
    auto sep = s.find(L"://");
    if (sep != std::wstring::npos)
        s = s.substr(sep + 3);
    if (s.compare(0, 4, L"www.") == 0)
        s = s.substr(4);
    if (!s.empty() && s.back() == L'/')
        s.pop_back();
    return s;
}
}

void SuggestionPopup::create(HWND owner, Callbacks callbacks)
{
    m_owner = owner;
    m_callbacks = std::move(callbacks);
    static bool registered = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc { sizeof(wc) };
        wc.style = CS_DROPSHADOW;
        wc.lpfnWndProc = wndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        registered = true;
    }
    // Owned (stays above the browser window), never activated, not in the taskbar.
    m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName, L"", WS_POPUP, 0, 0, 0, 0, owner,
        nullptr, inst, this);
    if (!m_hwnd)
        return;
    // Rounded corners on Windows 11 (DWMWA_WINDOW_CORNER_PREFERENCE = 33,
    // DWMWCP_ROUND = 2; raw values so older SDK headers build). Windows 10
    // rejects the attribute and keeps square corners.
    const DWORD kCornerPreference = 33, kRound = 2;
    DwmSetWindowAttribute(m_hwnd, kCornerPreference, &kRound, sizeof(kRound));
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2d.GetAddressOf());
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3), reinterpret_cast<IUnknown**>(m_dwrite.GetAddressOf()));
    auto makeFormat = [&](ComPtr<IDWriteTextFormat>& out, float size, DWRITE_FONT_WEIGHT weight) {
        m_dwrite->CreateTextFormat(L"Segoe UI Variable Text", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, size, L"", out.GetAddressOf());
        out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        ComPtr<IDWriteInlineObject> ellipsis;
        m_dwrite->CreateEllipsisTrimmingSign(out.Get(), ellipsis.GetAddressOf());
        DWRITE_TRIMMING trim { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        out->SetTrimming(&trim, ellipsis.Get());
    };
    makeFormat(m_titleFormat, tokens::kBodyFontSize, DWRITE_FONT_WEIGHT_NORMAL);
    makeFormat(m_detailFormat, tokens::kBodyFontSize - 1, DWRITE_FONT_WEIGHT_NORMAL);
    makeFormat(m_headerFormat, 11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
}

void SuggestionPopup::destroy()
{
    if (m_hwnd)
        DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
}

LRESULT CALLBACK SuggestionPopup::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    SuggestionPopup* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<SuggestionPopup*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else
        self = reinterpret_cast<SuggestionPopup*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCDESTROY)
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    if (!self || msg == WM_NCCREATE || msg == WM_NCDESTROY)
        return DefWindowProcW(hwnd, msg, wp, lp);
    return self->handle(msg, wp, lp);
}

void SuggestionPopup::buildRows()
{
    m_rows.clear();
    float y = kPadding;
    const wchar_t* lastHeader = nullptr;
    for (int i = 0; i < int(m_items.size()); ++i) {
        const wchar_t* header = headerFor(m_items[i].kind);
        if (!lastHeader || wcscmp(header, lastHeader) != 0) {
            Row h;
            h.header = header;
            h.rect = D2D1::RectF(0, y, 0, y + tokens::kSuggestionHeaderHeight);
            m_rows.push_back(h);
            y += tokens::kSuggestionHeaderHeight;
            lastHeader = header;
        }
        Row r;
        r.item = i;
        r.rect = D2D1::RectF(0, y, 0, y + tokens::kSuggestionRowHeight);
        m_rows.push_back(r);
        y += tokens::kSuggestionRowHeight;
    }
}

float SuggestionPopup::contentHeight() const
{
    return (m_rows.empty() ? 0 : m_rows.back().rect.bottom) + kPadding;
}

void SuggestionPopup::show(const RECT& field, UINT dpi, const platform::ThemeState& theme,
    std::vector<services::Suggestion> items, int selected)
{
    if (!m_hwnd)
        return;
    if (items.empty()) {
        hide();
        return;
    }
    if (dpi != m_dpi || theme.dark != m_theme.dark || theme.highContrast != m_theme.highContrast)
        discardDeviceResources();
    m_dpi = dpi;
    m_theme = theme;
    m_items = std::move(items);
    m_selected = std::clamp(selected, -1, int(m_items.size()) - 1);
    m_hover = -1;
    buildRows();

    int gap = int(4 * dpiScale());
    int width = field.right - field.left;
    int height = int(contentHeight() * dpiScale() + 0.5f);
    // Keep it on the monitor: if there is no room below, it is cut to fit.
    MONITORINFO mi { sizeof(mi) };
    GetMonitorInfoW(MonitorFromRect(&field, MONITOR_DEFAULTTONEAREST), &mi);
    height = std::min<int>(height, std::max<LONG>(0, mi.rcWork.bottom - field.bottom - gap * 2));
    SetWindowPos(m_hwnd, nullptr, field.left, field.bottom + gap, width, height,
        SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
    if (m_rt)
        m_rt->Resize(D2D1::SizeU(UINT32(width), UINT32(height)));
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SuggestionPopup::setSelected(int index)
{
    m_selected = std::clamp(index, -1, int(m_items.size()) - 1);
    if (m_hwnd)
        InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SuggestionPopup::hide()
{
    if (m_hwnd)
        ShowWindow(m_hwnd, SW_HIDE);
    m_items.clear();
    m_rows.clear();
    m_selected = m_hover = -1;
}

int SuggestionPopup::itemAt(float x, float y) const
{
    (void)x;
    for (auto& r : m_rows)
        if (r.item >= 0 && y >= r.rect.top && y < r.rect.bottom)
            return r.item;
    return -1;
}

void SuggestionPopup::discardDeviceResources()
{
    m_rt.Reset();
    m_icons.clear();
}

ID2D1Bitmap* SuggestionPopup::iconFor(const std::wstring& host)
{
    if (host.empty() || !m_callbacks.icon || !m_rt)
        return nullptr;
    auto it = m_icons.find(host);
    if (it == m_icons.end()) {
        const services::Bitmap& bmp = m_callbacks.icon(host);
        it = m_icons.emplace(host, bmp ? makeBitmap(m_rt.Get(), bmp) : nullptr).first;
    }
    return it->second.Get();
}

void SuggestionPopup::paint()
{
    if (!m_rt) {
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), float(m_dpi), float(m_dpi));
        m_d2d->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(m_hwnd, D2D1::SizeU(rc.right, rc.bottom)),
            m_rt.GetAddressOf());
        if (!m_rt)
            return;
    }
    auto pal = paletteFor(m_theme);
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    float width = rc.right / dpiScale();

    m_rt->BeginDraw();
    m_rt->SetTransform(D2D1::Matrix3x2F::Identity());
    m_rt->Clear(pal.fieldFocused);
    ComPtr<ID2D1SolidColorBrush> brush;
    m_rt->CreateSolidColorBrush(pal.text, brush.GetAddressOf());
    Painter p { m_rt.Get(), m_d2d.Get(), brush.Get(), m_dwrite.Get() };
    const D2D1_COLOR_F onAccent = m_theme.highContrast ? pal.toolbarOpaque : D2D1::ColorF(D2D1::ColorF::White);

    for (auto& row : m_rows) {
        auto r = D2D1::RectF(kPadding, row.rect.top, width - kPadding, row.rect.bottom);
        if (row.item < 0) {
            brush->SetColor(pal.secondary);
            m_rt->DrawTextW(row.header.c_str(), UINT32(row.header.size()), m_headerFormat.Get(),
                D2D1::RectF(r.left + kIconInset - 2, r.top + 4, r.right - 8, r.bottom), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            continue;
        }
        const auto& item = m_items[row.item];
        bool selected = row.item == m_selected;
        if (selected)
            p.fill(D2D1::RoundedRect(r, 7, 7), pal.accent);
        else if (row.item == m_hover)
            p.fill(D2D1::RoundedRect(r, 7, 7), pal.hover);
        auto text = selected ? onAccent : pal.text;
        auto detail = selected ? onAccent : pal.secondary;

        D2D1_POINT_2F ic = D2D1::Point2F(r.left + kIconInset + 4, (r.top + r.bottom) / 2);
        bool search = item.kind == services::SuggestionKind::Search || item.kind == services::SuggestionKind::SearchSuggestion;
        if (search)
            p.magnifier(ic, detail);
        else if (ID2D1Bitmap* icon = iconFor(services::displayHost(item.url)))
            m_rt->DrawBitmap(icon, D2D1::RectF(ic.x - 8, ic.y - 8, ic.x + 8, ic.y + 8), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        else
            p.globe(ic, 6.5f, detail);

        std::wstring title = item.title.empty() ? prettyURL(item.url) : item.title;
        std::wstring info;
        switch (item.kind) {
        case services::SuggestionKind::Search: info = L"Search"; break;
        case services::SuggestionKind::SearchSuggestion: break;
        case services::SuggestionKind::Navigate: info = L"Go to site"; break;
        default: info = prettyURL(item.url); break;
        }
        if (item.bookmarked && item.kind != services::SuggestionKind::SearchSuggestion)
            info = info.empty() ? L"Bookmark" : info + L"  ·  Bookmark";

        float textLeft = r.left + kTextInset, textRight = r.right - 10;
        float avail = std::max(0.f, textRight - textLeft);
        ComPtr<IDWriteTextLayout> titleLayout;
        m_dwrite->CreateTextLayout(title.c_str(), UINT32(title.size()), m_titleFormat.Get(),
            info.empty() ? avail : avail * 0.62f, r.bottom - r.top, titleLayout.GetAddressOf());
        if (!titleLayout)
            continue;
        DWRITE_TEXT_METRICS tm {};
        titleLayout->GetMetrics(&tm);
        brush->SetColor(text);
        m_rt->DrawTextLayout(D2D1::Point2F(textLeft, r.top), titleLayout.Get(), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        if (!info.empty()) {
            std::wstring dashed = L"—  " + info;
            float infoLeft = textLeft + tm.widthIncludingTrailingWhitespace + 8;
            brush->SetColor(detail);
            m_rt->DrawTextW(dashed.c_str(), UINT32(dashed.size()), m_detailFormat.Get(),
                D2D1::RectF(infoLeft, r.top, textRight, r.bottom), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }
    if (m_rt->EndDraw() == D2DERR_RECREATE_TARGET)
        discardDeviceResources();
}

LRESULT SuggestionPopup::handle(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE; // the address field keeps focus
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(m_hwnd, &ps);
        paint();
        EndPaint(m_hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (m_rt)
            m_rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
        return 0;
    case WM_MOUSEMOVE: {
        int hover = itemAt(GET_X_LPARAM(lp) / dpiScale(), GET_Y_LPARAM(lp) / dpiScale());
        if (hover != m_hover) {
            m_hover = hover;
            TRACKMOUSEEVENT tme { sizeof(tme), TME_LEAVE, m_hwnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        m_hover = -1;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        return 0; // act on button up, like the toolbar buttons
    case WM_LBUTTONUP: {
        int index = itemAt(GET_X_LPARAM(lp) / dpiScale(), GET_Y_LPARAM(lp) / dpiScale());
        if (index >= 0 && m_callbacks.activate)
            m_callbacks.activate(index); // may hide (and clear) this popup
        return 0;
    }
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

} // namespace meridian::ui
