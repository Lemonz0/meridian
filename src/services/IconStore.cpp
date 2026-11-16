#include "IconStore.h"

#include "History.h" // unixNow

namespace meridian::services {

namespace {
constexpr int kMaxSide = 64;
}

IconStore::IconStore(Database& db)
    : m_db(db)
{
    m_db.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS site_icons (
            host TEXT PRIMARY KEY,
            width INTEGER NOT NULL,
            height INTEGER NOT NULL,
            bgra BLOB NOT NULL,
            updated INTEGER NOT NULL
        )
    )SQL");
}

void IconStore::put(const std::wstring& host, const Bitmap& bmp)
{
    if (host.empty() || !bmp || bmp.width > kMaxSide || bmp.height > kMaxSide
        || bmp.bgra.size() != size_t(bmp.width) * size_t(bmp.height) * 4)
        return;
    auto cached = m_cache.find(host);
    if (cached != m_cache.end() && cached->second.width == bmp.width && cached->second.height == bmp.height
        && cached->second.bgra == bmp.bgra)
        return; // unchanged: no write
    auto s = m_db.prepare(R"SQL(
        INSERT INTO site_icons (host, width, height, bgra, updated) VALUES (?, ?, ?, ?, ?)
        ON CONFLICT(host) DO UPDATE SET width = excluded.width, height = excluded.height,
            bgra = excluded.bgra, updated = excluded.updated
    )SQL");
    s.bind(1, host).bind(2, int64_t(bmp.width)).bind(3, int64_t(bmp.height)).bind(4, bmp.bgra).bind(5, unixNow());
    s.run();
    m_cache[host] = bmp;
}

const Bitmap& IconStore::get(const std::wstring& host)
{
    auto it = m_cache.find(host);
    if (it != m_cache.end())
        return it->second;
    Bitmap bmp;
    if (!host.empty()) {
        auto s = m_db.prepare("SELECT width, height, bgra FROM site_icons WHERE host = ?");
        s.bind(1, host);
        if (s.step()) {
            bmp.width = int(s.integer(0));
            bmp.height = int(s.integer(1));
            bmp.bgra = s.blob(2);
            if (bmp.width <= 0 || bmp.height <= 0 || bmp.width > kMaxSide || bmp.height > kMaxSide
                || bmp.bgra.size() != size_t(bmp.width) * size_t(bmp.height) * 4)
                bmp = {};
        }
    }
    return m_cache.emplace(host, std::move(bmp)).first->second;
}

void IconStore::clear()
{
    m_db.exec("DELETE FROM site_icons");
    m_cache.clear();
}

} // namespace meridian::services
