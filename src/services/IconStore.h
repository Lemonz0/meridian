#pragma once
// services/: site icons kept per host, so suggestions, history and the start
// page can show an icon for a page that is not open. Icons are stored already
// decoded (small BGRA bitmaps), never as the bytes a site sent.
#include "Database.h"
#include "Favicons.h"

#include <map>
#include <string>

namespace meridian::services {

class IconStore {
public:
    explicit IconStore(Database&);
    void put(const std::wstring& host, const Bitmap&);
    // Empty bitmap if there is none. Lookups are cached in memory.
    const Bitmap& get(const std::wstring& host);
    void clear();
private:
    Database& m_db;
    std::map<std::wstring, Bitmap> m_cache;
};

} // namespace meridian::services
