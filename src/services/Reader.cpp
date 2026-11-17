#include "Reader.h"

#include <algorithm>
#include <cstdio>

namespace meridian::services {

namespace {
const wchar_t kThemeKey[] = L"reader.theme";
const wchar_t kSerifKey[] = L"reader.serif";
const wchar_t kSizeKey[] = L"reader.size";

const wchar_t* themeName(ReaderTheme t)
{
    switch (t) {
    case ReaderTheme::Light: return L"light";
    case ReaderTheme::Sepia: return L"sepia";
    case ReaderTheme::Dark: return L"dark";
    default: return L"auto";
    }
}

ReaderTheme themeFromName(const std::wstring& s)
{
    if (s == L"light") return ReaderTheme::Light;
    if (s == L"sepia") return ReaderTheme::Sepia;
    if (s == L"dark") return ReaderTheme::Dark;
    return ReaderTheme::Automatic;
}
}

ReaderStyle ReaderStyle::larger() const
{
    ReaderStyle s = *this;
    s.sizePercent = std::min(kMaxSize, sizePercent + kStep);
    return s;
}

ReaderStyle ReaderStyle::smaller() const
{
    ReaderStyle s = *this;
    s.sizePercent = std::max(kMinSize, sizePercent - kStep);
    return s;
}

std::wstring ReaderStyle::toJson() const
{
    return L"{\"theme\":\"" + std::wstring(themeName(theme)) + L"\",\"font\":\"" + (serif ? L"serif" : L"sans")
        + L"\",\"size\":" + std::to_wstring(std::clamp(sizePercent, kMinSize, kMaxSize)) + L"}";
}

ReaderStyle loadReaderStyle(Settings& settings)
{
    ReaderStyle s;
    s.theme = themeFromName(settings.get(kThemeKey, L"auto"));
    s.serif = settings.getBool(kSerifKey, false);
    s.sizePercent = std::clamp(_wtoi(settings.get(kSizeKey, L"100").c_str()), ReaderStyle::kMinSize, ReaderStyle::kMaxSize);
    return s;
}

void saveReaderStyle(Settings& settings, const ReaderStyle& s)
{
    settings.set(kThemeKey, themeName(s.theme));
    settings.setBool(kSerifKey, s.serif);
    settings.set(kSizeKey, std::to_wstring(std::clamp(s.sizePercent, ReaderStyle::kMinSize, ReaderStyle::kMaxSize)));
}

ReaderResult parseReaderResult(const std::wstring& raw)
{
    ReaderResult r;
    std::wstring fields[6];
    size_t n = 0, start = 0;
    for (size_t i = 0; i <= raw.size() && n < 6; ++i) {
        if (i == raw.size() || raw[i] == L'\x1f') {
            fields[n++] = raw.substr(start, i - start);
            start = i + 1;
        }
    }
    if (n < 6)
        return r; // not a reader.js answer
    r.available = fields[0] == L"1";
    r.title = fields[1];
    r.excerpt = fields[2];
    r.byline = fields[3];
    r.siteName = fields[4];
    r.shown = fields[5] == L"1";
    return r;
}

std::wstring jsStringLiteral(const std::wstring& s)
{
    std::wstring out = L"\"";
    for (wchar_t c : s) {
        switch (c) {
        case L'"': out += L"\\\""; break;
        case L'\\': out += L"\\\\"; break;
        case L'\n': out += L"\\n"; break;
        case L'\r': out += L"\\r"; break;
        case L'\t': out += L"\\t"; break;
        default:
            if (c < 0x20 || c == 0x2028 || c == 0x2029 || c == L'<') {
                // Control characters, JS line terminators, and '<' (no "</script>" surprises).
                wchar_t buf[8];
                swprintf(buf, 8, L"\\u%04x", unsigned(c));
                out += buf;
            } else
                out.push_back(c);
        }
    }
    return out + L"\"";
}

} // namespace meridian::services
