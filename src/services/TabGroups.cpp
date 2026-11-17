#include "TabGroups.h"

#include <algorithm>
#include <cwctype>

namespace meridian::services {

namespace {
constexpr size_t kMaxName = 200;

std::wstring cleanName(const std::wstring& raw)
{
    std::wstring s;
    for (wchar_t c : raw)
        s.push_back(iswcntrl(c) ? L' ' : c);
    size_t b = 0, e = s.size();
    while (b < e && iswspace(s[b])) ++b;
    while (e > b && iswspace(s[e - 1])) --e;
    return s.substr(b, std::min(e - b, kMaxName));
}
}

TabGroups::TabGroups(Database& db)
    : m_db(db)
{
    m_db.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS tab_groups (
            id INTEGER PRIMARY KEY,
            name TEXT NOT NULL,
            position INTEGER NOT NULL,
            selected INTEGER NOT NULL DEFAULT 0,
            created INTEGER NOT NULL DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS tab_group_tabs (
            group_id INTEGER NOT NULL REFERENCES tab_groups(id) ON DELETE CASCADE,
            position INTEGER NOT NULL,
            url TEXT NOT NULL,
            title TEXT NOT NULL,
            state BLOB
        );
        CREATE INDEX IF NOT EXISTS tab_group_tabs_group ON tab_group_tabs(group_id, position);
    )SQL");
}

std::vector<TabGroup> TabGroups::list()
{
    auto s = m_db.prepare("SELECT id, name, created FROM tab_groups ORDER BY position, id");
    std::vector<TabGroup> out;
    while (s.step())
        out.push_back({ s.integer(0), s.text(1), s.integer(2) });
    return out;
}

std::optional<TabGroup> TabGroups::get(int64_t id)
{
    auto s = m_db.prepare("SELECT id, name, created FROM tab_groups WHERE id = ?");
    s.bind(1, id);
    if (!s.step())
        return std::nullopt;
    return TabGroup { s.integer(0), s.text(1), s.integer(2) };
}

std::wstring TabGroups::uniqueName(const std::wstring& base)
{
    std::wstring clean = cleanName(base);
    if (clean.empty())
        clean = L"Untitled";
    auto groups = list();
    auto taken = [&](const std::wstring& n) {
        return std::any_of(groups.begin(), groups.end(), [&](auto& g) { return g.name == n; });
    };
    if (!taken(clean))
        return clean;
    for (int i = 2;; ++i) {
        std::wstring candidate = clean + L" " + std::to_wstring(i);
        if (!taken(candidate))
            return candidate;
    }
}

int64_t TabGroups::create(const std::wstring& name, int64_t now)
{
    std::wstring clean = cleanName(name);
    if (clean.empty())
        clean = uniqueName(L"Untitled");
    auto s = m_db.prepare("INSERT INTO tab_groups (name, position, created) VALUES (?, (SELECT coalesce(max(position), -1) + 1 FROM tab_groups), ?)");
    s.bind(1, clean).bind(2, now);
    s.run();
    auto id = m_db.prepare("SELECT last_insert_rowid()");
    return id.step() ? id.integer(0) : 0;
}

bool TabGroups::rename(int64_t id, const std::wstring& name)
{
    std::wstring clean = cleanName(name);
    if (clean.empty() || !get(id))
        return false;
    auto s = m_db.prepare("UPDATE tab_groups SET name = ? WHERE id = ?");
    s.bind(1, clean).bind(2, id);
    s.run();
    return true;
}

bool TabGroups::remove(int64_t id)
{
    if (!get(id))
        return false;
    auto s = m_db.prepare("DELETE FROM tab_groups WHERE id = ?");
    s.bind(1, id);
    s.run();
    return true;
}

bool TabGroups::move(int64_t id, int position)
{
    auto groups = list();
    auto it = std::find_if(groups.begin(), groups.end(), [&](auto& g) { return g.id == id; });
    if (it == groups.end())
        return false;
    TabGroup moving = *it;
    groups.erase(it);
    position = std::clamp(position, 0, int(groups.size()));
    groups.insert(groups.begin() + position, moving);
    Database::Transaction tx(m_db);
    auto s = m_db.prepare("UPDATE tab_groups SET position = ? WHERE id = ?");
    for (size_t i = 0; i < groups.size(); ++i) {
        s.reset();
        s.bind(1, int64_t(i)).bind(2, groups[i].id);
        s.run();
    }
    tx.commit();
    return true;
}

std::vector<SessionTab> TabGroups::tabs(int64_t id, int* selected)
{
    std::vector<SessionTab> out;
    auto s = m_db.prepare("SELECT url, title, state FROM tab_group_tabs WHERE group_id = ? ORDER BY position");
    s.bind(1, id);
    while (s.step())
        out.push_back({ s.text(0), s.text(1), false, s.blob(2) });
    if (selected) {
        auto q = m_db.prepare("SELECT selected FROM tab_groups WHERE id = ?");
        q.bind(1, id);
        int value = q.step() ? int(q.integer(0)) : 0;
        *selected = out.empty() ? 0 : std::clamp(value, 0, int(out.size()) - 1);
    }
    return out;
}

void TabGroups::saveTabs(int64_t id, const std::vector<SessionTab>& tabs, int selected)
{
    Database::Transaction tx(m_db);
    auto del = m_db.prepare("DELETE FROM tab_group_tabs WHERE group_id = ?");
    del.bind(1, id);
    del.run();
    auto ins = m_db.prepare("INSERT INTO tab_group_tabs (group_id, position, url, title, state) VALUES (?, ?, ?, ?, ?)");
    for (size_t i = 0; i < tabs.size(); ++i) {
        ins.reset();
        ins.bind(1, id).bind(2, int64_t(i)).bind(3, tabs[i].url).bind(4, tabs[i].title).bind(5, tabs[i].state);
        ins.run();
    }
    auto sel = m_db.prepare("UPDATE tab_groups SET selected = ? WHERE id = ?");
    sel.bind(1, int64_t(selected)).bind(2, id);
    sel.run();
    tx.commit();
}

size_t TabGroups::tabCount(int64_t id)
{
    auto s = m_db.prepare("SELECT count(*) FROM tab_group_tabs WHERE group_id = ?");
    s.bind(1, id);
    return s.step() ? size_t(s.integer(0)) : 0;
}

} // namespace meridian::services
