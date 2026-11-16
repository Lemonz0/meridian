#pragma once
// services/: key/value preferences in the profile database. The Settings
// window (M7) will sit on top of this.
#include "Database.h"

#include <string>

namespace meridian::services {

namespace settings {
// Off by default: sending each keystroke to the search engine is a network
// request the user has to opt in to.
inline constexpr wchar_t kSearchSuggestions[] = L"search.suggestions";
}

class Settings {
public:
    explicit Settings(Database&);
    std::wstring get(const std::wstring& key, const std::wstring& fallback = {});
    bool getBool(const std::wstring& key, bool fallback = false);
    void set(const std::wstring& key, const std::wstring& value);
    void setBool(const std::wstring& key, bool value) { set(key, value ? L"1" : L"0"); }
private:
    Database& m_db;
};

} // namespace meridian::services
