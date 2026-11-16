#pragma once
// services/: turns what the user typed in the Smart Search field into a URL.
#include <string>

namespace meridian::services {

struct FixupResult {
    std::wstring url;
    bool isSearch { false };
};

// HTTPS-first: bare hosts get https://. Text that does not look like a host
// becomes a search. Explicit schemes are kept as typed (http:// included).
FixupResult fixupInput(const std::wstring& input, const std::wstring& searchPrefix);

// "https://www.example.com/a/b?c" -> "example.com" (Safari's idle display).
std::wstring displayHost(const std::wstring& url);

bool isSecureURL(const std::wstring& url);

std::wstring percentEncodeQuery(const std::wstring& text);

} // namespace meridian::services
