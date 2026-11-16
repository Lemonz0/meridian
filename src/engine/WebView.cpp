#include "WebView.h"

#include <WebKit/WKContext.h>
#include <WebKit/WKContextConfigurationRef.h>
#include <WebKit/WKError.h>
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
#include <shlobj.h>
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

static std::filesystem::path profileDirectory()
{
    PWSTR base = nullptr;
    std::filesystem::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)))
        dir = std::filesystem::path(base) / L"Meridian" / L"Default";
    CoTaskMemFree(base);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

EngineContext::EngineContext(bool ephemeral)
    : m_ephemeral(ephemeral)
{
    auto contextConf = adoptWK(WKContextConfigurationCreate());
    m_context = adoptWK(WKContextCreateWithConfiguration(contextConf.get()));

    if (ephemeral) {
        // Private browsing: nothing touches disk.
        m_dataStore = adoptWK(WKWebsiteDataStoreCreateNonPersistentDataStore());
    } else {
        auto dir = profileDirectory();
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
    static void didFailProvisional(WKPageRef, WKNavigationRef, WKErrorRef error, WKTypeRef, const void* info)
    {
        auto& v = self(info);
        auto desc = toWString(adoptWK(WKErrorCopyLocalizedDescription(error)).get());
        auto url = toWString(adoptWK(WKErrorCopyFailingURL(error)).get());
        if (v.m_callbacks.loadFailed)
            v.m_callbacks.loadFailed(url, desc);
    }
    static void webProcessDidCrash(WKPageRef, const void* info)
    {
        auto& v = self(info);
        if (v.m_callbacks.webProcessCrashed)
            v.m_callbacks.webProcessCrashed();
    }
};

WebView::WebView(EngineContext& engine, HWND parent, WebViewCallbacks callbacks)
    : m_callbacks(std::move(callbacks))
{
    auto conf = adoptWK(engine.newPageConfiguration());
    RECT r {};
    m_view = adoptWK(WKViewCreate(r, conf.get(), parent));
    WKViewSetIsInWindow(m_view.get(), true);
    installClients();
}

WebView::~WebView()
{
    if (m_view)
        WKPageClose(page());
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
    nav.didFailProvisionalNavigation = WebViewThunks::didFailProvisional;
    nav.webProcessDidCrash = WebViewThunks::webProcessDidCrash;
    WKPageSetPageNavigationClient(page(), &nav.base);
}

WKPageRef WebView::page() const { return WKViewGetPage(m_view.get()); }
HWND WebView::hwnd() const { return WKViewGetWindow(m_view.get()); }

void WebView::setBounds(const RECT& r)
{
    SetWindowPos(hwnd(), nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

void WebView::setVisible(bool visible)
{
    ShowWindow(hwnd(), visible ? SW_SHOW : SW_HIDE);
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

} // namespace meridian::engine
