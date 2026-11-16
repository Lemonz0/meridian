#include "services/SessionStore.h"
#include "test.h"

#include <filesystem>
#include <windows.h>

using namespace meridian::services;

static std::filesystem::path tempDb()
{
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    auto p = std::filesystem::path(dir) / (L"meridian-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".db");
    return p;
}

TEST(sessionRoundTrip)
{
    auto path = tempDb();
    {
        Database db(path);
        SessionStore store(db);
        SessionWindow w;
        w.selected = 1;
        w.left = 10; w.top = 20; w.right = 1290; w.bottom = 880;
        w.maximized = true;
        w.tabs.push_back({ L"https://webkit.org/", L"WebKit", true, { 1, 2, 3, 0, 255 } });
        w.tabs.push_back({ L"https://example.com/", L"Example — é", false, {} });
        SessionWindow w2;
        w2.tabs.push_back({ L"about:blank", L"", false, {} });
        store.save({ w, w2 });
    }
    {
        Database db(path); // reopen: data must be on disk
        SessionStore store(db);
        auto windows = store.load();
        CHECK(windows.size() == 2);
        CHECK(windows[0].tabs.size() == 2);
        CHECK(windows[0].selected == 1);
        CHECK(windows[0].maximized);
        CHECK(windows[0].right == 1290);
        CHECK_EQ(windows[0].tabs[0].url, L"https://webkit.org/");
        CHECK(windows[0].tabs[0].pinned);
        CHECK((windows[0].tabs[0].state == std::vector<uint8_t> { 1, 2, 3, 0, 255 }));
        CHECK_EQ(windows[0].tabs[1].title, L"Example — é");
        CHECK(windows[0].tabs[1].state.empty());
        CHECK(windows[1].tabs.size() == 1);
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path.wstring() + L"-wal", ec);
    std::filesystem::remove(path.wstring() + L"-shm", ec);
}

TEST(sessionSaveReplaces)
{
    Database db(":memory:");
    SessionStore store(db);
    SessionWindow a;
    a.tabs = { { L"https://a/", L"a" }, { L"https://b/", L"b" } };
    store.save({ a, a });
    SessionWindow b;
    b.tabs = { { L"https://c/", L"c" } };
    store.save({ b });
    auto loaded = store.load();
    CHECK(loaded.size() == 1);
    CHECK(loaded[0].tabs.size() == 1);
    CHECK_EQ(loaded[0].tabs[0].url, L"https://c/");
}

TEST(sessionSelectedIsClamped)
{
    Database db(":memory:");
    SessionStore store(db);
    SessionWindow w;
    w.selected = 7;
    w.tabs = { { L"https://a/", L"a" } };
    store.save({ w });
    CHECK(store.load()[0].selected == 0);
}

TEST(sessionEmptyWindowsDropped)
{
    Database db(":memory:");
    SessionStore store(db);
    store.save({ SessionWindow {} });
    CHECK(store.load().empty());
}
