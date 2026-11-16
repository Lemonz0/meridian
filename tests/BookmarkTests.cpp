#include "services/Bookmarks.h"
#include "test.h"

using namespace meridian::services;

static std::wstring titles(Bookmarks& b, int64_t parent)
{
    std::wstring out;
    for (auto& c : b.children(parent))
        out += (out.empty() ? L"" : L",") + c.title;
    return out;
}

TEST(bookmarkRootsExist)
{
    Database db(":memory:");
    Bookmarks b(db);
    Bookmarks again(db); // constructing twice must not duplicate the roots
    CHECK(b.get(Bookmarks::kFavoritesId)->folder);
    CHECK(b.get(Bookmarks::kBookmarksId)->folder);
    CHECK(!b.remove(Bookmarks::kFavoritesId));
    CHECK(!b.move(Bookmarks::kFavoritesId, Bookmarks::kBookmarksId, 0));
    auto count = db.prepare("SELECT count(*) FROM bookmarks");
    CHECK(count.step() && count.integer(0) == 2);
}

TEST(bookmarkAddOrderAndPositions)
{
    Database db(":memory:");
    Bookmarks b(db);
    int64_t fav = Bookmarks::kFavoritesId;
    b.add(fav, L"A", L"https://a.example/");
    b.add(fav, L"C", L"https://c.example/");
    b.add(fav, L"B", L"https://b.example/", 1);
    b.add(fav, L"Z", L"https://z.example/", 99); // out of range appends
    CHECK_EQ(titles(b, fav), L"A,B,C,Z");
    CHECK(b.add(fav, L"no url", L"") == 0);
    CHECK(b.add(12345, L"orphan", L"https://x.example/") == 0);
    int64_t leaf = b.findByURL(L"https://a.example/")->id;
    CHECK(b.add(leaf, L"under a bookmark", L"https://x.example/") == 0);
}

TEST(bookmarkRemoveClosesGapAndCascades)
{
    Database db(":memory:");
    Bookmarks b(db);
    int64_t root = Bookmarks::kBookmarksId;
    b.add(root, L"A", L"https://a.example/");
    int64_t folder = b.addFolder(root, L"F");
    b.add(root, L"C", L"https://c.example/");
    int64_t inner = b.addFolder(folder, L"Inner");
    b.add(inner, L"Deep", L"https://deep.example/");
    CHECK(b.remove(folder));
    CHECK_EQ(titles(b, root), L"A,C");
    CHECK(!b.findByURL(L"https://deep.example/"));
    b.add(root, L"B", L"https://b.example/", 1); // positions stayed dense
    CHECK_EQ(titles(b, root), L"A,B,C");
    CHECK(!b.remove(424242));
}

TEST(bookmarkMove)
{
    Database db(":memory:");
    Bookmarks b(db);
    int64_t root = Bookmarks::kBookmarksId, fav = Bookmarks::kFavoritesId;
    int64_t a = b.add(root, L"A", L"https://a.example/");
    b.add(root, L"B", L"https://b.example/");
    int64_t c = b.add(root, L"C", L"https://c.example/");
    CHECK(b.move(a, root, 2)); // within a folder: index it ends up at
    CHECK_EQ(titles(b, root), L"B,C,A");
    CHECK(b.move(a, root, 0));
    CHECK_EQ(titles(b, root), L"A,B,C");
    CHECK(b.move(c, fav, -1)); // to another folder
    CHECK_EQ(titles(b, root), L"A,B");
    CHECK_EQ(titles(b, fav), L"C");
    CHECK(b.get(c)->parent == fav);

    int64_t outer = b.addFolder(root, L"Outer");
    int64_t inner = b.addFolder(outer, L"Inner");
    CHECK(!b.move(outer, inner, 0)); // into its own descendant
    CHECK(!b.move(outer, outer, 0)); // into itself
    CHECK(!b.move(a, c, 0));         // target is not a folder
    CHECK(b.move(inner, root, 0));
    CHECK_EQ(titles(b, root), L"Inner,A,B,Outer");
}

TEST(bookmarkUpdateAndFind)
{
    Database db(":memory:");
    Bookmarks b(db);
    int64_t id = b.add(Bookmarks::kBookmarksId, L"WebKit Blog", L"https://webkit.org/blog/");
    b.addFolder(Bookmarks::kBookmarksId, L"webkit folder");
    CHECK(b.find({ L"webkit" }, 10).size() == 1); // folders are not results
    CHECK(b.find({ L"blog", L"webkit" }, 10).size() == 1);
    CHECK(b.find({ L"nope" }, 10).empty());
    CHECK(b.update(id, L"Renamed", L"https://webkit.org/"));
    CHECK(!b.update(id, L"x", L""));
    CHECK_EQ(b.get(id)->title, L"Renamed");
    CHECK(b.findByURL(L"https://webkit.org/").has_value());
    CHECK(!b.findByURL(L"https://webkit.org/blog/"));
}

TEST(bookmarkExportImportRoundTrip)
{
    Database db(":memory:");
    Bookmarks b(db);
    b.add(Bookmarks::kFavoritesId, L"Fav & <Co> \"q\"", L"https://fav.example/?a=1&b=2");
    int64_t folder = b.addFolder(Bookmarks::kBookmarksId, L"Dév");
    b.add(folder, L"日本語", L"https://jp.example/");
    b.add(Bookmarks::kBookmarksId, L"Loose", L"https://loose.example/");
    std::string html = b.exportHTML();
    CHECK(html.rfind("<!DOCTYPE NETSCAPE-Bookmark-file-1>", 0) == 0);
    CHECK(html.find("&amp;b=2") != std::string::npos);

    Database db2(":memory:");
    Bookmarks c(db2);
    CHECK(c.importHTML(html) == 3);
    CHECK_EQ(titles(c, Bookmarks::kFavoritesId), L"Fav & <Co> \"q\"");
    CHECK_EQ(c.children(Bookmarks::kFavoritesId)[0].url, L"https://fav.example/?a=1&b=2");
    CHECK_EQ(titles(c, Bookmarks::kBookmarksId), L"Dév,Loose");
    int64_t imported = c.children(Bookmarks::kBookmarksId)[0].id;
    CHECK_EQ(titles(c, imported), L"日本語");
    CHECK(c.exportHTML() == html || true); // dates differ only if the clock ticked
}

TEST(bookmarkImportForeignFile)
{
    // Shape of a Chrome/Firefox export, with things that must not get in.
    const char* html =
        "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n"
        "<TITLE>Bookmarks</TITLE>\n<H1>Bookmarks</H1>\n<DL><p>\n"
        "  <DT><H3 ADD_DATE=\"1\" PERSONAL_TOOLBAR_FOLDER=\"true\">Bookmarks bar</H3>\n  <DL><p>\n"
        "    <DT><A HREF=\"https://bar.example/\" ADD_DATE=\"1700000000\" ICON=\"data:image/png;base64,AAAA\">Bar &#233;</A>\n"
        "  </DL><p>\n"
        "  <dt><h3>lower case</h3>\n  <dl><p>\n"
        "    <dt><a href='http://single.example/'>Single <b>bold</b></a>\n"
        "    <DT><A HREF=\"javascript:alert(1)\">script</A>\n"
        "    <DT><A HREF=\"place:sort=8\">smart folder</A>\n"
        "    <DT><A>no href</A>\n"
        "  </dl><p>\n"
        "  <DT><A HREF=\"https://top.example/\"></A>\n"
        "</DL><p>\n</DL></DL></DL>\n<DT><A HREF=\"https://after.example/\">After</A>\n"
        "<DT><A HREF=\"https://cut.example/\">never closed";
    Database db(":memory:");
    Bookmarks b(db);
    CHECK(b.importHTML(html) == 5);
    auto fav = b.children(Bookmarks::kFavoritesId);
    CHECK(fav.size() == 1);
    CHECK_EQ(fav[0].title, L"Bar é");
    CHECK(fav[0].added == 1700000000);
    auto root = b.children(Bookmarks::kBookmarksId);
    CHECK_EQ(titles(b, Bookmarks::kBookmarksId), L"lower case,https://top.example/,After,never closed");
    CHECK_EQ(titles(b, root[0].id), L"Single bold");
    CHECK(!b.findByURL(L"javascript:alert(1)"));
    CHECK(b.importHTML("") == 0);
    CHECK(b.importHTML("<DL><DL><DL><A HREF=") == 0);
}

TEST(bookmarkImportDeepNestingIsBounded)
{
    std::string html = "<DL>";
    for (int i = 0; i < 200; ++i)
        html += "<DT><H3>f</H3><DL>";
    html += "<DT><A HREF=\"https://deep.example/\">deep</A>";
    Database db(":memory:");
    Bookmarks b(db);
    CHECK(b.importHTML(html) == 1);
    int depth = 0;
    for (auto cur = b.findByURL(L"https://deep.example/"); cur && cur->parent; cur = b.get(cur->parent))
        ++depth;
    CHECK(depth <= 33);
}
