#pragma once
// engine/: thin wrapper over the WebKit2 C API (WKView/WKPage) for one tab.
#include "WKPtr.h"
#include <WebKit/WKPageConfigurationRef.h>
#include <WebKit/WKView.h>
#include <functional>
#include <string>
#include <windows.h>

namespace meridian::engine {

struct WebViewCallbacks {
    std::function<void(const std::wstring&)> titleChanged;
    std::function<void(const std::wstring&)> urlChanged;
    std::function<void(double)> progressChanged;      // 0..1
    std::function<void(bool)> loadingChanged;
    std::function<void()> backForwardChanged;
    std::function<void(const std::wstring& url, const std::wstring& description)> loadFailed;
    std::function<void()> webProcessCrashed;
};

// Shared engine objects (one per profile; private windows get their own).
class EngineContext {
public:
    explicit EngineContext(bool ephemeral);
    WKPageConfigurationRef newPageConfiguration() const;
    bool isEphemeral() const { return m_ephemeral; }
private:
    bool m_ephemeral;
    WKPtr<WKContextRef> m_context;
    WKPtr<WKWebsiteDataStoreRef> m_dataStore;
    WKPtr<WKPreferencesRef> m_preferences;
};

class WebView {
public:
    WebView(EngineContext&, HWND parent, WebViewCallbacks);
    ~WebView();
    WebView(const WebView&) = delete;
    WebView& operator=(const WebView&) = delete;

    HWND hwnd() const;
    void setBounds(const RECT&);
    void setVisible(bool);

    void loadURL(const std::wstring&);
    void goBack();
    void goForward();
    void reload();
    void stop();
    void showInspector();

    bool canGoBack() const;
    bool canGoForward() const;
    bool isLoading() const;
    double progress() const;
    std::wstring title() const;
    std::wstring url() const;

private:
    WKPageRef page() const;
    void installClients();

    WebViewCallbacks m_callbacks;
    WKPtr<WKViewRef> m_view;
    bool m_isLoading { false };

    friend struct WebViewThunks;
};

} // namespace meridian::engine
