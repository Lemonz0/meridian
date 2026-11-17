#pragma once
// services/: the Reading List: pages saved to read later, newest first.
//
// Items keep the URL, title and a short excerpt. The page itself is not saved
// for offline reading (see docs/FEATURE_STATUS.md).
#include "Database.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace meridian::services {

struct ReadingItem {
    int64_t id { 0 };
    std::wstring url;
    std::wstring title;
    std::wstring excerpt;
    int64_t added { 0 };  // Unix seconds
    int64_t readAt { 0 }; // 0 = unread
    bool unread() const { return readAt == 0; }
};

class ReadingList {
public:
    explicit ReadingList(Database&);

    // Adding a URL that is already in the list moves it to the top and marks
    // it unread again; a non-empty title/excerpt replaces the stored one.
    // Returns 0 for URLs that can't be saved (only http/https/file).
    int64_t add(const std::wstring& url, const std::wstring& title, const std::wstring& excerpt, int64_t now);
    bool remove(int64_t id);
    void setRead(int64_t id, bool read, int64_t now);
    std::vector<ReadingItem> list(bool unreadOnly = false);
    std::optional<ReadingItem> get(int64_t id);
    std::optional<ReadingItem> findByURL(const std::wstring& url);
    size_t clearRead();
    size_t count(bool unreadOnly = false);

private:
    Database& m_db;
};

} // namespace meridian::services
