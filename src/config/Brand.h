#pragma once
// Single source of truth for product naming. Change here only.
namespace meridian::brand {
inline constexpr wchar_t kProductName[] = L"Meridian";
inline constexpr wchar_t kWindowClass[] = L"MeridianBrowserWindow";
inline constexpr wchar_t kDefaultHomeURL[] = L"https://webkit.org/";
inline constexpr wchar_t kSearchURLPrefix[] = L"https://duckduckgo.com/?q=";
// Search-engine suggestions endpoint; only contacted when the user turns on
// search suggestions (off by default), never from private windows.
inline constexpr wchar_t kSuggestURLPrefix[] = L"https://duckduckgo.com/ac/?q=";
inline constexpr wchar_t kVersion[] = L"0.4.0-m4";
} // namespace meridian::brand
