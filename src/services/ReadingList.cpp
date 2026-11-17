#include "ReadingList.h"

#include "History.h" // isRecordableURL

namespace meridian::services {

namespace {
constexpr size_t kMaxExcerpt = 400;
constexpr size_t kMaxTitle = 500;
const char kColumns[] = "id, url, title, excerpt, added, read_at";

ReadingItem readItem(Statement& s)
{
    return { s.integer(0), s.text(1), s.text(2), s.text(3), s.integer(4), s.integer(5) };
}
}

ReadingList::ReadingList(Database& db)
    : m_db(db)
{
    m_db.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS reading_list (
            id INTEGER PRIMARY KEY,
            url TEXT NOT NULL UNIQUE,
            title TEXT NOT NULL DEFAULT '',
            excerpt TEXT NOT NULL DEFAULT '',
            added INTEGER NOT NULL,
            read_at INTEGER NOT NULL DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS reading_list_added ON reading_list(added);
    )SQL");
}

int64_t ReadingList::add(const std::wstring& url, const std::wstring& title, const std::wstring& excerpt, int64_t now)
{
    if (!isRecordableURL(url))
        return 0;
    auto s = m_db.prepare(R"SQL(
        INSERT INTO reading_list (url, title, excerpt, added, read_at) VALUES (?1, ?2, ?3, ?4, 0)
        ON CONFLICT(url) DO UPDATE SET
            added = excluded.added,
            read_at = 0,
            title = CASE WHEN excluded.title <> '' THEN excluded.title ELSE title END,
            excerpt = CASE WHEN excluded.excerpt <> '' THEN excluded.excerpt ELSE excerpt END
    )SQL");
    s.bind(1, url).bind(2, title.substr(0, kMaxTitle)).bind(3, excerpt.substr(0, kMaxExcerpt)).bind(4, now);
    s.run();
    auto id = m_db.prepare("SELECT id FROM reading_list WHERE url = ?");
    id.bind(1, url);
    return id.step() ? id.integer(0) : 0;
}

bool ReadingList::remove(int64_t id)
{
    auto s = m_db.prepare("DELETE FROM reading_list WHERE id = ?");
    s.bind(1, id);
    s.run();
    auto changes = m_db.prepare("SELECT changes()");
    return changes.step() && changes.integer(0) > 0;
}

void ReadingList::setRead(int64_t id, bool read, int64_t now)
{
    auto s = m_db.prepare("UPDATE reading_list SET read_at = ? WHERE id = ?");
    s.bind(1, read ? (now > 0 ? now : 1) : 0).bind(2, id);
    s.run();
}

std::vector<ReadingItem> ReadingList::list(bool unreadOnly)
{
    std::string sql = std::string("SELECT ") + kColumns + " FROM reading_list"
        + (unreadOnly ? " WHERE read_at = 0" : "") + " ORDER BY added DESC, id DESC";
    auto s = m_db.prepare(sql.c_str());
    std::vector<ReadingItem> out;
    while (s.step())
        out.push_back(readItem(s));
    return out;
}

std::optional<ReadingItem> ReadingList::get(int64_t id)
{
    std::string sql = std::string("SELECT ") + kColumns + " FROM reading_list WHERE id = ?";
    auto s = m_db.prepare(sql.c_str());
    s.bind(1, id);
    if (!s.step())
        return std::nullopt;
    return readItem(s);
}

std::optional<ReadingItem> ReadingList::findByURL(const std::wstring& url)
{
    std::string sql = std::string("SELECT ") + kColumns + " FROM reading_list WHERE url = ?";
    auto s = m_db.prepare(sql.c_str());
    s.bind(1, url);
    if (!s.step())
        return std::nullopt;
    return readItem(s);
}

size_t ReadingList::clearRead()
{
    m_db.exec("DELETE FROM reading_list WHERE read_at <> 0");
    auto changes = m_db.prepare("SELECT changes()");
    return changes.step() ? size_t(changes.integer(0)) : 0;
}

size_t ReadingList::count(bool unreadOnly)
{
    auto s = m_db.prepare(unreadOnly ? "SELECT count(*) FROM reading_list WHERE read_at = 0" : "SELECT count(*) FROM reading_list");
    return s.step() ? size_t(s.integer(0)) : 0;
}

} // namespace meridian::services
