// M5: Reading List, Tab Groups, session store with groups.
#include "services/ReadingList.h"
#include "services/SessionStore.h"
#include "services/TabGroups.h"
#include "test.h"

using namespace meridian::services;

namespace {
constexpr int64_t kNow = 1800000000;
}

TEST(readingListAddAndOrder)
{
    Database db(":memory:");
    ReadingList r(db);
    int64_t a = r.add(L"https://a.example/", L"A", L"first", kNow - 20);
    int64_t b = r.add(L"https://b.example/", L"B", L"", kNow - 10);
    CHECK(a && b && a != b);
    CHECK(r.add(L"about:blank", L"", L"", kNow) == 0);
    CHECK(r.add(L"javascript:alert(1)", L"", L"", kNow) == 0);
    auto items = r.list();
    CHECK(items.size() == 2);
    CHECK_EQ(items[0].url, L"https://b.example/"); // newest first
    CHECK(items[0].unread());
    CHECK_EQ(items[1].excerpt, L"first");
}

TEST(readingListReAddMovesToTopAndUnread)
{
    Database db(":memory:");
    ReadingList r(db);
    int64_t a = r.add(L"https://a.example/", L"A", L"excerpt", kNow - 20);
    r.add(L"https://b.example/", L"B", L"", kNow - 10);
    r.setRead(a, true, kNow - 5);
    CHECK(!r.get(a)->unread());
    CHECK(r.add(L"https://a.example/", L"", L"", kNow) == a); // same row
    auto items = r.list();
    CHECK(items.size() == 2);
    CHECK_EQ(items[0].url, L"https://a.example/");
    CHECK(items[0].unread());
    CHECK_EQ(items[0].title, L"A");       // empty title doesn't wipe the old one
    CHECK_EQ(items[0].excerpt, L"excerpt");
}

TEST(readingListReadStateAndClear)
{
    Database db(":memory:");
    ReadingList r(db);
    int64_t a = r.add(L"https://a.example/", L"A", L"", kNow);
    int64_t b = r.add(L"https://b.example/", L"B", L"", kNow);
    r.add(L"https://c.example/", L"C", L"", kNow);
    r.setRead(a, true, kNow);
    r.setRead(b, true, 0); // a zero clock still counts as read
    CHECK(r.count() == 3);
    CHECK(r.count(true) == 1);
    CHECK(r.list(true).size() == 1);
    r.setRead(b, false, kNow);
    CHECK(r.count(true) == 2);
    CHECK(r.clearRead() == 1);
    CHECK(r.count() == 2);
    CHECK(r.findByURL(L"https://b.example/").has_value());
    CHECK(r.remove(b));
    CHECK(!r.remove(b));
    CHECK(!r.findByURL(L"https://b.example/"));
}

TEST(readingListCapsLongText)
{
    Database db(":memory:");
    ReadingList r(db);
    int64_t id = r.add(L"https://a.example/", std::wstring(5000, L't'), std::wstring(5000, L'e'), kNow);
    auto item = r.get(id);
    CHECK(item->title.size() == 500);
    CHECK(item->excerpt.size() == 400);
}

TEST(tabGroupsCreateRenameOrder)
{
    Database db(":memory:");
    TabGroups g(db);
    int64_t work = g.create(L"  Work\t", kNow);
    int64_t trip = g.create(L"Trip", kNow);
    int64_t untitled = g.create(L"   ", kNow);
    auto list = g.list();
    CHECK(list.size() == 3);
    CHECK_EQ(list[0].name, L"Work");
    CHECK_EQ(list[2].name, L"Untitled");
    CHECK_EQ(g.uniqueName(L"Untitled"), L"Untitled 2");
    CHECK_EQ(g.uniqueName(L"Work"), L"Work 2");
    CHECK_EQ(g.uniqueName(L"New"), L"New");
    CHECK(g.rename(trip, L"Holiday"));
    CHECK(!g.rename(trip, L"  "));
    CHECK(!g.rename(999, L"x"));
    CHECK_EQ(g.get(trip)->name, L"Holiday");
    CHECK(g.move(untitled, 0));
    list = g.list();
    CHECK(list[0].id == untitled && list[1].id == work && list[2].id == trip);
    CHECK(g.move(untitled, 99));
    CHECK(g.list()[2].id == untitled);
    CHECK(!g.move(12345, 0));
}

TEST(tabGroupsTabsRoundTripAndCascade)
{
    Database db(":memory:");
    TabGroups g(db);
    int64_t id = g.create(L"Work", kNow);
    std::vector<SessionTab> tabs { { L"https://a.example/", L"A", true, { 1, 2, 3 } }, { L"https://b.example/", L"B", false, {} } };
    g.saveTabs(id, tabs, 1);
    int selected = -1;
    auto loaded = g.tabs(id, &selected);
    CHECK(loaded.size() == 2);
    CHECK(selected == 1);
    CHECK(!loaded[0].pinned); // groups never hold pinned tabs
    CHECK((loaded[0].state == std::vector<uint8_t> { 1, 2, 3 }));
    CHECK(g.tabCount(id) == 2);
    g.saveTabs(id, { tabs[1] }, 7); // replace; selection clamped on read
    loaded = g.tabs(id, &selected);
    CHECK(loaded.size() == 1 && selected == 0);
    CHECK(g.remove(id));
    CHECK(!g.remove(id));
    CHECK(g.tabCount(id) == 0);
    auto rows = db.prepare("SELECT count(*) FROM tab_group_tabs");
    CHECK(rows.step() && rows.integer(0) == 0);
}

TEST(sessionKeepsGroupAndStash)
{
    Database db(":memory:");
    SessionStore store(db);
    SessionWindow w;
    w.groupId = 42;
    w.tabs = { { L"https://pinned.example/", L"P", true, {} }, { L"https://group.example/", L"G", false, {} } };
    w.stash = { { L"https://s1.example/", L"S1", false, {} }, { L"https://s2.example/", L"S2", false, { 9 } } };
    w.stashSelected = 1;
    store.save({ w });
    auto loaded = store.load();
    CHECK(loaded.size() == 1);
    CHECK(loaded[0].groupId == 42);
    CHECK(loaded[0].tabs.size() == 2);
    CHECK(loaded[0].stash.size() == 2);
    CHECK(loaded[0].stashSelected == 1);
    CHECK_EQ(loaded[0].stash[1].url, L"https://s2.example/");
    CHECK((loaded[0].stash[1].state == std::vector<uint8_t> { 9 }));
}

TEST(sessionMigratesM4Schema)
{
    // A profile written by M3/M4: no group or stash columns yet.
    Database db(":memory:");
    db.exec(R"SQL(
        CREATE TABLE session_windows (id INTEGER PRIMARY KEY, position INTEGER NOT NULL, selected INTEGER NOT NULL,
            x0 INTEGER, y0 INTEGER, x1 INTEGER, y1 INTEGER, maximized INTEGER NOT NULL DEFAULT 0);
        CREATE TABLE session_tabs (window_id INTEGER NOT NULL REFERENCES session_windows(id) ON DELETE CASCADE,
            position INTEGER NOT NULL, url TEXT NOT NULL, title TEXT NOT NULL, pinned INTEGER NOT NULL DEFAULT 0, state BLOB);
        INSERT INTO session_windows VALUES (1, 0, 0, 10, 10, 800, 600, 0);
        INSERT INTO session_tabs VALUES (1, 0, 'https://old.example/', 'Old', 0, NULL);
    )SQL");
    SessionStore store(db);
    auto loaded = store.load();
    CHECK(loaded.size() == 1);
    CHECK(loaded[0].tabs.size() == 1);
    CHECK_EQ(loaded[0].tabs[0].url, L"https://old.example/");
    CHECK(loaded[0].groupId == 0);
    CHECK(loaded[0].stash.empty());
    SessionStore again(db); // migrating twice is a no-op
    store.save(loaded);
    CHECK(store.load().size() == 1);
}

TEST(sessionWindowWithOnlyStashSurvives)
{
    Database db(":memory:");
    SessionStore store(db);
    SessionWindow w;
    w.groupId = 3;
    w.stash = { { L"https://s.example/", L"S", false, {} } };
    store.save({ w });
    CHECK(store.load().size() == 1);
}
