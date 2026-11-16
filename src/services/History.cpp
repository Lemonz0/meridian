#include "History.h"

#include "Text.h"

#include <algorithm>
#include <ctime>

namespace meridian::services {

namespace {
constexpr int64_t kDay = 24 * 60 * 60;
constexpr size_t kMaxWords = 8;
const char kColumns[] = "id, url, title, visit_count, typed_count, last_visit";

HistoryEntry readEntry(Statement& s)
{
    HistoryEntry e;
    e.id = s.integer(0);
    e.url = s.text(1);
    e.title = s.text(2);
    e.visitCount = int(s.integer(3));
    e.typedCount = int(s.integer(4));
    e.lastVisit = s.integer(5);
    return e;
}
}

int64_t unixNow()
{
    return static_cast<int64_t>(std::time(nullptr));
}

bool isRecordableURL(const std::wstring& url)
{
    std::wstring lower = toLower(url.substr(0, 8));
    return lower.compare(0, 7, L"http://") == 0 || lower.compare(0, 8, L"https://") == 0
        || lower.compare(0, 8, L"file:///") == 0;
}

double frecencyScore(int visitCount, int typedCount, int64_t ageSeconds)
{
    if (visitCount <= 0)
        return 0;
    double weight = ageSeconds <= 4 * kDay ? 100 : ageSeconds <= 14 * kDay ? 70 : ageSeconds <= 31 * kDay ? 50
        : ageSeconds <= 90 * kDay ? 30 : 10;
    int typed = std::clamp(typedCount, 0, visitCount);
    return (visitCount + 2.0 * typed) * weight;
}

History::History(Database& db)
    : m_db(db)
{
    m_db.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS history_urls (
            id INTEGER PRIMARY KEY,
            url TEXT NOT NULL UNIQUE,
            title TEXT NOT NULL DEFAULT '',
            visit_count INTEGER NOT NULL DEFAULT 0,
            typed_count INTEGER NOT NULL DEFAULT 0,
            last_visit INTEGER NOT NULL DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS history_visits (
            id INTEGER PRIMARY KEY,
            url_id INTEGER NOT NULL REFERENCES history_urls(id) ON DELETE CASCADE,
            visit_time INTEGER NOT NULL,
            typed INTEGER NOT NULL DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS history_visits_url ON history_visits(url_id);
        CREATE INDEX IF NOT EXISTS history_visits_time ON history_visits(visit_time);
        CREATE INDEX IF NOT EXISTS history_urls_last ON history_urls(last_visit);
    )SQL");
}

int64_t History::recordVisit(const std::wstring& url, const std::wstring& title, bool typed, int64_t now)
{
    if (!isRecordableURL(url))
        return 0;
    Database::Transaction tx(m_db);
    auto upsert = m_db.prepare(R"SQL(
        INSERT INTO history_urls (url, title, visit_count, typed_count, last_visit) VALUES (?1, ?2, 1, ?3, ?4)
        ON CONFLICT(url) DO UPDATE SET
            visit_count = visit_count + 1,
            typed_count = typed_count + excluded.typed_count,
            last_visit = max(last_visit, excluded.last_visit),
            title = CASE WHEN excluded.title <> '' THEN excluded.title ELSE title END
    )SQL");
    upsert.bind(1, url).bind(2, title).bind(3, int64_t(typed)).bind(4, now);
    upsert.run();
    auto lookup = m_db.prepare("SELECT id FROM history_urls WHERE url = ?");
    lookup.bind(1, url);
    int64_t id = lookup.step() ? lookup.integer(0) : 0;
    lookup.reset();
    auto visit = m_db.prepare("INSERT INTO history_visits (url_id, visit_time, typed) VALUES (?, ?, ?)");
    visit.bind(1, id).bind(2, now).bind(3, int64_t(typed));
    visit.run();
    tx.commit();
    return id;
}

void History::setTitle(const std::wstring& url, const std::wstring& title)
{
    if (title.empty())
        return;
    auto s = m_db.prepare("UPDATE history_urls SET title = ? WHERE url = ?");
    s.bind(1, title).bind(2, url);
    s.run();
}

std::vector<HistoryEntry> History::find(const std::vector<std::wstring>& words, Order order, size_t limit)
{
    size_t n = std::min(words.size(), kMaxWords);
    std::string sql = std::string("SELECT ") + kColumns + " FROM history_urls";
    for (size_t i = 0; i < n; ++i) {
        std::string p = "?" + std::to_string(i + 1);
        sql += (i ? " AND " : " WHERE ");
        sql += "(url LIKE " + p + " ESCAPE '\\' OR title LIKE " + p + " ESCAPE '\\')";
    }
    sql += order == Order::Recent ? " ORDER BY last_visit DESC, id DESC" : " ORDER BY visit_count DESC, last_visit DESC";
    sql += " LIMIT " + std::to_string(limit);
    auto s = m_db.prepare(sql.c_str());
    for (size_t i = 0; i < n; ++i)
        s.bind(int(i + 1), likePattern(words[i]));
    std::vector<HistoryEntry> out;
    while (s.step())
        out.push_back(readEntry(s));
    return out;
}

std::vector<HistoryEntry> History::search(const std::wstring& text, size_t limit)
{
    return find(splitWords(text), Order::Recent, limit);
}

std::vector<HistoryEntry> History::frequentlyVisited(size_t limit, int64_t now)
{
    std::string sql = std::string("SELECT ") + kColumns
        + " FROM history_urls WHERE visit_count >= 2 AND last_visit >= ? ORDER BY visit_count DESC LIMIT 200";
    auto s = m_db.prepare(sql.c_str());
    s.bind(1, now - 90 * kDay);
    std::vector<HistoryEntry> out;
    while (s.step())
        out.push_back(readEntry(s));
    auto score = [now](const HistoryEntry& e) { return frecencyScore(e.visitCount, e.typedCount, now - e.lastVisit); };
    std::stable_sort(out.begin(), out.end(), [&](auto& a, auto& b) { return score(a) > score(b); });
    if (out.size() > limit)
        out.resize(limit);
    return out;
}

void History::remove(int64_t id)
{
    auto s = m_db.prepare("DELETE FROM history_urls WHERE id = ?"); // visits cascade
    s.bind(1, id);
    s.run();
}

void History::removeVisitsSince(int64_t time)
{
    Database::Transaction tx(m_db);
    auto del = m_db.prepare("DELETE FROM history_visits WHERE visit_time >= ?");
    del.bind(1, time);
    del.run();
    m_db.exec(R"SQL(
        UPDATE history_urls SET
            visit_count = (SELECT count(*) FROM history_visits WHERE url_id = history_urls.id),
            typed_count = (SELECT coalesce(sum(typed), 0) FROM history_visits WHERE url_id = history_urls.id),
            last_visit = (SELECT coalesce(max(visit_time), 0) FROM history_visits WHERE url_id = history_urls.id);
        DELETE FROM history_urls WHERE visit_count = 0;
    )SQL");
    tx.commit();
}

void History::clear()
{
    m_db.exec("DELETE FROM history_urls"); // visits cascade
}

size_t History::count()
{
    auto s = m_db.prepare("SELECT count(*) FROM history_urls");
    return s.step() ? size_t(s.integer(0)) : 0;
}

} // namespace meridian::services
