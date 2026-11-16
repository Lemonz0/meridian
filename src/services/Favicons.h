#pragma once
// services/: favicon fetching (WinHTTP) and decoding (WIC).
//
// Icon URLs come from page content, so they are untrusted: only http/https/
// data: are allowed, responses are capped at 512 KB and 5 s, images at 1024 px,
// and no cookies are sent. SVG icons are not supported yet.
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace meridian::services {

struct Bitmap {
    int width { 0 };
    int height { 0 };
    std::vector<uint8_t> bgra; // 32bpp premultiplied BGRA, top-down
    explicit operator bool() const { return width > 0 && height > 0; }
};

// Decodes PNG/ICO/JPEG/GIF/BMP/WebP (whatever WIC supports) and scales to fit
// sizePx x sizePx. For multi-image ICOs the frame closest to sizePx is used.
std::optional<Bitmap> decodeImage(const uint8_t* data, size_t size, int sizePx);

// "data:image/png;base64,...." -> bytes (base64 payloads only).
std::optional<std::vector<uint8_t>> decodeDataURL(const std::wstring& url);

class FaviconLoader {
public:
    using Callback = std::function<void(const Bitmap&)>;
    // Async; the callback runs on the UI thread (needs platform::initUiDispatcher).
    // Results (including failures) are cached per URL for the process lifetime.
    void load(const std::wstring& iconURL, int sizePx, Callback);

private:
    std::map<std::wstring, Bitmap> m_cache;
    std::map<std::wstring, std::vector<Callback>> m_pending;
};

} // namespace meridian::services
