#pragma once
// ui/: small text helpers shared by the BrowserWindow source files.
#include <cwctype>
#include <string>
#include <windows.h>

namespace meridian::ui {

inline std::wstring windowText(HWND h)
{
    int len = GetWindowTextLengthW(h);
    std::wstring text(size_t(len > 0 ? len : 0), L'\0');
    if (len > 0)
        GetWindowTextW(h, text.data(), len + 1);
    return text;
}

inline std::wstring trimmed(const std::wstring& s)
{
    size_t b = 0, e = s.size();
    while (b < e && iswspace(s[b])) ++b;
    while (e > b && iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

} // namespace meridian::ui
