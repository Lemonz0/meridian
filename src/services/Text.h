#pragma once
// services/: small Unicode text helpers shared by history, bookmarks and
// suggestions.
#include <string>
#include <vector>

namespace meridian::services {

std::wstring toLower(std::wstring);                    // full Unicode (CharLowerBuffW)
std::vector<std::wstring> splitWords(const std::wstring&); // lowercased, whitespace-separated
std::wstring likePattern(const std::wstring& word);    // "%word%" with \ % _ escaped (ESCAPE '\')
std::string toUtf8(const std::wstring&);
std::wstring fromUtf8(const std::string&);

// "https://www.Example.com/a" -> "example.com/a": lowercase, scheme and a
// leading "www." removed. What the user is matched against when typing a URL.
std::wstring strippedURL(const std::wstring& url);
// "https://www.example.com/a?b" -> "https://www.example.com" ("" if no host).
std::wstring originOf(const std::wstring& url);

} // namespace meridian::services
