#include "UrlFixup.h"

#include <algorithm>
#include <cwctype>
#include <windows.h>

namespace meridian::services {

static std::wstring trim(const std::wstring& s)
{
    size_t b = 0, e = s.size();
    while (b < e && iswspace(s[b])) ++b;
    while (e > b && iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

static bool startsWithI(const std::wstring& s, const wchar_t* prefix)
{
    size_t n = wcslen(prefix);
    return s.size() >= n && _wcsnicmp(s.c_str(), prefix, n) == 0;
}

static bool hasScheme(const std::wstring& s)
{
    // scheme ":" per RFC 3986, but treat "localhost:8080" style as host:port.
    auto colon = s.find(L':');
    if (colon == std::wstring::npos || colon == 0)
        return false;
    for (size_t i = 0; i < colon; ++i) {
        wchar_t c = s[i];
        if (!(iswalnum(c) || c == L'+' || c == L'-' || c == L'.'))
            return false;
    }
    if (startsWithI(s, L"about:") || startsWithI(s, L"data:") || startsWithI(s, L"file:")
        || startsWithI(s, L"view-source:") || startsWithI(s, L"javascript:"))
        return true;
    // "host:port" → digits after colon means not a scheme.
    size_t i = colon + 1, digitCount = 0;
    bool allDigits = true;
    for (; i < s.size() && s[i] != L'/'; ++i, ++digitCount)
        allDigits = allDigits && iswdigit(s[i]);
    if (digitCount > 0 && allDigits)
        return false;
    return s.compare(colon, 3, L"://") == 0;
}

static bool looksLikeHost(const std::wstring& s)
{
    if (s.find(L' ') != std::wstring::npos)
        return false;
    std::wstring host = s.substr(0, s.find_first_of(L"/?#"));
    auto colon = host.find(L':');
    if (colon != std::wstring::npos)
        host = host.substr(0, colon);
    if (host.empty())
        return false;
    if (_wcsicmp(host.c_str(), L"localhost") == 0)
        return true;
    if (host.front() == L'[')
        return true; // IPv6 literal
    auto dot = host.rfind(L'.');
    if (dot == std::wstring::npos || dot == 0 || dot == host.size() - 1)
        return false;
    // TLD must be letters (or the whole thing an IPv4 address).
    bool tldAlpha = std::all_of(host.begin() + dot + 1, host.end(), [](wchar_t c) { return iswalpha(c); });
    bool ipv4 = std::all_of(host.begin(), host.end(), [](wchar_t c) { return iswdigit(c) || c == L'.'; });
    return tldAlpha || ipv4;
}

std::wstring percentEncodeQuery(const std::wstring& text)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), len, nullptr, nullptr);
    std::wstring out;
    const wchar_t* hex = L"0123456789ABCDEF";
    for (unsigned char c : utf8) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out.push_back(c);
        else if (c == ' ')
            out.push_back(L'+');
        else {
            out.push_back(L'%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

FixupResult fixupInput(const std::wstring& raw, const std::wstring& searchPrefix)
{
    std::wstring s = trim(raw);
    if (s.empty())
        return {};
    if (hasScheme(s))
        return { s, false };
    if (looksLikeHost(s)) {
        bool local = startsWithI(s, L"localhost") || startsWithI(s, L"127.") || startsWithI(s, L"[::1]");
        return { (local ? L"http://" : L"https://") + s, false };
    }
    return { searchPrefix + percentEncodeQuery(s), true };
}

std::wstring displayHost(const std::wstring& url)
{
    auto sep = url.find(L"://");
    if (sep == std::wstring::npos)
        return url;
    std::wstring rest = url.substr(sep + 3);
    std::wstring host = rest.substr(0, rest.find_first_of(L"/?#"));
    auto at = host.rfind(L'@');
    if (at != std::wstring::npos)
        host = host.substr(at + 1);
    if (startsWithI(host, L"www."))
        host = host.substr(4);
    return host.empty() ? url : host;
}

bool isSecureURL(const std::wstring& url)
{
    return startsWithI(url, L"https://");
}

} // namespace meridian::services
