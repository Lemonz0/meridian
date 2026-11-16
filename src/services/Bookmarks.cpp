#include "Bookmarks.h"

#include "History.h" // unixNow
#include "Text.h"

#include <algorithm>
#include <cstdlib>

namespace meridian::services {

namespace {
constexpr size_t kMaxWords = 8;
constexpr int kMaxImportDepth = 32;
const char kColumns[] = "id, coalesce(parent_id, 0), is_folder, title, coalesce(url, ''), added";

Bookmark readBookmark(Statement& s)
{
    Bookmark b;
    b.id = s.integer(0);
    b.parent = s.integer(1);
    b.folder = s.integer(2) != 0;
    b.title = s.text(3);
    b.url = s.text(4);
    b.added = s.integer(5);
    return b;
}

std::string escapeHTML(const std::wstring& text)
{
    std::string out;
    for (char c : toUtf8(text)) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out.push_back(c);
        }
    }
    return out;
}

void appendCodePoint(std::string& out, unsigned long cp)
{
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        return;
    if (cp < 0x80)
        out.push_back(char(cp));
    else if (cp < 0x800) {
        out.push_back(char(0xC0 | (cp >> 6)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(char(0xE0 | (cp >> 12)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(char(0xF0 | (cp >> 18)));
        out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    }
}

std::string unescapeHTML(const std::string& in)
{
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '&') {
            out.push_back(in[i]);
            continue;
        }
        size_t semi = in.find(';', i);
        if (semi == std::string::npos || semi - i > 10) {
            out.push_back('&');
            continue;
        }
        std::string name = in.substr(i + 1, semi - i - 1);
        if (name == "amp") out.push_back('&');
        else if (name == "lt") out.push_back('<');
        else if (name == "gt") out.push_back('>');
        else if (name == "quot") out.push_back('"');
        else if (name == "apos") out.push_back('\'');
        else if (name.size() > 1 && name[0] == '#') {
            bool hex = name[1] == 'x' || name[1] == 'X';
            appendCodePoint(out, std::strtoul(name.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10));
        } else {
            out.push_back('&'); // unknown entity: keep the text as written
            continue;
        }
        i = semi;
    }
    return out;
}

char lowerAscii(char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; }

// Value of `name` inside a tag's attribute text (case-insensitive name).
std::string attribute(const std::string& tag, const char* name)
{
    std::string lower;
    for (char c : tag)
        lower.push_back(lowerAscii(c));
    std::string key = name;
    size_t pos = 0;
    while ((pos = lower.find(key, pos)) != std::string::npos) {
        bool boundary = pos == 0 || lower[pos - 1] == ' ' || lower[pos - 1] == '\t' || lower[pos - 1] == '\n' || lower[pos - 1] == '\r';
        size_t eq = pos + key.size();
        if (!boundary || eq >= tag.size() || tag[eq] != '=') {
            pos += key.size();
            continue;
        }
        size_t start = eq + 1;
        if (start < tag.size() && (tag[start] == '"' || tag[start] == '\'')) {
            size_t end = tag.find(tag[start], start + 1);
            return unescapeHTML(tag.substr(start + 1, end == std::string::npos ? std::string::npos : end - start - 1));
        }
        size_t end = tag.find_first_of(" \t\r\n", start);
        return unescapeHTML(tag.substr(start, end == std::string::npos ? std::string::npos : end - start));
    }
    return {};
}

bool importableURL(const std::wstring& url)
{
    std::wstring l = toLower(url.substr(0, 8));
    return l.compare(0, 7, L"http://") == 0 || l.compare(0, 8, L"https://") == 0 || l.compare(0, 6, L"ftp://") == 0
        || l.compare(0, 8, L"file:///") == 0;
}

std::string trimmed(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}
}

Bookmarks::Bookmarks(Database& db)
    : m_db(db)
{
    m_db.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS bookmarks (
            id INTEGER PRIMARY KEY,
            parent_id INTEGER REFERENCES bookmarks(id) ON DELETE CASCADE,
            position INTEGER NOT NULL,
            is_folder INTEGER NOT NULL DEFAULT 0,
            title TEXT NOT NULL DEFAULT '',
            url TEXT,
            added INTEGER NOT NULL DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS bookmarks_parent ON bookmarks(parent_id, position);
        CREATE INDEX IF NOT EXISTS bookmarks_url ON bookmarks(url);
        INSERT OR IGNORE INTO bookmarks (id, parent_id, position, is_folder, title) VALUES (1, NULL, 0, 1, 'Favorites');
        INSERT OR IGNORE INTO bookmarks (id, parent_id, position, is_folder, title) VALUES (2, NULL, 1, 1, 'Bookmarks');
    )SQL");
}

bool Bookmarks::isFolder(int64_t id)
{
    auto s = m_db.prepare("SELECT is_folder FROM bookmarks WHERE id = ?");
    s.bind(1, id);
    return s.step() && s.integer(0) != 0;
}

int Bookmarks::childCount(int64_t parent)
{
    auto s = m_db.prepare("SELECT count(*) FROM bookmarks WHERE parent_id = ?");
    s.bind(1, parent);
    return s.step() ? int(s.integer(0)) : 0;
}

// No transaction of its own: callers wrap it.
int64_t Bookmarks::insert(int64_t parent, bool folder, const std::wstring& title, const std::wstring& url, int position, int64_t added)
{
    if (!isFolder(parent))
        return 0;
    int count = childCount(parent);
    if (position < 0 || position > count)
        position = count;
    auto shift = m_db.prepare("UPDATE bookmarks SET position = position + 1 WHERE parent_id = ? AND position >= ?");
    shift.bind(1, parent).bind(2, int64_t(position));
    shift.run();
    auto ins = m_db.prepare(folder
        ? "INSERT INTO bookmarks (parent_id, position, is_folder, title, url, added) VALUES (?1, ?2, 1, ?3, NULL, ?5)"
        : "INSERT INTO bookmarks (parent_id, position, is_folder, title, url, added) VALUES (?1, ?2, 0, ?3, ?4, ?5)");
    ins.bind(1, parent).bind(2, int64_t(position)).bind(3, title);
    if (!folder)
        ins.bind(4, url);
    ins.bind(5, added);
    ins.run();
    auto id = m_db.prepare("SELECT last_insert_rowid()");
    return id.step() ? id.integer(0) : 0;
}

int64_t Bookmarks::add(int64_t parent, const std::wstring& title, const std::wstring& url, int position)
{
    if (url.empty())
        return 0;
    Database::Transaction tx(m_db);
    int64_t id = insert(parent, false, title, url, position, unixNow());
    tx.commit();
    return id;
}

int64_t Bookmarks::addFolder(int64_t parent, const std::wstring& title, int position)
{
    Database::Transaction tx(m_db);
    int64_t id = insert(parent, true, title, {}, position, unixNow());
    tx.commit();
    return id;
}

bool Bookmarks::remove(int64_t id)
{
    auto b = get(id);
    if (!b || b->parent == 0)
        return false;
    Database::Transaction tx(m_db);
    auto pos = m_db.prepare("SELECT position FROM bookmarks WHERE id = ?");
    pos.bind(1, id);
    int64_t position = pos.step() ? pos.integer(0) : 0;
    pos.reset();
    auto del = m_db.prepare("DELETE FROM bookmarks WHERE id = ?"); // children cascade
    del.bind(1, id);
    del.run();
    auto shift = m_db.prepare("UPDATE bookmarks SET position = position - 1 WHERE parent_id = ? AND position > ?");
    shift.bind(1, b->parent).bind(2, position);
    shift.run();
    tx.commit();
    return true;
}

bool Bookmarks::move(int64_t id, int64_t newParent, int position)
{
    auto b = get(id);
    if (!b || b->parent == 0 || !isFolder(newParent))
        return false;
    // A folder can't go inside itself or one of its descendants.
    for (int64_t p = newParent, guard = 0; p != 0 && guard < 1000; ++guard) {
        if (p == id)
            return false;
        auto parent = get(p);
        p = parent ? parent->parent : 0;
    }
    Database::Transaction tx(m_db);
    auto pos = m_db.prepare("SELECT position FROM bookmarks WHERE id = ?");
    pos.bind(1, id);
    int64_t oldPosition = pos.step() ? pos.integer(0) : 0;
    pos.reset();
    // Take it out of its old place (parked at -1 so the shifts skip it)...
    auto park = m_db.prepare("UPDATE bookmarks SET position = -1 WHERE id = ?");
    park.bind(1, id);
    park.run();
    auto close = m_db.prepare("UPDATE bookmarks SET position = position - 1 WHERE parent_id = ? AND position > ?");
    close.bind(1, b->parent).bind(2, oldPosition);
    close.run();
    // ...and open a gap in the new one. `position` is the index it ends up at.
    int count = childCount(newParent) - (newParent == b->parent ? 1 : 0);
    if (position < 0 || position > count)
        position = count;
    auto open = m_db.prepare("UPDATE bookmarks SET position = position + 1 WHERE parent_id = ? AND position >= ?");
    open.bind(1, newParent).bind(2, int64_t(position));
    open.run();
    auto place = m_db.prepare("UPDATE bookmarks SET parent_id = ?, position = ? WHERE id = ?");
    place.bind(1, newParent).bind(2, int64_t(position)).bind(3, id);
    place.run();
    tx.commit();
    return true;
}

bool Bookmarks::update(int64_t id, const std::wstring& title, const std::wstring& url)
{
    auto b = get(id);
    if (!b || (!b->folder && url.empty()))
        return false;
    if (b->folder) {
        auto s = m_db.prepare("UPDATE bookmarks SET title = ? WHERE id = ?");
        s.bind(1, title).bind(2, id);
        s.run();
    } else {
        auto s = m_db.prepare("UPDATE bookmarks SET title = ?, url = ? WHERE id = ?");
        s.bind(1, title).bind(2, url).bind(3, id);
        s.run();
    }
    return true;
}

std::optional<Bookmark> Bookmarks::get(int64_t id)
{
    std::string sql = std::string("SELECT ") + kColumns + " FROM bookmarks WHERE id = ?";
    auto s = m_db.prepare(sql.c_str());
    s.bind(1, id);
    if (!s.step())
        return std::nullopt;
    return readBookmark(s);
}

std::vector<Bookmark> Bookmarks::children(int64_t parent)
{
    std::string sql = std::string("SELECT ") + kColumns + " FROM bookmarks WHERE parent_id = ? ORDER BY position";
    auto s = m_db.prepare(sql.c_str());
    s.bind(1, parent);
    std::vector<Bookmark> out;
    while (s.step())
        out.push_back(readBookmark(s));
    return out;
}

std::optional<Bookmark> Bookmarks::findByURL(const std::wstring& url)
{
    std::string sql = std::string("SELECT ") + kColumns + " FROM bookmarks WHERE url = ? AND is_folder = 0 ORDER BY id LIMIT 1";
    auto s = m_db.prepare(sql.c_str());
    s.bind(1, url);
    if (!s.step())
        return std::nullopt;
    return readBookmark(s);
}

std::vector<Bookmark> Bookmarks::find(const std::vector<std::wstring>& words, size_t limit)
{
    size_t n = std::min(words.size(), kMaxWords);
    std::string sql = std::string("SELECT ") + kColumns + " FROM bookmarks WHERE is_folder = 0";
    for (size_t i = 0; i < n; ++i) {
        std::string p = "?" + std::to_string(i + 1);
        sql += " AND (url LIKE " + p + " ESCAPE '\\' OR title LIKE " + p + " ESCAPE '\\')";
    }
    sql += " ORDER BY added DESC, id DESC LIMIT " + std::to_string(limit);
    auto s = m_db.prepare(sql.c_str());
    for (size_t i = 0; i < n; ++i)
        s.bind(int(i + 1), likePattern(words[i]));
    std::vector<Bookmark> out;
    while (s.step())
        out.push_back(readBookmark(s));
    return out;
}

// ---- Netscape bookmark file -------------------------------------------------

void Bookmarks::exportFolder(int64_t id, int depth, std::string& out)
{
    std::string indent(size_t(depth) * 4, ' ');
    for (auto& b : children(id)) {
        if (b.folder) {
            out += indent + "<DT><H3 ADD_DATE=\"" + std::to_string(b.added) + "\">" + escapeHTML(b.title) + "</H3>\n";
            out += indent + "<DL><p>\n";
            exportFolder(b.id, depth + 1, out);
            out += indent + "</DL><p>\n";
        } else
            out += indent + "<DT><A HREF=\"" + escapeHTML(b.url) + "\" ADD_DATE=\"" + std::to_string(b.added) + "\">"
                + escapeHTML(b.title) + "</A>\n";
    }
}

std::string Bookmarks::exportHTML()
{
    std::string out = "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n"
                      "<!-- This is an automatically generated file. -->\n"
                      "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n"
                      "<TITLE>Bookmarks</TITLE>\n"
                      "<H1>Bookmarks</H1>\n"
                      "<DL><p>\n"
                      "    <DT><H3 PERSONAL_TOOLBAR_FOLDER=\"true\">Favorites</H3>\n"
                      "    <DL><p>\n";
    exportFolder(kFavoritesId, 2, out);
    out += "    </DL><p>\n";
    exportFolder(kBookmarksId, 1, out);
    out += "</DL><p>\n";
    return out;
}

size_t Bookmarks::importHTML(const std::string& html)
{
    Database::Transaction tx(m_db);
    size_t added = 0;
    std::vector<int64_t> stack { kBookmarksId };
    int64_t pendingFolder = 0; // folder named by the last <H3>, entered at the next <DL>
    int skippedDepth = 0;      // <DL> levels beyond kMaxImportDepth
    bool sawList = false;

    size_t i = 0;
    while ((i = html.find('<', i)) != std::string::npos) {
        if (html.compare(i, 4, "<!--") == 0) {
            size_t end = html.find("-->", i);
            i = end == std::string::npos ? html.size() : end + 3;
            continue;
        }
        size_t close = html.find('>', i);
        if (close == std::string::npos)
            break;
        std::string tag = html.substr(i + 1, close - i - 1);
        size_t nameEnd = tag.find_first_of(" \t\r\n");
        std::string name;
        for (char c : tag.substr(0, nameEnd))
            name.push_back(lowerAscii(c));
        std::string attrs = nameEnd == std::string::npos ? std::string() : tag.substr(nameEnd);
        i = close + 1;

        auto innerText = [&](const char* endTag) {
            // Text up to the matching end tag (case-insensitive), tags inside dropped.
            size_t j = i;
            std::string text;
            while (j < html.size()) {
                if (html[j] == '<') {
                    size_t e = html.find('>', j);
                    if (e == std::string::npos) {
                        j = html.size();
                        break;
                    }
                    std::string inner;
                    for (char c : html.substr(j + 1, e - j - 1))
                        inner.push_back(lowerAscii(c));
                    j = e + 1;
                    if (inner == endTag)
                        break;
                    continue;
                }
                text.push_back(html[j++]);
            }
            i = j;
            return fromUtf8(unescapeHTML(trimmed(text)));
        };

        if (name == "dl") {
            if (!sawList) {
                sawList = true; // the outermost list is the import root itself
                continue;
            }
            if (int(stack.size()) >= kMaxImportDepth) {
                ++skippedDepth;
                continue;
            }
            stack.push_back(pendingFolder ? pendingFolder : stack.back());
            pendingFolder = 0;
        } else if (name == "/dl") {
            if (skippedDepth)
                --skippedDepth;
            else if (stack.size() > 1)
                stack.pop_back();
            pendingFolder = 0;
        } else if (name == "h3") {
            std::wstring title = innerText("/h3");
            std::string toolbar = attribute(attrs, "personal_toolbar_folder");
            if (toolbar == "true" || toolbar == "TRUE")
                pendingFolder = kFavoritesId;
            else
                pendingFolder = insert(stack.back(), true, title, {}, -1, unixNow());
        } else if (name == "a") {
            std::wstring url = fromUtf8(attribute(attrs, "href"));
            std::string date = attribute(attrs, "add_date");
            std::wstring title = innerText("/a");
            if (!importableURL(url))
                continue;
            int64_t when = date.empty() ? unixNow() : std::strtoll(date.c_str(), nullptr, 10);
            if (insert(stack.back(), false, title.empty() ? url : title, url, -1, when))
                ++added;
        }
    }
    tx.commit();
    return added;
}

} // namespace meridian::services
