#include "BrowserWindow.h"

#include "config/Brand.h"
#include "services/UrlFixup.h"

#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>

namespace meridian::ui {

// ---- design tokens (DIPs) --------------------------------------------------
namespace tokens {
constexpr float kToolbarHeight = 52;
constexpr float kButtonSize = 30;
constexpr float kButtonRadius = 8;
constexpr float kLeftInset = 12;
constexpr float kCaptionReserve = 140; // system caption buttons (min/max/close)
constexpr float kFieldHeight = 32;
constexpr float kFieldMaxWidth = 640;
constexpr float kFieldMinWidth = 220;
constexpr float kGlyphStroke = 1.7f;
constexpr float kBannerHeight = 32;
constexpr float kHostFontSize = 13.5f;
}

struct Palette {
    D2D1_COLOR_F toolbarOpaque, fieldIdle, fieldFocused, text, secondary, hover, pressed, accent, banner;
};

static D2D1_COLOR_F rgba(UINT32 rgb, float a = 1) { return D2D1::ColorF(rgb, a); }

static Palette paletteFor(const platform::ThemeState& t)
{
    if (t.highContrast) {
        return { rgba(GetSysColor(COLOR_WINDOW)), rgba(GetSysColor(COLOR_WINDOW)), rgba(GetSysColor(COLOR_WINDOW)),
                 rgba(GetSysColor(COLOR_WINDOWTEXT)), rgba(GetSysColor(COLOR_GRAYTEXT)), rgba(GetSysColor(COLOR_HIGHLIGHT), 0.3f),
                 rgba(GetSysColor(COLOR_HIGHLIGHT), 0.5f), rgba(GetSysColor(COLOR_HIGHLIGHT)), rgba(GetSysColor(COLOR_INFOBK)) };
    }
    if (t.dark) {
        return { rgba(0x202022), rgba(0xFFFFFF, 0.08f), rgba(0x2C2C2E), rgba(0xF5F5F7), rgba(0xF5F5F7, 0.55f),
                 rgba(0xFFFFFF, 0.08f), rgba(0xFFFFFF, 0.14f), rgba(0x3B8CFF), rgba(0x3A3A3C) };
    }
    return { rgba(0xF3F3F5), rgba(0x000000, 0.055f), rgba(0xFFFFFF), rgba(0x1D1D1F), rgba(0x1D1D1F, 0.5f),
             rgba(0x000000, 0.06f), rgba(0x000000, 0.11f), rgba(0x0A6CFF), rgba(0xFFF4D6) };
}

static COLORREF toColorRef(const D2D1_COLOR_F& c)
{
    return RGB(BYTE(c.r * 255), BYTE(c.g * 255), BYTE(c.b * 255));
}

// ---- creation ---------------------------------------------------------------

BrowserWindow::BrowserWindow(engine::EngineContext& engine)
    : m_engine(engine)
{
}

BrowserWindow* BrowserWindow::create(engine::EngineContext& engine, const std::wstring& initialURL)
{
    static bool registered = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc { sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = wndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
        wc.lpszClassName = brand::kWindowClass;
        RegisterClassExW(&wc);
        registered = true;
    }

    auto* self = new BrowserWindow(engine);
    HWND hwnd = CreateWindowExW(0, brand::kWindowClass, brand::kProductName,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 860,
        nullptr, nullptr, inst, self);
    if (!hwnd) {
        delete self;
        return nullptr;
    }
    self->m_view->loadURL(initialURL.empty() ? brand::kDefaultHomeURL : initialURL);
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    return self;
}

LRESULT CALLBACK BrowserWindow::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    BrowserWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<BrowserWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else
        self = reinterpret_cast<BrowserWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (!self)
        return DefWindowProcW(hwnd, msg, wp, lp);

    LRESULT result = self->handle(msg, wp, lp);
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete self;
    }
    return result;
}

void BrowserWindow::onCreate()
{
    m_dpi = GetDpiForWindow(m_hwnd);
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2d.GetAddressOf());
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3),
        reinterpret_cast<IUnknown**>(m_dwrite.GetAddressOf()));

    // Address field: a borderless EDIT shown only while editing (Safari shows
    // the bare host when idle and the full URL on focus).
    m_address = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | ES_LEFT,
        0, 0, 0, 0, m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(m_address, addressProc, 1, reinterpret_cast<DWORD_PTR>(this));

    engine::WebViewCallbacks cb;
    cb.titleChanged = [this](const std::wstring& t) {
        m_title = t;
        std::wstring caption = t.empty() ? brand::kProductName : t + L" — " + brand::kProductName;
        SetWindowTextW(m_hwnd, caption.c_str());
    };
    cb.urlChanged = [this](const std::wstring& u) {
        m_url = u;
        if (!m_editing)
            SetWindowTextW(m_address, u.c_str());
        InvalidateRect(m_hwnd, nullptr, FALSE);
    };
    cb.progressChanged = [this](double p) {
        m_progress = p;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    };
    cb.loadingChanged = [this](bool loading) {
        m_loading = loading;
        if (!loading)
            m_progress = 0;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    };
    cb.backForwardChanged = [this] { InvalidateRect(m_hwnd, nullptr, FALSE); };
    cb.loadFailed = [this](const std::wstring& url, const std::wstring& desc) {
        // -999 style cancellations arrive with an empty description; ignore them.
        if (!desc.empty())
            showBanner(L"Couldn’t open " + services::displayHost(url) + L": " + desc);
    };
    cb.webProcessCrashed = [this] {
        showBanner(L"This webpage was reloaded because a problem occurred.");
        m_view->reload();
    };
    m_view = std::make_unique<engine::WebView>(m_engine, m_hwnd, std::move(cb));

    updateTheme();
}

void BrowserWindow::updateTheme()
{
    m_theme = platform::queryTheme();
    platform::applyWindowMaterial(m_hwnd, m_theme);

    MARGINS margins { 0, 0, px(tokens::kToolbarHeight), 0 };
    DwmExtendFrameIntoClientArea(m_hwnd, &margins);

    // The toolbar is painted transparent so the backdrop shows through; stop
    // DWM from drawing the caption text/icon into that area (the taskbar and
    // Alt+Tab still use the window title). Caption buttons are kept.
    WTA_OPTIONS nc { WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON | WTNCA_NOSYSMENU,
                     WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON | WTNCA_NOSYSMENU };
    SetWindowThemeAttribute(m_hwnd, WTA_NONCLIENT, &nc, sizeof(nc));

    if (m_editFont)
        DeleteObject(m_editFont);
    m_editFont = CreateFontW(-px(tokens::kHostFontSize), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable Text");
    SendMessageW(m_address, WM_SETFONT, reinterpret_cast<WPARAM>(m_editFont), TRUE);

    if (m_editBrush)
        DeleteObject(m_editBrush);
    m_editBrush = CreateSolidBrush(toColorRef(paletteFor(m_theme).fieldFocused));

    m_hostFormat.Reset();
    m_dwrite->CreateTextFormat(L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_MEDIUM,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, tokens::kHostFontSize, L"",
        reinterpret_cast<IDWriteTextFormat**>(m_hostFormat.GetAddressOf()));
    m_hostFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_hostFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_hostFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TRIMMING trim { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
    m_hostFormat->SetTrimming(&trim, nullptr);

    discardDeviceResources();
    layout();
    RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

// ---- geometry ---------------------------------------------------------------

D2D1_RECT_F BrowserWindow::addressRectDip() const
{
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    float width = rc.right / dpiScale();
    float leftLimit = tokens::kLeftInset + 2 * tokens::kButtonSize + 16;
    float rightLimit = width - tokens::kCaptionReserve - 8;
    float fieldWidth = std::min(tokens::kFieldMaxWidth, std::max(tokens::kFieldMinWidth, width * 0.46f));
    float left = (width - fieldWidth) / 2;
    if (left < leftLimit)
        left = leftLimit;
    float right = std::min(left + fieldWidth, rightLimit);
    float top = (tokens::kToolbarHeight - tokens::kFieldHeight) / 2;
    return D2D1::RectF(left, top, std::max(right, left + 80), top + tokens::kFieldHeight);
}

static D2D1_RECT_F buttonRect(int index)
{
    float top = (tokens::kToolbarHeight - tokens::kButtonSize) / 2;
    float left = tokens::kLeftInset + index * (tokens::kButtonSize + 2);
    return D2D1::RectF(left, top, left + tokens::kButtonSize, top + tokens::kButtonSize);
}

static bool contains(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

BrowserWindow::Hit BrowserWindow::hitTest(POINT pt) const
{
    float x = pt.x / dpiScale(), y = pt.y / dpiScale();
    if (y >= tokens::kToolbarHeight)
        return Hit::None;
    if (contains(buttonRect(0), x, y))
        return Hit::Back;
    if (contains(buttonRect(1), x, y))
        return Hit::Forward;
    auto field = addressRectDip();
    if (contains(field, x, y)) {
        auto reload = D2D1::RectF(field.right - tokens::kFieldHeight, field.top, field.right, field.bottom);
        return contains(reload, x, y) && !m_editing ? Hit::Reload : Hit::Address;
    }
    return Hit::Caption;
}

void BrowserWindow::layout()
{
    if (!m_view)
        return;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int top = px(tokens::kToolbarHeight) + (m_banner.empty() ? 0 : px(tokens::kBannerHeight));
    m_view->setBounds({ 0, top, rc.right, rc.bottom });

    auto f = addressRectDip();
    int editHeight = px(tokens::kHostFontSize * 1.45f);
    int x = px(f.left + 14), w = px(f.right - f.left - 28);
    int y = px((f.top + f.bottom) / 2) - editHeight / 2;
    SetWindowPos(m_address, nullptr, x, y, w, editHeight, SWP_NOZORDER | SWP_NOACTIVATE);

    if (m_rt)
        m_rt->Resize(D2D1::SizeU(rc.right, rc.bottom));
}

// ---- drawing ----------------------------------------------------------------

void BrowserWindow::createDeviceResources()
{
    if (m_rt)
        return;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        static_cast<float>(m_dpi), static_cast<float>(m_dpi));
    m_d2d->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(m_hwnd, D2D1::SizeU(rc.right, rc.bottom)),
        m_rt.GetAddressOf());
}

void BrowserWindow::discardDeviceResources() { m_rt.Reset(); }

static void drawChevron(ID2D1RenderTarget* rt, ID2D1Factory* f, D2D1_RECT_F r, bool left, ID2D1Brush* b)
{
    float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, s = 5.5f;
    float dir = left ? 1.f : -1.f;
    ComPtr<ID2D1PathGeometry> g;
    f->CreatePathGeometry(g.GetAddressOf());
    ComPtr<ID2D1GeometrySink> sink;
    g->Open(sink.GetAddressOf());
    sink->BeginFigure(D2D1::Point2F(cx + dir * s * 0.55f, cy - s), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(D2D1::Point2F(cx - dir * s * 0.55f, cy));
    sink->AddLine(D2D1::Point2F(cx + dir * s * 0.55f, cy + s));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    ComPtr<ID2D1StrokeStyle> style;
    f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
        D2D1_LINE_JOIN_ROUND), nullptr, 0, style.GetAddressOf());
    rt->DrawGeometry(g.Get(), b, tokens::kGlyphStroke + 0.3f, style.Get());
}

static void drawReload(ID2D1RenderTarget* rt, ID2D1Factory* f, D2D1_POINT_2F c, ID2D1Brush* b)
{
    // Open circle (about 300 degrees) with an arrowhead at the open end.
    const float r = 5.6f;
    ComPtr<ID2D1PathGeometry> g;
    f->CreatePathGeometry(g.GetAddressOf());
    ComPtr<ID2D1GeometrySink> sink;
    g->Open(sink.GetAddressOf());
    auto at = [&](float deg) {
        float rad = deg * 3.14159265f / 180;
        return D2D1::Point2F(c.x + r * cosf(rad), c.y + r * sinf(rad));
    };
    sink->BeginFigure(at(-60), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(at(-110), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    auto tip = at(-60);
    sink->BeginFigure(D2D1::Point2F(tip.x - 3.6f, tip.y - 1.2f), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(tip);
    sink->AddLine(D2D1::Point2F(tip.x + 0.6f, tip.y - 3.8f));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    ComPtr<ID2D1StrokeStyle> style;
    f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
        D2D1_LINE_JOIN_ROUND), nullptr, 0, style.GetAddressOf());
    rt->DrawGeometry(g.Get(), b, tokens::kGlyphStroke - 0.2f, style.Get());
}

static void drawStop(ID2D1RenderTarget* rt, D2D1_POINT_2F c, ID2D1Brush* b)
{
    const float s = 4.2f;
    rt->DrawLine(D2D1::Point2F(c.x - s, c.y - s), D2D1::Point2F(c.x + s, c.y + s), b, tokens::kGlyphStroke);
    rt->DrawLine(D2D1::Point2F(c.x - s, c.y + s), D2D1::Point2F(c.x + s, c.y - s), b, tokens::kGlyphStroke);
}

static void drawLock(ID2D1RenderTarget* rt, D2D1_POINT_2F c, ID2D1Brush* b)
{
    // Body 8x6.5 with a 1.5 radius; shackle as a rounded arch.
    auto body = D2D1::RoundedRect(D2D1::RectF(c.x - 4, c.y - 1, c.x + 4, c.y + 5.5f), 1.5f, 1.5f);
    rt->FillRoundedRectangle(body, b);
    auto shackle = D2D1::RoundedRect(D2D1::RectF(c.x - 2.6f, c.y - 5.5f, c.x + 2.6f, c.y + 1.5f), 2.6f, 2.6f);
    rt->DrawRoundedRectangle(shackle, b, 1.4f);
}

void BrowserWindow::paint()
{
    createDeviceResources();
    if (!m_rt)
        return;
    auto pal = paletteFor(m_theme);
    bool translucent = m_theme.transparencyEnabled && !m_theme.highContrast && !platform::isRemoteSession();

    m_rt->BeginDraw();
    m_rt->SetTransform(D2D1::Matrix3x2F::Identity());
    // Transparent clear lets the DWM Mica backdrop show through the toolbar.
    m_rt->Clear(translucent ? D2D1::ColorF(0, 0, 0, 0) : pal.toolbarOpaque);

    ComPtr<ID2D1SolidColorBrush> brush;
    m_rt->CreateSolidColorBrush(pal.text, brush.GetAddressOf());

    // Back / forward.
    struct Btn { Hit id; bool enabled; bool left; };
    Btn buttons[] = { { Hit::Back, m_view->canGoBack(), true }, { Hit::Forward, m_view->canGoForward(), false } };
    for (int i = 0; i < 2; ++i) {
        auto r = buttonRect(i);
        if (buttons[i].enabled && (m_hover == buttons[i].id || m_pressed == buttons[i].id)) {
            brush->SetColor(m_pressed == buttons[i].id ? pal.pressed : pal.hover);
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(r, tokens::kButtonRadius, tokens::kButtonRadius), brush.Get());
        }
        brush->SetColor(buttons[i].enabled ? pal.text : pal.secondary);
        drawChevron(m_rt.Get(), m_d2d.Get(), r, buttons[i].left, brush.Get());
    }

    // Smart Search field (capsule).
    auto f = addressRectDip();
    float radius = tokens::kFieldHeight / 2;
    auto capsule = D2D1::RoundedRect(f, radius, radius);
    brush->SetColor(m_editing ? pal.fieldFocused : pal.fieldIdle);
    m_rt->FillRoundedRectangle(capsule, brush.Get());
    if (m_editing) {
        brush->SetColor(pal.accent);
        m_rt->DrawRoundedRectangle(capsule, brush.Get(), 2.0f);
    }

    // Loading progress: accent fill sweeping across the capsule.
    if (m_loading && m_progress > 0 && !m_editing) {
        ComPtr<ID2D1RoundedRectangleGeometry> clip;
        m_d2d->CreateRoundedRectangleGeometry(capsule, clip.GetAddressOf());
        m_rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip.Get()), nullptr);
        auto fill = f;
        fill.right = f.left + float((f.right - f.left) * m_progress);
        auto accent = pal.accent;
        accent.a = 0.18f;
        brush->SetColor(accent);
        m_rt->FillRectangle(fill, brush.Get());
        m_rt->PopLayer();
    }

    if (!m_editing) {
        std::wstring host = m_url.empty() ? std::wstring(L"Search or enter website name") : services::displayHost(m_url);
        bool secure = services::isSecureURL(m_url);
        auto textRect = D2D1::RectF(f.left + 36, f.top, f.right - 36, f.bottom);
        ComPtr<IDWriteTextLayout> layoutObj;
        m_dwrite->CreateTextLayout(host.c_str(), UINT32(host.size()), m_hostFormat.Get(),
            textRect.right - textRect.left, textRect.bottom - textRect.top, layoutObj.GetAddressOf());
        DWRITE_TEXT_METRICS tm {};
        layoutObj->GetMetrics(&tm);
        brush->SetColor(m_url.empty() ? pal.secondary : pal.text);
        m_rt->DrawTextLayout(D2D1::Point2F(textRect.left, textRect.top), layoutObj.Get(), brush.Get());
        if (secure) {
            float lockX = textRect.left + tm.left - 10;
            brush->SetColor(pal.secondary);
            drawLock(m_rt.Get(), D2D1::Point2F(lockX, (f.top + f.bottom) / 2), brush.Get());
        }

        // Reload / stop at the trailing edge.
        auto rr = D2D1::RectF(f.right - tokens::kFieldHeight, f.top, f.right, f.bottom);
        if (m_hover == Hit::Reload || m_pressed == Hit::Reload) {
            brush->SetColor(m_pressed == Hit::Reload ? pal.pressed : pal.hover);
            m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F((rr.left + rr.right) / 2, (rr.top + rr.bottom) / 2), 13, 13), brush.Get());
        }
        brush->SetColor(pal.text);
        auto c = D2D1::Point2F((rr.left + rr.right) / 2, (rr.top + rr.bottom) / 2);
        if (m_loading)
            drawStop(m_rt.Get(), c, brush.Get());
        else
            drawReload(m_rt.Get(), m_d2d.Get(), c, brush.Get());
    }

    // Banner (load errors, crash recovery).
    if (!m_banner.empty()) {
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        auto br = D2D1::RectF(0, tokens::kToolbarHeight, rc.right / dpiScale(), tokens::kToolbarHeight + tokens::kBannerHeight);
        brush->SetColor(pal.banner);
        m_rt->FillRectangle(br, brush.Get());
        brush->SetColor(pal.text);
        m_rt->DrawTextW(m_banner.c_str(), UINT32(m_banner.size()), m_hostFormat.Get(), br, brush.Get());
    }

    if (m_rt->EndDraw() == D2DERR_RECREATE_TARGET)
        discardDeviceResources();
}

// ---- address editing --------------------------------------------------------

void BrowserWindow::beginEditingAddress(bool selectAll)
{
    if (!m_editing) {
        m_editing = true;
        SetWindowTextW(m_address, m_url.c_str());
        ShowWindow(m_address, SW_SHOW);
    }
    SetFocus(m_address);
    if (selectAll)
        SendMessageW(m_address, EM_SETSEL, 0, -1);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void BrowserWindow::endEditingAddress(bool revert)
{
    if (!m_editing)
        return;
    m_editing = false;
    if (revert)
        SetWindowTextW(m_address, m_url.c_str());
    ShowWindow(m_address, SW_HIDE);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void BrowserWindow::navigateFromAddressBar()
{
    int len = GetWindowTextLengthW(m_address);
    std::wstring text(len, L'\0');
    GetWindowTextW(m_address, text.data(), len + 1);
    auto fixed = services::fixupInput(text, brand::kSearchURLPrefix);
    if (fixed.url.empty())
        return;
    m_url = fixed.url;
    m_banner.clear();
    endEditingAddress(false);
    layout();
    m_view->loadURL(fixed.url);
    SetFocus(m_view->hwnd());
}

void BrowserWindow::showBanner(const std::wstring& text)
{
    m_banner = text;
    layout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    SetTimer(m_hwnd, 1, 6000, nullptr);
}

LRESULT CALLBACK BrowserWindow::addressProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref)
{
    auto* self = reinterpret_cast<BrowserWindow*>(ref);
    switch (msg) {
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DefSubclassProc(h, msg, wp, lp);
    case WM_KEYDOWN:
        if (wp == VK_RETURN) {
            self->navigateFromAddressBar();
            return 0;
        }
        if (wp == VK_ESCAPE) {
            self->endEditingAddress(true);
            SetFocus(self->m_view->hwnd());
            return 0;
        }
        break;
    case WM_CHAR:
        if (wp == VK_RETURN || wp == VK_ESCAPE)
            return 0; // suppress beep
        break;
    case WM_KILLFOCUS:
        self->endEditingAddress(true);
        break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// ---- input ------------------------------------------------------------------

bool BrowserWindow::handleShortcut(const MSG& msg)
{
    if (msg.message == WM_XBUTTONUP) {
        if (GET_XBUTTON_WPARAM(msg.wParam) == XBUTTON1)
            m_view->goBack();
        else
            m_view->goForward();
        return true;
    }
    if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN)
        return false;
    bool ctrl = GetKeyState(VK_CONTROL) & 0x8000;
    bool alt = GetKeyState(VK_MENU) & 0x8000;
    bool shift = GetKeyState(VK_SHIFT) & 0x8000;
    WPARAM k = msg.wParam;

    if ((ctrl && k == 'L') || (alt && k == 'D') || k == VK_F6) {
        beginEditingAddress(true);
        return true;
    }
    if ((ctrl && k == 'R') || k == VK_F5) {
        m_view->reload();
        return true;
    }
    if ((alt && k == VK_LEFT) || (ctrl && k == VK_OEM_4)) {
        m_view->goBack();
        return true;
    }
    if ((alt && k == VK_RIGHT) || (ctrl && k == VK_OEM_6)) {
        m_view->goForward();
        return true;
    }
    if (k == VK_F12 || (ctrl && alt && k == 'I') || (ctrl && shift && k == 'I')) {
        m_view->showInspector();
        return true;
    }
    if (k == VK_ESCAPE && m_loading && !m_editing) {
        m_view->stop();
        return true;
    }
    return false;
}

LRESULT BrowserWindow::handle(UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT dwmResult = 0;
    if (DwmDefWindowProc(m_hwnd, msg, wp, lp, &dwmResult))
        return dwmResult; // caption buttons drawn/hit-tested by DWM

    switch (msg) {
    case WM_CREATE:
        onCreate();
        return 0;

    case WM_NCCALCSIZE:
        if (wp) {
            // Remove the standard title bar; keep the resize borders.
            auto& p = *reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
            int fx = GetSystemMetricsForDpi(SM_CXFRAME, m_dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, m_dpi);
            int fy = GetSystemMetricsForDpi(SM_CYFRAME, m_dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, m_dpi);
            p.rgrc[0].left += fx;
            p.rgrc[0].right -= fx;
            p.rgrc[0].bottom -= fy;
            if (IsZoomed(m_hwnd))
                p.rgrc[0].top += fy;
            return 0;
        }
        break;

    case WM_NCHITTEST: {
        POINT pt { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(m_hwnd, &pt);
        int frame = GetSystemMetricsForDpi(SM_CYFRAME, m_dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, m_dpi);
        if (!IsZoomed(m_hwnd) && pt.y < frame)
            return HTTOP;
        LRESULT def = DefWindowProcW(m_hwnd, msg, wp, lp);
        if (def != HTCLIENT)
            return def;
        return hitTest(pt) == Hit::Caption ? HTCAPTION : HTCLIENT;
    }

    case WM_ACTIVATE:
        updateTheme();
        if (LOWORD(wp) != WA_INACTIVE && m_view && !m_editing)
            SetFocus(m_view->hwnd());
        return 0;

    case WM_SIZE:
        layout();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;

    case WM_DPICHANGED: {
        m_dpi = HIWORD(wp);
        auto* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        updateTheme();
        return 0;
    }

    case WM_SETTINGCHANGE:
        if (lp && (wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0))
            updateTheme();
        return 0;

    case WM_THEMECHANGED:
        updateTheme();
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(m_hwnd, &ps);
        paint();
        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEMOVE: {
        Hit h = hitTest({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        if (h != m_hover) {
            m_hover = h;
            TRACKMOUSEEVENT tme { sizeof(tme), TME_LEAVE, m_hwnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        m_hover = Hit::None;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
        m_pressed = hitTest({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        if (m_pressed == Hit::Address) {
            beginEditingAddress(true);
            m_pressed = Hit::None;
        } else
            SetCapture(m_hwnd);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONUP: {
        ReleaseCapture();
        Hit h = hitTest({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        if (h == m_pressed) {
            if (h == Hit::Back)
                m_view->goBack();
            else if (h == Hit::Forward)
                m_view->goForward();
            else if (h == Hit::Reload)
                m_loading ? m_view->stop() : m_view->reload();
        }
        m_pressed = Hit::None;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_CTLCOLOREDIT: {
        auto pal = paletteFor(m_theme);
        HDC dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, toColorRef(pal.text));
        SetBkColor(dc, toColorRef(pal.fieldFocused));
        return reinterpret_cast<LRESULT>(m_editBrush);
    }

    case WM_TIMER:
        if (wp == 1) {
            KillTimer(m_hwnd, 1);
            m_banner.clear();
            layout();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_APPCOMMAND:
        switch (GET_APPCOMMAND_LPARAM(lp)) {
        case APPCOMMAND_BROWSER_BACKWARD: m_view->goBack(); return TRUE;
        case APPCOMMAND_BROWSER_FORWARD: m_view->goForward(); return TRUE;
        case APPCOMMAND_BROWSER_REFRESH: m_view->reload(); return TRUE;
        }
        break;

    case WM_DESTROY:
        m_view.reset();
        if (m_editFont)
            DeleteObject(m_editFont);
        if (m_editBrush)
            DeleteObject(m_editBrush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

} // namespace meridian::ui
