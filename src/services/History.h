#pragma once
// services/: browsing history. One row per URL (with visit and typed counts)
// plus one row per visit, so ranges of time can be cleared later.
//
// Callers must not record visits from private windows; this class has no
// notion of privacy mode and stores whatever it is given.
#include "Database.h"

#include <cstdint>
#include <string>
#include <vector>

namespace meridian::services {

struct HistoryEntry {
    int64_t id { 0 };
    std::wstring url;
    std::wstring title;
    int visitCount { 0 };
    int typedCount { 0 };
    int64_t lastVisit { 0 }; // Unix seconds (UTC)
};

int64_t unixNow();

// Only http(s) and file pages are kept; about:, data:, javascript: etc. are not.
bool isRecordableURL(const std::wstring& url);

// Ranking score: how often and how recently a page was visited. Typed visits
// count triple. Recency is bucketed (4 / 14 / 31 / 90 days) so that a page
// used daily last week still beats one opened once today.
double frecencyScore(int visitCount, int typedCount, int64_t ageSeconds);

class History {
public:
    enum class Order { Recent, MostVisited };

    explicit History(Database&);

    // Returns the URL's row id, or 0 if the URL is not recordable.
    int64_t recordVisit(const std::wstring& url, const std::wstring& title, bool typed, int64_t now = unixNow());
    void setTitle(const std::wstring& url, const std::wstring& title);

    // Entries whose URL or title contains every word (case-insensitive for
    // ASCII; SQLite's LIKE does not fold other scripts). No words = everything.
    std::vector<HistoryEntry> find(const std::vector<std::wstring>& words, Order, size_t limit);
    std::vector<HistoryEntry> search(const std::wstring& text, size_t limit);

    // Pages visited at least twice in the last 90 days, best first.
    std::vector<HistoryEntry> frequentlyVisited(size_t limit, int64_t now = unixNow());

    void remove(int64_t id);
    void removeVisitsSince(int64_t time); // e.g. "clear the last hour"
    void clear();
    size_t count();

private:
    Database& m_db;
};

} // namespace meridian::services
