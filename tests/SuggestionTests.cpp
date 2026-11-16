#include "services/SearchSuggestions.h"
#include "services/Suggestions.h"
#include "test.h"

using namespace meridian::services;

namespace {
constexpr int64_t kNow = 1800000000;
constexpr int64_t kDay = 86400;

struct Fixture {
    Database db { ":memory:" };
    History history { db };
    Bookmarks bookmarks { db };
    SuggestionOptions options;
    Fixture()
    {
        options.searchPrefix = L"https://search.example/?q=";
        options.now = kNow;
    }
    void visit(const wchar_t* url, const wchar_t* title, int times = 1, int64_t age = kDay)
    {
        for (int i = 0; i < times; ++i)
            history.recordVisit(url, title, false, kNow - age);
    }
    SuggestionList suggest(const wchar_t* input) { return buildSuggestions(history, bookmarks, input, options); }
};

const Suggestion* findKind(const SuggestionList& l, SuggestionKind k)
{
    for (auto& s : l.items)
        if (s.kind == k)
            return &s;
    return nullptr;
}
}

TEST(suggestEmptyInput)
{
    Fixture f;
    f.visit(L"https://webkit.org/", L"WebKit");
    CHECK(f.suggest(L"").items.empty());
    CHECK(f.suggest(L"   ").items.empty());
}

TEST(suggestNoHistoryStillOffersSearch)
{
    Fixture f;
    auto l = f.suggest(L"hello world");
    CHECK(l.items.size() == 1);
    CHECK(l.items[0].kind == SuggestionKind::Search);
    CHECK_EQ(l.items[0].url, L"https://search.example/?q=hello+world");
    CHECK(l.defaultSelection == -1);
    CHECK(l.completion.empty());
}

TEST(suggestTypedAddressOffersNavigate)
{
    Fixture f;
    auto l = f.suggest(L"example.com/x");
    CHECK(l.items.size() == 1);
    CHECK(l.items[0].kind == SuggestionKind::Navigate);
    CHECK_EQ(l.items[0].url, L"https://example.com/x");
}

TEST(suggestCompletesHost)
{
    Fixture f;
    f.visit(L"https://webkit.org/", L"WebKit", 3);
    f.visit(L"https://webkit.org/blog/", L"Blog | WebKit", 2);
    f.visit(L"https://www.wikipedia.org/", L"Wikipedia", 1);
    auto l = f.suggest(L"we");
    CHECK_EQ(l.completion, L"bkit.org");
    CHECK(l.defaultSelection == 0);
    CHECK(l.items[0].kind == SuggestionKind::TopHit);
    CHECK_EQ(l.items[0].url, L"https://webkit.org/");
    CHECK_EQ(l.items[0].title, L"WebKit");
    CHECK(l.items[0].historyId != 0);
    CHECK(l.items[1].kind == SuggestionKind::Search);
    // The other page on the host is listed; the top hit is not repeated.
    int repeats = 0;
    for (auto& s : l.items)
        repeats += s.url == L"https://webkit.org/";
    CHECK(repeats == 1);
    CHECK(findKind(l, SuggestionKind::History) != nullptr);
}

TEST(suggestWwwAndSchemeAreIgnoredWhenTyping)
{
    Fixture f;
    f.visit(L"https://www.wikipedia.org/wiki/Meridian", L"Meridian", 2);
    auto l = f.suggest(L"wiki");
    CHECK_EQ(l.completion, L"pedia.org");
    // Only a deep page is known: the top hit is the site itself, with history's scheme and www.
    CHECK_EQ(l.items[0].url, L"https://www.wikipedia.org/");
    CHECK_EQ(l.items[0].title, L"wikipedia.org");
    CHECK(l.items[0].historyId == 0);
    CHECK_EQ(f.suggest(L"https://wiki").completion, L"pedia.org");
    CHECK_EQ(f.suggest(L"www.wiki").completion, L"pedia.org");
    CHECK_EQ(f.suggest(L"WIKI").completion, L"pedia.org");
}

TEST(suggestCompletesPathOnceSlashTyped)
{
    Fixture f;
    f.visit(L"https://webkit.org/", L"WebKit", 5);
    f.visit(L"https://webkit.org/blog/", L"Blog", 2);
    auto l = f.suggest(L"webkit.org/b");
    CHECK_EQ(l.completion, L"log/");
    CHECK_EQ(l.items[0].url, L"https://webkit.org/blog/");
    CHECK(l.defaultSelection == 0);
}

TEST(suggestExactHostHasNoNavigateDuplicate)
{
    Fixture f;
    f.visit(L"https://webkit.org/", L"WebKit", 2);
    auto l = f.suggest(L"webkit.org");
    CHECK(l.completion.empty());
    CHECK(l.defaultSelection == 0);
    CHECK(l.items.size() == 1);
    CHECK(l.items[0].kind == SuggestionKind::TopHit);
}

TEST(suggestNoCompletionWhileDeleting)
{
    Fixture f;
    f.visit(L"https://webkit.org/", L"WebKit", 2);
    f.options.allowCompletion = false;
    auto l = f.suggest(L"web");
    CHECK(l.completion.empty());
    CHECK(l.defaultSelection == -1); // Enter acts on the typed text
    CHECK(l.items[0].kind == SuggestionKind::TopHit);
}

TEST(suggestNoCompletionAfterSpaceOrForSeveralWords)
{
    Fixture f;
    f.visit(L"https://webkit.org/", L"WebKit Project", 4);
    CHECK(f.suggest(L"webkit ").completion.empty());
    auto l = f.suggest(L"webkit proj");
    CHECK(l.completion.empty());
    CHECK(l.defaultSelection == -1);
    // A well-used page whose title matches still leads.
    CHECK(l.items[0].kind == SuggestionKind::TopHit);
    CHECK(l.items[1].kind == SuggestionKind::Search);
}

TEST(suggestRanksByFrecency)
{
    Fixture f;
    f.visit(L"https://news.example/rare", L"Rare news", 1, 60 * kDay);
    f.visit(L"https://news.example/daily", L"Daily news", 6, kDay);
    f.visit(L"https://other.example/", L"Other news", 2, kDay);
    auto l = f.suggest(L"news story"); // nothing matches both words
    CHECK(l.items.size() == 1);
    l = f.suggest(L"news");
    // "news" completes news.example; the rows below it are best first.
    std::vector<std::wstring> rows;
    for (auto& s : l.items)
        if (s.kind == SuggestionKind::History)
            rows.push_back(s.url);
    CHECK(rows.size() == 3);
    CHECK_EQ(rows[0], L"https://news.example/daily");
    CHECK_EQ(rows[2], L"https://news.example/rare");
}

TEST(suggestBookmarksMergeWithHistory)
{
    Fixture f;
    f.visit(L"https://docs.example/guide", L"Guide", 1);
    f.bookmarks.add(Bookmarks::kBookmarksId, L"Guide (saved)", L"https://docs.example/guide");
    f.bookmarks.add(Bookmarks::kBookmarksId, L"Guide two", L"https://unvisited.example/guide");
    auto l = f.suggest(L"guide");
    int bookmarks = 0, total = 0;
    for (auto& s : l.items) {
        if (s.kind == SuggestionKind::Search)
            continue;
        ++total;
        bookmarks += s.bookmarked;
        if (s.url == L"https://docs.example/guide") {
            CHECK_EQ(s.title, L"Guide"); // history title kept
            CHECK(s.historyId != 0);
        }
    }
    CHECK(total == 2);     // one row per URL
    CHECK(bookmarks == 2);
}

TEST(suggestLimitsRows)
{
    Fixture f;
    for (int i = 0; i < 20; ++i)
        f.visit((L"https://site" + std::to_wstring(i) + L".example/page").c_str(), L"Page");
    f.options.maxMatches = 4;
    auto l = f.suggest(L"page");
    size_t rows = 0;
    for (auto& s : l.items)
        rows += s.kind == SuggestionKind::History;
    CHECK(rows == 4);
}

TEST(searchSuggestionParsing)
{
    auto a = parseSearchSuggestions("[\"web\",[\"webkit\",\"web \\\"quoted\\\"\",\"caf\\u00e9 \\ud83d\\ude00\"]]", 8);
    CHECK(a.size() == 3);
    CHECK_EQ(a[0], L"webkit");
    CHECK_EQ(a[1], L"web \"quoted\"");
    CHECK_EQ(a[2], L"café 😀");
    auto b = parseSearchSuggestions("[{\"phrase\":\"one\"},{\"phrase\" : \"two\",\"other\":\"x\"}]", 8);
    CHECK(b.size() == 2);
    CHECK_EQ(b[1], L"two");
    CHECK(parseSearchSuggestions("[\"q\",[\"a\",\"b\",\"c\"]]", 2).size() == 2);
    // Not suggestions, or broken.
    CHECK(parseSearchSuggestions("", 8).empty());
    CHECK(parseSearchSuggestions("<html>nope</html>", 8).empty());
    CHECK(parseSearchSuggestions("{\"error\":\"x\"}", 8).empty());
    CHECK(parseSearchSuggestions("[\"q\",[\"unterminated", 8).empty());
    CHECK(parseSearchSuggestions("]]]][\"q\",[\"a\"]]", 8).empty());
    CHECK(parseSearchSuggestions("[\"q\",[\"bad \\u12\"]]", 8).empty());
}
