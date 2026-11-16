#include "services/History.h"
#include "services/IconStore.h"
#include "services/Settings.h"
#include "services/Text.h"
#include "test.h"

using namespace meridian::services;

namespace {
constexpr int64_t kNow = 1800000000;
constexpr int64_t kDay = 86400;
}

TEST(historyRecordsAndCounts)
{
    Database db(":memory:");
    History h(db);
    int64_t a = h.recordVisit(L"https://webkit.org/", L"WebKit", true, kNow - 100);
    int64_t b = h.recordVisit(L"https://webkit.org/", L"", false, kNow - 50);
    CHECK(a != 0 && a == b);
    CHECK(h.count() == 1);
    auto all = h.search(L"", 10);
    CHECK(all.size() == 1);
    CHECK(all[0].visitCount == 2);
    CHECK(all[0].typedCount == 1);
    CHECK(all[0].lastVisit == kNow - 50);
    CHECK_EQ(all[0].title, L"WebKit"); // an empty title never replaces a real one
}

TEST(historySkipsNonPages)
{
    Database db(":memory:");
    History h(db);
    CHECK(h.recordVisit(L"about:blank", L"", false, kNow) == 0);
    CHECK(h.recordVisit(L"data:text/html,hi", L"", false, kNow) == 0);
    CHECK(h.recordVisit(L"javascript:alert(1)", L"", false, kNow) == 0);
    CHECK(h.recordVisit(L"", L"", false, kNow) == 0);
    CHECK(h.recordVisit(L"HTTP://example.com/", L"", false, kNow) != 0);
    CHECK(h.recordVisit(L"file:///C:/page.html", L"", false, kNow) != 0);
    CHECK(h.count() == 2);
}

TEST(historySearchMatchesAllWords)
{
    Database db(":memory:");
    History h(db);
    h.recordVisit(L"https://webkit.org/blog/", L"Blog | WebKit", false, kNow - 30);
    h.recordVisit(L"https://example.com/", L"Example Domain", false, kNow - 20);
    h.recordVisit(L"https://example.com/100%_done", L"Progress", false, kNow - 10);
    CHECK(h.search(L"webkit blog", 10).size() == 1);
    CHECK(h.search(L"WEBKIT", 10).size() == 1);
    CHECK(h.search(L"example", 10).size() == 2);
    CHECK(h.search(L"example webkit", 10).empty());
    // LIKE wildcards in the query are literal.
    CHECK(h.search(L"%", 10).size() == 1);
    CHECK(h.search(L"_", 10).size() == 1);
    auto recent = h.search(L"", 10);
    CHECK(recent.size() == 3);
    CHECK_EQ(recent[0].title, L"Progress"); // newest first
}

TEST(historyTitleUpdate)
{
    Database db(":memory:");
    History h(db);
    h.recordVisit(L"https://example.com/", L"", false, kNow);
    h.setTitle(L"https://example.com/", L"Example — é");
    h.setTitle(L"https://example.com/", L""); // ignored
    CHECK_EQ(h.search(L"", 1)[0].title, L"Example — é");
}

TEST(historyRemoveAndClear)
{
    Database db(":memory:");
    History h(db);
    int64_t a = h.recordVisit(L"https://a.example/", L"a", false, kNow);
    h.recordVisit(L"https://b.example/", L"b", false, kNow);
    h.remove(a);
    CHECK(h.count() == 1);
    auto visits = db.prepare("SELECT count(*) FROM history_visits");
    CHECK(visits.step() && visits.integer(0) == 1); // visits go with their URL
    h.clear();
    CHECK(h.count() == 0);
}

TEST(historyRemoveVisitsSince)
{
    Database db(":memory:");
    History h(db);
    h.recordVisit(L"https://old.example/", L"old", true, kNow - 5000);
    h.recordVisit(L"https://old.example/", L"old", true, kNow - 10);
    h.recordVisit(L"https://new.example/", L"new", false, kNow - 5);
    h.removeVisitsSince(kNow - 3600); // "clear the last hour"
    auto all = h.search(L"", 10);
    CHECK(all.size() == 1);
    CHECK_EQ(all[0].url, L"https://old.example/");
    CHECK(all[0].visitCount == 1);
    CHECK(all[0].typedCount == 1);
    CHECK(all[0].lastVisit == kNow - 5000);
}

TEST(frecencyOrdering)
{
    // More visits win at equal age; recent wins at equal visits.
    CHECK(frecencyScore(5, 0, kDay) > frecencyScore(2, 0, kDay));
    CHECK(frecencyScore(2, 0, kDay) > frecencyScore(2, 0, 20 * kDay));
    CHECK(frecencyScore(2, 0, 20 * kDay) > frecencyScore(2, 0, 200 * kDay));
    // Typed visits count for more.
    CHECK(frecencyScore(2, 2, kDay) > frecencyScore(2, 0, kDay));
    // A page used daily last week beats one opened once today.
    CHECK(frecencyScore(7, 0, 8 * kDay) > frecencyScore(1, 0, 0));
    // Bad data can't inflate a score.
    CHECK(frecencyScore(1, 50, 0) == frecencyScore(1, 1, 0));
    CHECK(frecencyScore(0, 0, 0) == 0);
}

TEST(frequentlyVisitedNeedsRepeatsAndRecency)
{
    Database db(":memory:");
    History h(db);
    h.recordVisit(L"https://once.example/", L"once", false, kNow - kDay);
    for (int i = 0; i < 5; ++i)
        h.recordVisit(L"https://daily.example/", L"daily", false, kNow - kDay * i);
    for (int i = 0; i < 2; ++i)
        h.recordVisit(L"https://twice.example/", L"twice", false, kNow - kDay);
    for (int i = 0; i < 9; ++i)
        h.recordVisit(L"https://stale.example/", L"stale", false, kNow - 200 * kDay);
    auto top = h.frequentlyVisited(8, kNow);
    CHECK(top.size() == 2);
    CHECK_EQ(top[0].url, L"https://daily.example/");
    CHECK_EQ(top[1].url, L"https://twice.example/");
    CHECK(h.frequentlyVisited(1, kNow).size() == 1);
}

TEST(textHelpers)
{
    CHECK_EQ(strippedURL(L"https://www.Example.com/A?b"), L"example.com/a?b");
    CHECK_EQ(strippedURL(L"example.com"), L"example.com");
    CHECK_EQ(strippedURL(L"example.com/?u=http://x"), L"example.com/?u=http://x");
    CHECK_EQ(originOf(L"https://www.example.com/a?b"), L"https://www.example.com");
    CHECK_EQ(originOf(L"http://localhost:8080"), L"http://localhost:8080");
    CHECK_EQ(originOf(L"about:blank"), L"");
    CHECK_EQ(toLower(L"ÉCOLE Ж"), L"école ж");
    auto words = splitWords(L"  Hello\tWORLD  ");
    CHECK(words.size() == 2);
    CHECK_EQ(words[1], L"world");
    CHECK_EQ(fromUtf8(toUtf8(L"日本語 é")), L"日本語 é");
}

TEST(settingsRoundTrip)
{
    Database db(":memory:");
    Settings s(db);
    CHECK(!s.getBool(settings::kSearchSuggestions)); // opt-in: off unless set
    s.setBool(settings::kSearchSuggestions, true);
    CHECK(s.getBool(settings::kSearchSuggestions));
    s.set(L"k", L"v1");
    s.set(L"k", L"v2");
    CHECK_EQ(s.get(L"k"), L"v2");
    CHECK_EQ(s.get(L"missing", L"fallback"), L"fallback");
}

TEST(iconStoreRoundTrip)
{
    Database db(":memory:");
    Bitmap icon;
    icon.width = 2;
    icon.height = 1;
    icon.bgra = { 1, 2, 3, 255, 4, 5, 6, 255 };
    {
        IconStore store(db);
        store.put(L"example.com", icon);
        Bitmap bad = icon;
        bad.bgra.pop_back(); // size doesn't match the dimensions
        store.put(L"bad.example", bad);
        store.put(L"", icon);
    }
    IconStore store(db); // fresh cache: must come from the database
    CHECK(store.get(L"example.com").width == 2);
    CHECK(store.get(L"example.com").bgra == icon.bgra);
    CHECK(!store.get(L"bad.example"));
    CHECK(!store.get(L"other.example"));
    store.clear();
    CHECK(!store.get(L"example.com"));
}
