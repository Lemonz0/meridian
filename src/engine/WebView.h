#pragma once
// engine/: thin wrapper over the WebKit2 C API (WKView/WKPage) for one tab.
#include "WKPtr.h"
#include <WebKit/WKPageConfigurationRef.h>
#include <WebKit/WKView.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>
#include <windows.h>

namespace meridian::engine {

class WebView;

struct WebViewCallbacks {
    std::function<void(const std::wstring&)> titleChanged;
    std::function<void(const std::wstring&)> urlChanged;
    std::function<void(double)> progressChanged;      // 0..1
    std::function<void(bool)> loadingChanged;
    std::function<void()> backForwardChanged;
    std::function<void(const std::wstring& url, const std::wstring& description)> loadFailed;
    std::function<void()> webProcessCrashed;
    std::function<void(const std::wstring& iconURL)> faviconURLChanged;
    // target=_blank / window.open: the UI creates a tab for this configuration
    // and returns its view (or nullptr to block the popup).
    std::function<WebView*(WKPageConfigurationRef)> createNewPage;
    // Ctrl+click / middle-click on a link.
    std::function<void(const std::wstring& url, bool foreground)> openInNewTab;
    std::function<void()> closeRequested;               // window.close()
};

// Shared engine objects (one per profile; private windows get their own).
class EngineContext {
public:
    EngineContext(bool ephemeral, const std::filesystem::path& profile);
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
    WebView(EngineContext&, HWND parent);
    // For pages WebKit creates itself (createNewPage); the configuration
    // carries the opener relationship.
    WebView(WKPageConfigurationRef, HWND parent);
    ~WebView();
    WebView(const WebView&) = delete;
    WebView& operator=(const WebView&) = delete;

    void setCallbacks(WebViewCallbacks);
    HWND hwnd() const;
    void setParentWindow(HWND);
    void setBounds(const RECT&);
    void setVisible(bool);
    WKPageRef page() const;

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

    // Back/forward list + current item, serialized by WebKit. Restoring
    // navigates to the current item; returns false if the data can't be
    // decoded (caller should then just load the URL).
    std::vector<uint8_t> sessionState() const;
    bool restoreSessionState(const std::vector<uint8_t>&);

    // Called from the app's message loop so link clicks can be classified as
    // "open in new tab" (the C API's navigation action has no modifier info).
    static void noteInput(const MSG&);

private:
    void initialize(WKPageConfigurationRef, HWND parent);
    void installClients();
    void requestFavicon();
    void onTrustVerified(const std::wstring& host, bool trusted, const std::wstring& description);

    WebViewCallbacks m_callbacks;
    WKPtr<WKViewRef> m_view;
    bool m_isLoading { false };

    // Server-trust fallback (see platform/CertificateTrust.h). Curl verification
    // is never disabled: we reject, verify with Windows (which installs missing
    // roots), then retry the navigation once with full verification.
    HWND m_messageWindow { nullptr };
    std::set<std::wstring> m_trustPending;   // hosts being verified
    std::set<std::wstring> m_trustRetried;   // hosts already retried once
    std::wstring m_failedWhilePending;       // URL whose load failed during verification
    std::wstring m_failedDescription;

    friend struct WebViewThunks;
};

} // namespace meridian::engine
