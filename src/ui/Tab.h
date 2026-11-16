#pragma once
// ui/: one browser tab. The WebView is created lazily, so restored background
// tabs cost nothing until selected (Safari behaves the same way).
#include "engine/WebView.h"
#include "services/Favicons.h"

#include <cstdint>
#include <d2d1_1.h>
#include <memory>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace meridian::ui {

struct Tab {
    uint64_t id { 0 };
    std::unique_ptr<engine::WebView> view;   // null until first shown
    std::vector<uint8_t> pendingState;       // session state to restore on first show
    std::wstring url;
    std::wstring title;
    std::wstring banner;                     // per-tab error / crash notice
    bool pinned { false };
    bool loading { false };
    double progress { 0 };

    services::Bitmap favicon;
    std::wstring faviconHost;                // host the favicon belongs to
    services::Bitmap thumbnail;
    std::wstring thumbnailURL;
    ULONGLONG thumbnailTick { 0 };

    // Device-dependent caches (reset when the render target is recreated).
    Microsoft::WRL::ComPtr<ID2D1Bitmap> faviconBitmap;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> thumbnailBitmap;

    std::wstring displayTitle() const;
};

} // namespace meridian::ui
