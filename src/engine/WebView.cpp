#include "WebView.h"

#include "platform/CertificateTrust.h"

#include <WebKit/WKArray.h>
#include <WebKit/WKAuthenticationChallenge.h>
#include <WebKit/WKAuthenticationDecisionListener.h>
#include <WebKit/WKContext.h>
#include <WebKit/WKData.h>
#include <WebKit/WKFrameInfoRef.h>
#include <WebKit/WKFramePolicyListener.h>
#include <WebKit/WKNavigationActionRef.h>
#include <WebKit/WKPageLoadTypes.h>
#include <WebKit/WKPageUIClient.h>
#include <WebKit/WKSessionStateRef.h>
#include <WebKit/WKURLRequest.h>
#include <WebKit/WKProtectionSpace.h>
#include <WebKit/WKProtectionSpaceCurl.h>
#include <WebKit/WKContextConfigurationRef.h>
#include <WebKit/WKErrorRef.h>
#include <WebKit/WKInspector.h>
#include <WebKit/WKPage.h>
#include <WebKit/WKPageNavigationClient.h>
#include <WebKit/WKPageStateClient.h>
#include <WebKit/WKPreferencesRef.h>
#include <WebKit/WKPreferencesRefPrivate.h>
#include <WebKit/WKString.h>
#include <WebKit/WKURL.h>
#include <WebKit/WKWebsiteDataStoreConfigurationRef.h>
#include <WebKit/WKWebsiteDataStoreRef.h>

#include <filesystem>
#include <memory>
#include <shlobj.h>
#include <thread>
#include <utility>
#include <vector>

namespace meridian::engine {

// ---- string helpers -------------------------------------------------------

std::wstring toWString(WKStringRef s)
{
    if (!s)
        return {};
    size_t max = WKStringGetMaximumUTF8CStringSize(s);
    std::vector<char> buf(max + 1);
    size_t len = WKStringGetUTF8CString(s, buf.data(), buf.size());
    if (len && buf[len - 1] == '\0')
        --len;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, buf.data(), static_cast<int>(len), nullptr, 0);
    std::wstring out(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, buf.data(), static_cast<int>(len), out.data(), wlen);
    return out;
}

std::wstring toWString(WKURLRef u)
{
    if (!u)
        return {};
    auto s = adoptWK(WKURLCopyString(u));
    return toWString(s.get());
}

static std::string toUTF8(const std::wstring& w)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), len, nullptr, nullptr);
    return out;
}

WKPtr<WKStringRef> makeWKString(const std::wstring& w)
{
    return adoptWK(WKStringCreateWithUTF8CString(toUTF8(w).c_str()));
}

WKPtr<WKURLRef> makeWKURL(const std::wstring& w)
{
    return adoptWK(WKURLCreateWithUTF8CString(toUTF8(w).c_str()));
}

// ---- EngineContext --------------------------------------------------------

EngineContext::EngineContext(bool ephemeral, const std::filesystem::path& profile)
    : m_ephemeral(ephemeral)
{
    auto contextConf = adoptWK(WKContextConfigurationCreate());
    m_context = adoptWK(WKContextCreateWithConfiguration(contextConf.get()));

    if (ephemeral) {
        // Private browsing: nothing touches disk.
        m_dataStore = adoptWK(WKWebsiteDataStoreCreateNonPersistentDataStore());
    } else {
        const auto& dir = profile;
        auto conf = adoptWK(WKWebsiteDataStoreConfigurationCreate());
        auto sub = [&](const wchar_t* name) { return makeWKString((dir / name).wstring()); };
        WKWebsiteDataStoreConfigurationSetGeneralStorageDirectory(conf.get(), sub(L"Storage").get());
        WKWebsiteDataStoreConfigurationSetNetworkCacheDirectory(conf.get(), sub(L"Cache").get());
        WKWebsiteDataStoreConfigurationSetLocalStorageDirectory(conf.get(), sub(L"LocalStorage").get());
        WKWebsiteDataStoreConfigurationSetIndexedDBDatabaseDirectory(conf.get(), sub(L"IndexedDB").get());
        WKWebsiteDataStoreConfigurationSetCacheStorageDirectory(conf.get(), sub(L"CacheStorage").get());
        WKWebsiteDataStoreConfigurationSetServiceWorkerRegistrationDirectory(conf.get(), sub(L"ServiceWorkers").get());
        WKWebsiteDataStoreConfigurationSetResourceLoadStatisticsDirectory(conf.get(), sub(L"ResourceLoadStatistics").get());
        WKWebsiteDataStoreConfigurationSetCookieStorageFile(conf.get(), sub(L"Cookies.db").get());
        m_dataStore = adoptWK(WKWebsiteDataStoreCreateWithConfiguration(conf.get()));
    }

    m_preferences = adoptWK(WKPreferencesCreate());
    WKPreferencesSetDeveloperExtrasEnabled(m_preferences.get(), true); // Web Inspector
    WKPreferencesSetJavaScriptEnabled(m_preferences.get(), true);
}

WKPageConfigurationRef EngineContext::newPageConfiguration() const
{
    WKPageConfigurationRef conf = WKPageConfigurationCreate();
    WKPageConfigurationSetContext(conf, m_context.get());
    WKPageConfigurationSetWebsiteDataStore(conf, m_dataStore.get());
    WKPageConfigurationSetPreferences(conf, m_preferences.get());
    return conf; // caller adopts
}

// ---- input hints for "open link in new tab" ---------------------------------

namespace {
struct ClickHint {
    DWORD tick { 0 };
    bool middle { false };
    bool ctrl { false };
    bool shift { false };
};
ClickHint g_lastClick;
std::set<const WebView*> g_liveViews; // guards async callbacks against destroyed views
uint64_t g_nextSerial = 0;

struct ScriptRequest {
    const WebView* view;
    uint64_t serial;
    std::function<void(const std::wstring&)> done;
};
}

void WebView::noteInput(const MSG& msg)
{
    if (msg.message == WM_MBUTTONUP || msg.message == WM_LBUTTONUP) {
        g_lastClick.tick = GetTickCount();
        g_lastClick.middle = msg.message == WM_MBUTTONUP;
        g_lastClick.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        g_lastClick.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    }
}

// ---- WebView --------------------------------------------------------------

struct WebViewThunks {
    static WebView& self(const void* info) { return *const_cast<WebView*>(static_cast<const WebView*>(info)); }

    static void titleChanged(const void* info)
    {
        auto& v = self(info);
        if (v.m_callbacks.titleChanged)
            v.m_callbacks.titleChanged(v.title());
    }
    static void urlChanged(const void* info)
    {
        auto& v = self(info);
        if (v.m_callbacks.urlChanged)
            v.m_callbacks.urlChanged(v.url());
    }
    static void progressChanged(const void* info)
    {
        auto& v = self(info);
        if (v.m_callbacks.progressChanged)
            v.m_callbacks.progressChanged(v.progress());
    }
    static void willChangeLoading(const void* info)
    {
        auto& v = self(info);
        v.m_isLoading = !v.m_isLoading; // state client signals a flip
    }
    static void loadingChanged(const void* info)
    {
        auto& v = self(info);
        if (v.m_callbacks.loadingChanged)
            v.m_callbacks.loadingChanged(v.m_isLoading);
    }
    static void backForwardChanged(const void* info)
    {
        auto& v = self(info);
        if (v.m_callbacks.backForwardChanged)
            v.m_callbacks.backForwardChanged();
    }

    static void decidePolicyForNavigationAction(WKPageRef, WKNavigationActionRef action, WKFramePolicyListenerRef listener, WKTypeRef, const void* info)
    {
        auto& v = self(info);
        bool linkClick = WKNavigationActionGetNavigationType(action) == kWKFrameNavigationTypeLinkClicked;
        bool recentClick = GetTickCount() - g_lastClick.tick < 600;
        if (linkClick && recentClick && (g_lastClick.middle || g_lastClick.ctrl) && v.m_callbacks.openInNewTab) {
            auto request = adoptWK(WKNavigationActionCopyRequest(action));
            auto url = toWString(adoptWK(WKURLRequestCopyURL(request.get())).get());
            g_lastClick.tick = 0; // one click -> one tab
            if (!url.empty()) {
                WKFramePolicyListenerIgnore(listener);
                // Safari: Ctrl+click opens in background; Ctrl+Shift+click in front.
                v.m_callbacks.openInNewTab(url, g_lastClick.shift);
                return;
            }
        }
        WKFramePolicyListenerUse(listener);
    }

    static void decidePolicyForNavigationResponse(WKPageRef, WKNavigationResponseRef, WKFramePolicyListenerRef listener, WKTypeRef, const void*)
    {
        WKFramePolicyListenerUse(listener); // downloads arrive in M6
    }

    static void didCommitNavigation(WKPageRef, WKNavigationRef, WKTypeRef, const void* info)
    {
        auto& v = self(info);
        if (v.m_callbacks.navigationCommitted)
            v.m_callbacks.navigationCommitted(v.url());
    }

    static void didFinishNavigation(WKPageRef, WKNavigationRef, WKTypeRef, const void* info)
    {
        auto& v = self(info);
        v.requestFavicon();
        if (v.m_callbacks.loadFinished)
            v.m_callbacks.loadFinished();
    }

    static void didFailProvisional(WKPageRef, WKNavigationRef, WKErrorRef error, WKTypeRef, const void* info)
    {
        auto& v = self(info);
        auto desc = toWString(adoptWK(WKErrorCopyLocalizedDescription(error)).get());
        auto failingURL = adoptWK(WKErrorCopyFailingURL(error));
        auto url = toWString(failingURL.get());
        auto host = failingURL ? toWString(adoptWK(WKURLCopyHostName(failingURL.get())).get()) : std::wstring();
        if (v.m_trustPending.count(host)) {
            // Windows is still verifying this host's chain; decide after it finishes.
            v.m_failedWhilePending = url;
            v.m_failedDescription = desc;
            return;
        }
        if (v.m_callbacks.loadFailed)
            v.m_callbacks.loadFailed(url, desc);
    }

    static void didReceiveAuthenticationChallenge(WKPageRef, WKAuthenticationChallengeRef challenge, const void* info)
    {
        auto& v = self(info);
        auto space = WKAuthenticationChallengeGetProtectionSpace(challenge);
        auto listener = WKAuthenticationChallengeGetDecisionListener(challenge);

        if (WKProtectionSpaceGetAuthenticationScheme(space) != kWKProtectionSpaceAuthenticationSchemeServerTrustEvaluationRequested) {
            // HTTP auth UI is not implemented yet (M6): continue without credentials.
            WKAuthenticationDecisionListenerUseCredential(listener, nullptr);
            return;
        }

        // Never accept here: accepting makes WebKit retry with verification
        // disabled. Reject, then verify with Windows off the UI thread.
        WKAuthenticationDecisionListenerCancel(listener);

        auto host = toWString(adoptWK(WKProtectionSpaceCopyHost(space)).get());
        if (v.m_trustRetried.count(host) || v.m_trustPending.count(host))
            return; // already tried once; let the failure surface

        std::vector<std::string> pems;
        auto chain = adoptWK(WKProtectionSpaceCopyCertificateChain(space));
        for (size_t i = 0; chain && i < WKArrayGetSize(chain.get()); ++i) {
            auto item = WKArrayGetItemAtIndex(chain.get(), i);
            if (WKGetTypeID(item) != WKDataGetTypeID())
                continue;
            auto data = static_cast<WKDataRef>(item);
            pems.emplace_back(reinterpret_cast<const char*>(WKDataGetBytes(data)), WKDataGetSize(data));
        }

        v.m_trustPending.insert(host);
        HWND reply = v.m_messageWindow;
        std::thread([reply, host, pems = std::move(pems)] {
            auto result = platform::verifyServerChain(pems, host);
            auto* msg = new TrustMessage { host, result.trusted, result.description };
            if (!PostMessageW(reply, kTrustMessage, 0, reinterpret_cast<LPARAM>(msg)))
                delete msg; // view already destroyed
        }).detach();
    }

    struct TrustMessage {
        std::wstring host;
        bool trusted;
        std::wstring description;
    };
    static constexpr UINT kTrustMessage = WM_APP + 1;

    static LRESULT CALLBACK messageProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == kTrustMessage) {
            std::unique_ptr<TrustMessage> m(reinterpret_cast<TrustMessage*>(lp));
            if (auto* v = reinterpret_cast<WebView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
                v->onTrustVerified(m->host, m->trusted, m->description);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    static void webProcessDidCrash(WKPageRef, const void* info)
    {
        auto& v = self(info);
        v.m_isLoading = false;
        if (v.m_callbacks.webProcessCrashed)
            v.m_callbacks.webProcessCrashed();
    }

    // UI client
    static WKPageRef createNewPage(WKPageRef, WKPageConfigurationRef conf, WKNavigationActionRef, WKWindowFeaturesRef, const void* info)
    {
        auto& v = self(info);
        if (!v.m_callbacks.createNewPage)
            return nullptr;
        WebView* created = v.m_callbacks.createNewPage(conf);
        if (!created)
            return nullptr;
        WKPageRef page = created->page();
        WKRetain(page); // WebKit adopts the returned reference
        return page;
    }
    static void close(WKPageRef, const void* info)
    {
        auto& v = self(info);
        if (v.m_callbacks.closeRequested)
            v.m_callbacks.closeRequested();
    }

    static void scriptDone(WKTypeRef result, WKErrorRef, void* context)
    {
        std::unique_ptr<ScriptRequest> request(static_cast<ScriptRequest*>(context));
        if (!g_liveViews.count(request->view) || request->view->m_serial != request->serial || !request->done)
            return;
        std::wstring text;
        if (result && WKGetTypeID(result) == WKStringGetTypeID())
            text = toWString(static_cast<WKStringRef>(result));
        request->done(text);
    }

    static void faviconScriptDone(WKTypeRef result, WKErrorRef, void* context)
    {
        auto* v = static_cast<WebView*>(context);
        if (!g_liveViews.count(v) || !result || WKGetTypeID(result) != WKStringGetTypeID())
            return;
        auto iconURL = toWString(static_cast<WKStringRef>(result));
        if (!iconURL.empty() && v->m_callbacks.faviconURLChanged)
            v->m_callbacks.faviconURLChanged(iconURL);
    }
};

WebView::WebView(EngineContext& engine, HWND parent)
{
    auto conf = adoptWK(engine.newPageConfiguration());
    initialize(conf.get(), parent);
}

WebView::WebView(WKPageConfigurationRef conf, HWND parent)
{
    initialize(conf, parent);
}

void WebView::initialize(WKPageConfigurationRef conf, HWND parent)
{
    RECT r {};
    m_view = adoptWK(WKViewCreate(r, conf, parent));
    WKViewSetIsInWindow(m_view.get(), true);

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc {};
        wc.lpfnWndProc = WebViewThunks::messageProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"MeridianEngineMessages";
        RegisterClassW(&wc);
        registered = true;
    }
    m_messageWindow = CreateWindowW(L"MeridianEngineMessages", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(m_messageWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    g_liveViews.insert(this);
    m_serial = ++g_nextSerial;
    installClients();
}

WebView::~WebView()
{
    g_liveViews.erase(this);
    // Teardown makes WebKit fire state/navigation callbacks (e.g. loading
    // changed). Unhook every client first so none reach a half-destroyed tab.
    m_callbacks = {};
    if (m_view) {
        WKPageSetPageStateClient(page(), nullptr);
        WKPageSetPageNavigationClient(page(), nullptr);
        WKPageSetPageUIClient(page(), nullptr);
    }
    if (m_messageWindow) {
        SetWindowLongPtrW(m_messageWindow, GWLP_USERDATA, 0);
        DestroyWindow(m_messageWindow);
    }
    // Destroying WKView's HWND makes WebKit close the page (WebView::closeInternal).
    if (m_view && IsWindow(hwnd()))
        DestroyWindow(hwnd());
}

void WebView::setCallbacks(WebViewCallbacks callbacks) { m_callbacks = std::move(callbacks); }

void WebView::onTrustVerified(const std::wstring& host, bool trusted, const std::wstring& description)
{
    m_trustPending.erase(host);
    m_trustRetried.insert(host);
    std::wstring failed = std::exchange(m_failedWhilePending, {});
    std::wstring failedDescription = std::exchange(m_failedDescription, {});
    if (failed.empty())
        return; // a subresource; the page itself loaded
    if (trusted) {
        // Windows trusts the chain and has installed any missing root, so a
        // fresh, fully verified connection should now succeed.
        loadURL(failed);
        return;
    }
    if (m_callbacks.loadFailed)
        m_callbacks.loadFailed(failed, description.empty() ? failedDescription : description);
}

void WebView::installClients()
{
    WKPageStateClientV0 state {};
    state.base.version = 0;
    state.base.clientInfo = this;
    state.willChangeIsLoading = WebViewThunks::willChangeLoading;
    state.didChangeIsLoading = WebViewThunks::loadingChanged;
    state.didChangeTitle = WebViewThunks::titleChanged;
    state.didChangeActiveURL = WebViewThunks::urlChanged;
    state.didChangeEstimatedProgress = WebViewThunks::progressChanged;
    state.didChangeCanGoBack = WebViewThunks::backForwardChanged;
    state.didChangeCanGoForward = WebViewThunks::backForwardChanged;
    WKPageSetPageStateClient(page(), &state.base);

    WKPageNavigationClientV0 nav {};
    nav.base.version = 0;
    nav.base.clientInfo = this;
    nav.decidePolicyForNavigationAction = WebViewThunks::decidePolicyForNavigationAction;
    nav.decidePolicyForNavigationResponse = WebViewThunks::decidePolicyForNavigationResponse;
    nav.didCommitNavigation = WebViewThunks::didCommitNavigation;
    nav.didFinishNavigation = WebViewThunks::didFinishNavigation;
    nav.didFailProvisionalNavigation = WebViewThunks::didFailProvisional;
    nav.didReceiveAuthenticationChallenge = WebViewThunks::didReceiveAuthenticationChallenge;
    nav.webProcessDidCrash = WebViewThunks::webProcessDidCrash;
    WKPageSetPageNavigationClient(page(), &nav.base);

    WKPageUIClientV13 ui {};
    ui.base.version = 13;
    ui.base.clientInfo = this;
    ui.createNewPage = WebViewThunks::createNewPage;
    ui.close = WebViewThunks::close;
    WKPageSetPageUIClient(page(), &ui.base);
}

void WebView::requestFavicon()
{
    // Picks the best raster icon the page declares (SVG is not decoded yet),
    // falling back to /favicon.ico. Runs in the page's world, so the result is
    // treated as untrusted input by the loader (scheme/size limits).
    static const wchar_t* script = LR"JS((() => {
  let best = null, bestScore = -1e9;
  for (const l of document.querySelectorAll('link[rel]')) {
    const rel = (l.getAttribute('rel') || '').toLowerCase();
    if (!/(^|\s)(icon|shortcut|apple-touch-icon)(\s|$)/.test(rel) || !l.href) continue;
    const type = (l.type || '').toLowerCase();
    if (type.includes('svg') || /\.svg(\?|#|$)/i.test(l.href)) continue;
    const size = parseInt((l.getAttribute('sizes') || '').split(/[x\s]/)[0]) || 0;
    let score = size ? 100 - Math.abs(size - 32) : 40;
    if (rel.includes('apple-touch-icon')) score -= 30;
    if (score > bestScore) { bestScore = score; best = l.href; }
  }
  if (best) return best;
  return /^https?:$/.test(location.protocol) ? new URL('/favicon.ico', location.href).href : '';
})())JS";
    WKPageEvaluateJavaScriptInMainFrame(page(), makeWKString(script).get(), this, WebViewThunks::faviconScriptDone);
}

void WebView::evaluateScript(const std::wstring& script, std::function<void(const std::wstring&)> done)
{
    auto* request = new ScriptRequest { this, m_serial, std::move(done) };
    WKPageEvaluateJavaScriptInMainFrame(page(), makeWKString(script).get(), request, WebViewThunks::scriptDone);
}

WKPageRef WebView::page() const { return WKViewGetPage(m_view.get()); }
HWND WebView::hwnd() const { return WKViewGetWindow(m_view.get()); }

void WebView::setParentWindow(HWND parent)
{
    WKViewSetParentWindow(m_view.get(), parent);
    WKViewWindowAncestryDidChange(m_view.get());
}

void WebView::setBounds(const RECT& r)
{
    SetWindowPos(hwnd(), nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

void WebView::setVisible(bool visible)
{
    ShowWindow(hwnd(), visible ? SW_SHOW : SW_HIDE);
    // Hidden tabs are "not in window", so WebKit throttles timers and painting.
    WKViewSetIsInWindow(m_view.get(), visible);
}

void WebView::loadURL(const std::wstring& url) { WKPageLoadURL(page(), makeWKURL(url).get()); }
void WebView::goBack() { WKPageGoBack(page()); }
void WebView::goForward() { WKPageGoForward(page()); }
void WebView::reload() { WKPageReload(page()); }
void WebView::stop() { WKPageStopLoading(page()); }
void WebView::showInspector() { WKInspectorShow(WKPageGetInspector(page())); }

bool WebView::canGoBack() const { return WKPageCanGoBack(page()); }
bool WebView::canGoForward() const { return WKPageCanGoForward(page()); }
bool WebView::isLoading() const { return m_isLoading; }
double WebView::progress() const { return WKPageGetEstimatedProgress(page()); }
std::wstring WebView::title() const { return toWString(adoptWK(WKPageCopyTitle(page())).get()); }
std::wstring WebView::url() const { return toWString(adoptWK(WKPageCopyActiveURL(page())).get()); }

std::vector<uint8_t> WebView::sessionState() const
{
    // With a null context, WKPageCopySessionState returns a WKDataRef holding
    // WebKit's "legacy" encoding (a compatibility hack in WKPage.cpp); a
    // context with the low bit set would return a WKSessionStateRef instead.
    // On non-Cocoa ports that encoding is WebKit's IPC wire format, so it is
    // only guaranteed to decode with the same WebKit build (see restore).
    auto state = adoptWK(WKPageCopySessionState(page(), nullptr, nullptr));
    if (!state)
        return {};
    WKPtr<WKDataRef> data;
    if (WKGetTypeID(state.get()) == WKDataGetTypeID())
        data = WKPtr<WKDataRef>::retain(static_cast<WKDataRef>(state.get()));
    else if (WKGetTypeID(state.get()) == WKSessionStateGetTypeID())
        data = adoptWK(WKSessionStateCopyData(static_cast<WKSessionStateRef>(state.get())));
    if (!data)
        return {};
    const uint8_t* bytes = WKDataGetBytes(data.get());
    return { bytes, bytes + WKDataGetSize(data.get()) };
}

bool WebView::restoreSessionState(const std::vector<uint8_t>& bytes)
{
    if (bytes.empty())
        return false;
    auto data = adoptWK(WKDataCreate(bytes.data(), bytes.size()));
    // Returns null if the blob does not decode (e.g. saved by another WebKit build).
    auto state = adoptWK(WKSessionStateCreateFromData(data.get()));
    if (!state)
        return false;
    WKPageRestoreFromSessionState(page(), state.get());
    return true;
}

} // namespace meridian::engine

