#include "Favicons.h"

#include "platform/UiDispatcher.h"

#include <windows.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <shlwapi.h>
#include <thread>
#include <wincodec.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace meridian::services {

static constexpr size_t kMaxBytes = 512 * 1024;
static constexpr UINT kMaxDimension = 1024;

std::optional<Bitmap> decodeImage(const uint8_t* data, size_t size, int sizePx)
{
    if (!data || !size || size > kMaxBytes || sizePx <= 0)
        return std::nullopt;
    bool comInit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    std::optional<Bitmap> result;
    do {
        ComPtr<IWICImagingFactory> wic;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
            break;
        ComPtr<IStream> stream;
        stream.Attach(SHCreateMemStream(data, static_cast<UINT>(size)));
        if (!stream)
            break;
        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)))
            break;

        // Pick the frame closest to the requested size (prefer >= over <).
        UINT frames = 0;
        decoder->GetFrameCount(&frames);
        ComPtr<IWICBitmapFrameDecode> best;
        long bestScore = LONG_MAX;
        for (UINT i = 0; i < frames && i < 32; ++i) {
            ComPtr<IWICBitmapFrameDecode> frame;
            UINT w = 0, h = 0;
            if (FAILED(decoder->GetFrame(i, &frame)) || FAILED(frame->GetSize(&w, &h)))
                continue;
            if (!w || !h || w > kMaxDimension || h > kMaxDimension)
                continue;
            long d = long(std::max(w, h)) - sizePx;
            long score = d >= 0 ? d : 10000 - d;
            if (score < bestScore) {
                bestScore = score;
                best = frame;
            }
        }
        if (!best)
            break;

        UINT w = 0, h = 0;
        best->GetSize(&w, &h);
        double scale = double(sizePx) / std::max(w, h);
        UINT outW = std::max<UINT>(1, UINT(std::lround(w * scale)));
        UINT outH = std::max<UINT>(1, UINT(std::lround(h * scale)));

        ComPtr<IWICBitmapSource> source = best;
        ComPtr<IWICBitmapScaler> scaler;
        if ((outW != w || outH != h) && SUCCEEDED(wic->CreateBitmapScaler(&scaler))
            && SUCCEEDED(scaler->Initialize(best.Get(), outW, outH, WICBitmapInterpolationModeHighQualityCubic)))
            source = scaler;
        else {
            outW = w;
            outH = h;
        }
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(wic->CreateFormatConverter(&converter))
            || FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
            break;
        Bitmap bmp;
        bmp.width = int(outW);
        bmp.height = int(outH);
        bmp.bgra.resize(size_t(outW) * outH * 4);
        if (FAILED(converter->CopyPixels(nullptr, outW * 4, UINT(bmp.bgra.size()), bmp.bgra.data())))
            break;
        result = std::move(bmp);
    } while (false);
    if (comInit)
        CoUninitialize();
    return result;
}

std::optional<std::vector<uint8_t>> decodeDataURL(const std::wstring& url)
{
    if (url.size() < 5 || _wcsnicmp(url.c_str(), L"data:", 5) != 0)
        return std::nullopt;
    auto comma = url.find(L',');
    if (comma == std::wstring::npos)
        return std::nullopt;
    std::wstring meta = url.substr(5, comma - 5);
    if (meta.size() < 7 || _wcsicmp(meta.c_str() + meta.size() - 7, L";base64") != 0)
        return std::nullopt;
    std::wstring payload = url.substr(comma + 1);
    if (payload.size() > kMaxBytes * 4 / 3 + 4)
        return std::nullopt;
    DWORD n = 0;
    if (!CryptStringToBinaryW(payload.c_str(), DWORD(payload.size()), CRYPT_STRING_BASE64, nullptr, &n, nullptr, nullptr))
        return std::nullopt;
    std::vector<uint8_t> out(n);
    if (!CryptStringToBinaryW(payload.c_str(), DWORD(payload.size()), CRYPT_STRING_BASE64, out.data(), &n, nullptr, nullptr))
        return std::nullopt;
    out.resize(n);
    return out;
}

static std::optional<std::vector<uint8_t>> httpGet(const std::wstring& url)
{
    URL_COMPONENTS uc { sizeof(uc) };
    wchar_t host[256] {}, path[2048] {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = ARRAYSIZE(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = ARRAYSIZE(path);
    uc.dwExtraInfoLength = 1; // include query in path
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc))
        return std::nullopt;
    if (uc.nScheme != INTERNET_SCHEME_HTTPS && uc.nScheme != INTERNET_SCHEME_HTTP)
        return std::nullopt;
    std::wstring fullPath = std::wstring(path, uc.dwUrlPathLength) + (uc.lpszExtraInfo ? std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength) : L"");

    std::optional<std::vector<uint8_t>> result;
    HINTERNET session = WinHttpOpen(L"Meridian-Favicon", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    HINTERNET connect = nullptr, request = nullptr;
    do {
        if (!session)
            break;
        WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
        connect = WinHttpConnect(session, host, uc.nPort, 0);
        if (!connect)
            break;
        DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        request = WinHttpOpenRequest(connect, L"GET", fullPath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!request)
            break;
        DWORD noCookies = WINHTTP_DISABLE_COOKIES;
        WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &noCookies, sizeof(noCookies));
        if (!WinHttpSendRequest(request, L"Accept: image/*\r\n", DWORD(-1), nullptr, 0, 0, 0) || !WinHttpReceiveResponse(request, nullptr))
            break;
        DWORD status = 0, len = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &len, nullptr);
        if (status != 200)
            break;
        std::vector<uint8_t> body;
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(request, &avail) || !avail)
                break;
            if (body.size() + avail > kMaxBytes) {
                body.clear();
                break;
            }
            size_t old = body.size();
            body.resize(old + avail);
            DWORD read = 0;
            if (!WinHttpReadData(request, body.data() + old, avail, &read)) {
                body.clear();
                break;
            }
            body.resize(old + read);
        }
        if (!body.empty())
            result = std::move(body);
    } while (false);
    if (request)
        WinHttpCloseHandle(request);
    if (connect)
        WinHttpCloseHandle(connect);
    if (session)
        WinHttpCloseHandle(session);
    return result;
}

void FaviconLoader::load(const std::wstring& iconURL, int sizePx, Callback callback)
{
    std::wstring key = iconURL + L"#" + std::to_wstring(sizePx);
    if (auto it = m_cache.find(key); it != m_cache.end()) {
        callback(it->second);
        return;
    }
    auto& waiters = m_pending[key];
    waiters.push_back(std::move(callback));
    if (waiters.size() > 1)
        return; // already in flight

    std::thread([this, key, iconURL, sizePx] {
        std::optional<std::vector<uint8_t>> bytes = _wcsnicmp(iconURL.c_str(), L"data:", 5) == 0
            ? decodeDataURL(iconURL) : httpGet(iconURL);
        Bitmap bmp;
        if (bytes)
            if (auto decoded = decodeImage(bytes->data(), bytes->size(), sizePx))
                bmp = std::move(*decoded);
        platform::postToUi([this, key, bmp = std::move(bmp)] {
            m_cache[key] = bmp;
            auto waiters = std::move(m_pending[key]);
            m_pending.erase(key);
            for (auto& cb : waiters)
                cb(bmp);
        });
    }).detach();
}

} // namespace meridian::services
