#pragma once
// services/: bookmarks as a folder tree. Two fixed roots exist from the start:
// Favorites (shown on the start page) and Bookmarks (everything else).
#include "Database.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace meridian::services {

struct Bookmark {
    int64_t id { 0 };
    int64_t parent { 0 };
    bool folder { false };
    std::wstring title;
    std::wstring url;      // empty for folders
    int64_t added { 0 };   // Unix seconds
};

class Bookmarks {
public:
    static constexpr int64_t kFavoritesId = 1;
    static constexpr int64_t kBookmarksId = 2;

    explicit Bookmarks(Database&);

    // position -1 appends. Return 0 if the parent is not a folder.
    int64_t add(int64_t parent, const std::wstring& title, const std::wstring& url, int position = -1);
    int64_t addFolder(int64_t parent, const std::wstring& title, int position = -1);
    bool remove(int64_t id);                               // folders go with their contents; roots can't be removed
    bool move(int64_t id, int64_t newParent, int position); // refuses to move a folder into itself
    bool update(int64_t id, const std::wstring& title, const std::wstring& url);

    std::optional<Bookmark> get(int64_t id);
    std::vector<Bookmark> children(int64_t parent);
    std::optional<Bookmark> findByURL(const std::wstring& url);
    // Bookmarks (not folders) whose title or URL contains every word.
    std::vector<Bookmark> find(const std::vector<std::wstring>& words, size_t limit);

    // Netscape bookmark file (the format every browser imports and exports),
    // UTF-8. Import adds to what is there; a folder flagged as the source
    // browser's toolbar folder is merged into Favorites. Only http, https,
    // ftp and file links are imported. Returns the number of bookmarks added.
    std::string exportHTML();
    size_t importHTML(const std::string& utf8);

private:
    int64_t insert(int64_t parent, bool folder, const std::wstring& title, const std::wstring& url, int position, int64_t added);
    bool isFolder(int64_t id);
    int childCount(int64_t parent);
    void exportFolder(int64_t id, int depth, std::string& out);

    Database& m_db;
};

} // namespace meridian::services
