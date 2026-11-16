#include "SearchSuggestions.h"

#include "Text.h"
#include "UrlFixup.h"
#include "platform/UiDispatcher.h"

#include <windows.h>

#include <thread>
#include <winhttp.h>

namespace meridian::services {

namespace {
constexpr size_t kMaxBytes = 64 * 1024;
constexpr size_t kMaxSuggestions = 4;
constexpr size_t kMaxSuggestionLength = 200;

void appendUtf8(std::string& out, unsigned cp)
{
    if (cp < 0x80)
        out.push_back(char(cp));
    else if (cp < 0x800) {
        out.push_back(char(0xC0 | (cp >> 6)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(char(0xE0 | (cp >> 12)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(char(0xF0 | (cp >> 18)));
        out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    }
}

int hexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool readHex4(const std::string& s, size_t at, unsigned& out)
{
    if (at + 4 > s.size())
        return false;
    out = 0;
    for (size_t k = 0; k < 4; ++k) {
        int v = hexValue(s[at + k]);
        if (v < 0)
            return false;
        out = out * 16 + unsigned(v);
    }
    return true;
}

// Reads the JSON string starting at s[i] == '"'. Leaves i after the closing quote.
bool readString(const std::string& s, size_t& i, std::string& out)
{
    out.clear();
    for (++i; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"') {
            ++i;
            return true;
        }
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (++i >= s.size())
            return false;
        switch (s[i]) {
        case 'n': out.push_back('\n'); break;
        case 't': out.push_back('\t'); break;
        case 'r': out.push_back('\r'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'u': {
            unsigned cp = 0;
            if (!readHex4(s, i + 1, cp))
                return false;
            i += 4;
            unsigned low = 0;
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 2 < s.size() && s[i + 1] == '\\' && s[i + 2] == 'u'
                && readHex4(s, i + 3, low) && low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                i += 6;
            }
            if (cp >= 0xD800 && cp <= 0xDFFF)
                cp = 0xFFFD; // unpaired surrogate
            appendUtf8(out, cp);
            break;
        }
        default: out.push_back(s[i]); // \" \\ \/
        }
    }
    return false;
}

std::string httpGetText(const std::wstring& url)
{
    URL_COMPONENTS uc { sizeof(uc) };
    wchar_t host[256] {}, path[2048] {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = ARRAYSIZE(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = ARRAYSIZE(path);
    uc.dwExtraInfoLength = 1; // include the query in the path
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS)
        return {}; // typed text never goes out over plain http
    std::wstring fullPath = std::wstring(path, uc.dwUrlPathLength) + (uc.lpszExtraInfo ? std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength) : L"");

    std::string body;
    HINTERNET session = WinHttpOpen(L"Meridian-Suggest", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    HINTERNET connect = nullptr, request = nullptr;
    do {
        if (!session)
            break;
        WinHttpSetTimeouts(session, 3000, 3000, 3000, 3000);
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
        connect = WinHttpConnect(session, host, uc.nPort, 0);
        if (!connect)
            break;
        request = WinHttpOpenRequest(connect, L"GET", fullPath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!request)
            break;
        DWORD noCookies = WINHTTP_DISABLE_COOKIES;
        WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &noCookies, sizeof(noCookies));
        if (!WinHttpSendRequest(request, L"Accept: application/json\r\n", DWORD(-1), nullptr, 0, 0, 0) || !WinHttpReceiveResponse(request, nullptr))
            break;
        DWORD status = 0, len = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &len, nullptr);
        if (status != 200)
            break;
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
    } while (false);
    if (request)
        WinHttpCloseHandle(request);
    if (connect)
        WinHttpCloseHandle(connect);
    if (session)
        WinHttpCloseHandle(session);
    return body;
}
}

std::vector<std::wstring> parseSearchSuggestions(const std::string& json, size_t limit)
{
    std::vector<std::wstring> out;
    int arrays = 0, objects = 0;
    std::string lastKey, text;
    for (size_t i = 0; i < json.size() && out.size() < limit;) {
        char c = json[i];
        if (c == '[') { ++arrays; ++i; }
        else if (c == ']') { --arrays; ++i; }
        else if (c == '{') { ++objects; ++i; }
        else if (c == '}') { --objects; lastKey.clear(); ++i; }
        else if (c == '"') {
            if (!readString(json, i, text))
                return {};
            size_t next = json.find_first_not_of(" \t\r\n", i);
            if (next != std::string::npos && json[next] == ':') {
                lastKey = text;
                i = next + 1;
                continue;
            }
            bool openSearchItem = arrays == 2 && objects == 0;
            bool phraseItem = arrays == 1 && objects == 1 && lastKey == "phrase";
            if ((openSearchItem || phraseItem) && !text.empty() && text.size() <= kMaxSuggestionLength)
                out.push_back(fromUtf8(text));
            lastKey.clear();
        } else
            ++i;
        if (arrays < 0 || objects < 0)
            return {};
    }
    return out;
}

void SearchSuggestions::fetch(const std::wstring& urlPrefix, const std::wstring& query, Callback callback)
{
    uint64_t generation = ++*m_generation;
    if (urlPrefix.empty() || query.empty())
        return;
    std::wstring url = urlPrefix + percentEncodeQuery(query);
    std::thread([url, query, generation, current = m_generation, callback = std::move(callback)]() mutable {
        auto results = parseSearchSuggestions(httpGetText(url), kMaxSuggestions);
        platform::postToUi([query, generation, current, callback = std::move(callback), results = std::move(results)]() mutable {
            if (*current == generation) // read on the UI thread only
                callback(query, std::move(results));
        });
    }).detach();
}

} // namespace meridian::services
