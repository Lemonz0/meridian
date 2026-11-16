#include "Text.h"

#include <cwctype>
#include <windows.h>

namespace meridian::services {

std::wstring toLower(std::wstring s)
{
    if (!s.empty())
        CharLowerBuffW(s.data(), static_cast<DWORD>(s.size()));
    return s;
}

std::vector<std::wstring> splitWords(const std::wstring& text)
{
    std::vector<std::wstring> words;
    std::wstring lower = toLower(text);
    size_t i = 0;
    while (i < lower.size()) {
        while (i < lower.size() && iswspace(lower[i]))
            ++i;
        size_t start = i;
        while (i < lower.size() && !iswspace(lower[i]))
            ++i;
        if (i > start)
            words.push_back(lower.substr(start, i - start));
    }
    return words;
}

std::wstring likePattern(const std::wstring& word)
{
    std::wstring out = L"%";
    for (wchar_t c : word) {
        if (c == L'\\' || c == L'%' || c == L'_')
            out.push_back(L'\\');
        out.push_back(c);
    }
    out.push_back(L'%');
    return out;
}

std::string toUtf8(const std::wstring& s)
{
    if (s.empty())
        return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring fromUtf8(const std::string& s)
{
    if (s.empty())
        return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

std::wstring strippedURL(const std::wstring& url)
{
    std::wstring s = toLower(url);
    auto sep = s.find(L"://");
    if (sep != std::wstring::npos && s.find_first_of(L"/?#") > sep)
        s = s.substr(sep + 3);
    if (s.compare(0, 4, L"www.") == 0)
        s = s.substr(4);
    return s;
}

std::wstring originOf(const std::wstring& url)
{
    auto sep = url.find(L"://");
    if (sep == std::wstring::npos)
        return {};
    auto end = url.find_first_of(L"/?#", sep + 3);
    std::wstring origin = url.substr(0, end);
    return origin.size() > sep + 3 ? origin : std::wstring();
}

} // namespace meridian::services
