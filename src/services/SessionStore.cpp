#include "SessionStore.h"

#include <algorithm>

namespace meridian::services {

SessionStore::SessionStore(Database& db)
    : m_db(db)
{
    m_db.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS session_windows (
            id INTEGER PRIMARY KEY,
            position INTEGER NOT NULL,
            selected INTEGER NOT NULL,
            x0 INTEGER, y0 INTEGER, x1 INTEGER, y1 INTEGER,
            maximized INTEGER NOT NULL DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS session_tabs (
            window_id INTEGER NOT NULL REFERENCES session_windows(id) ON DELETE CASCADE,
            position INTEGER NOT NULL,
            url TEXT NOT NULL,
            title TEXT NOT NULL,
            pinned INTEGER NOT NULL DEFAULT 0,
            state BLOB
        );
    )SQL");
    // M5 columns. Profiles from M3/M4 have the tables without them.
    auto hasColumn = [&](const char* table, const wchar_t* column) {
        auto q = m_db.prepare((std::string("PRAGMA table_info(") + table + ")").c_str());
        while (q.step())
            if (q.text(1) == column)
                return true;
        return false;
    };
    if (!hasColumn("session_windows", L"group_id"))
        m_db.exec("ALTER TABLE session_windows ADD COLUMN group_id INTEGER NOT NULL DEFAULT 0");
    if (!hasColumn("session_windows", L"stash_selected"))
        m_db.exec("ALTER TABLE session_windows ADD COLUMN stash_selected INTEGER NOT NULL DEFAULT 0");
    if (!hasColumn("session_tabs", L"stashed"))
        m_db.exec("ALTER TABLE session_tabs ADD COLUMN stashed INTEGER NOT NULL DEFAULT 0");
}

void SessionStore::save(const std::vector<SessionWindow>& windows)
{
    Database::Transaction tx(m_db);
    m_db.exec("DELETE FROM session_tabs; DELETE FROM session_windows;");
    auto insertWindow = m_db.prepare(
        "INSERT INTO session_windows (id, position, selected, x0, y0, x1, y1, maximized, group_id, stash_selected) VALUES (?,?,?,?,?,?,?,?,?,?)");
    auto insertTab = m_db.prepare(
        "INSERT INTO session_tabs (window_id, position, url, title, pinned, state, stashed) VALUES (?,?,?,?,?,?,?)");
    for (size_t w = 0; w < windows.size(); ++w) {
        const auto& win = windows[w];
        insertWindow.reset();
        insertWindow.bind(1, int64_t(w + 1)).bind(2, int64_t(w)).bind(3, int64_t(win.selected))
            .bind(4, int64_t(win.left)).bind(5, int64_t(win.top)).bind(6, int64_t(win.right)).bind(7, int64_t(win.bottom))
            .bind(8, int64_t(win.maximized)).bind(9, win.groupId).bind(10, int64_t(win.stashSelected));
        insertWindow.run();
        auto saveTabs = [&](const std::vector<SessionTab>& tabs, bool stashed) {
            for (size_t t = 0; t < tabs.size(); ++t) {
                const auto& tab = tabs[t];
                insertTab.reset();
                insertTab.bind(1, int64_t(w + 1)).bind(2, int64_t(t)).bind(3, tab.url).bind(4, tab.title)
                    .bind(5, int64_t(tab.pinned)).bind(6, tab.state).bind(7, int64_t(stashed));
                insertTab.run();
            }
        };
        saveTabs(win.tabs, false);
        saveTabs(win.stash, true);
    }
    tx.commit();
}

std::vector<SessionWindow> SessionStore::load()
{
    std::vector<SessionWindow> windows;
    std::vector<int64_t> ids;
    auto qw = m_db.prepare("SELECT id, selected, x0, y0, x1, y1, maximized, group_id, stash_selected FROM session_windows ORDER BY position");
    while (qw.step()) {
        SessionWindow w;
        ids.push_back(qw.integer(0));
        w.selected = int(qw.integer(1));
        w.left = int(qw.integer(2));
        w.top = int(qw.integer(3));
        w.right = int(qw.integer(4));
        w.bottom = int(qw.integer(5));
        w.maximized = qw.integer(6) != 0;
        w.groupId = qw.integer(7);
        w.stashSelected = int(qw.integer(8));
        windows.push_back(std::move(w));
    }
    auto qt = m_db.prepare("SELECT url, title, pinned, state, stashed FROM session_tabs WHERE window_id = ? ORDER BY position");
    for (size_t i = 0; i < windows.size(); ++i) {
        qt.reset();
        qt.bind(1, ids[i]);
        auto& w = windows[i];
        while (qt.step()) {
            SessionTab tab { qt.text(0), qt.text(1), qt.integer(2) != 0, qt.blob(3) };
            (qt.integer(4) ? w.stash : w.tabs).push_back(std::move(tab));
        }
        if (!w.tabs.empty())
            w.selected = std::clamp(w.selected, 0, int(w.tabs.size()) - 1);
        w.stashSelected = w.stash.empty() ? 0 : std::clamp(w.stashSelected, 0, int(w.stash.size()) - 1);
    }
    windows.erase(std::remove_if(windows.begin(), windows.end(), [](auto& w) { return w.tabs.empty() && w.stash.empty(); }), windows.end());
    return windows;
}

} // namespace meridian::services
