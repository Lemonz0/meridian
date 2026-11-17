#include "services/Reader.h"
#include "test.h"

using namespace meridian::services;

TEST(readerResultParsing)
{
    auto r = parseReaderResult(L"1\x1fTitle\x1f" L"Excerpt\x1f" L"Ada\x1fSite\x1f" L"0");
    CHECK(r.available && !r.shown);
    CHECK_EQ(r.title, L"Title");
    CHECK_EQ(r.excerpt, L"Excerpt");
    CHECK_EQ(r.byline, L"Ada");
    CHECK_EQ(r.siteName, L"Site");
    auto empty = parseReaderResult(L"0\x1f\x1f\x1f\x1f\x1f" L"1");
    CHECK(!empty.available && empty.shown && empty.title.empty());
    // Anything else (page returned something odd, script failed) is "not available".
    CHECK(!parseReaderResult(L"").available);
    CHECK(!parseReaderResult(L"1").available);
    CHECK(!parseReaderResult(L"1\x1fonly two").available);
}

TEST(readerStyleJsonAndSteps)
{
    ReaderStyle s;
    CHECK_EQ(s.toJson(), L"{\"theme\":\"auto\",\"font\":\"sans\",\"size\":100}");
    s.theme = ReaderTheme::Sepia;
    s.serif = true;
    s.sizePercent = 245;
    CHECK_EQ(s.larger().toJson(), L"{\"theme\":\"sepia\",\"font\":\"serif\",\"size\":250}");
    ReaderStyle small;
    small.sizePercent = 65;
    CHECK(small.smaller().sizePercent == 60);
    CHECK(small.smaller().smaller().sizePercent == 60);
    small.sizePercent = 9999; // never emitted out of range
    CHECK_EQ(small.toJson(), L"{\"theme\":\"auto\",\"font\":\"sans\",\"size\":250}");
}

TEST(readerStylePersists)
{
    Database db(":memory:");
    Settings settings(db);
    ReaderStyle defaults = loadReaderStyle(settings);
    CHECK(defaults.theme == ReaderTheme::Automatic && !defaults.serif && defaults.sizePercent == 100);
    ReaderStyle s;
    s.theme = ReaderTheme::Dark;
    s.serif = true;
    s.sizePercent = 130;
    saveReaderStyle(settings, s);
    ReaderStyle back = loadReaderStyle(settings);
    CHECK(back.theme == ReaderTheme::Dark && back.serif && back.sizePercent == 130);
    settings.set(L"reader.size", L"nonsense");
    CHECK(loadReaderStyle(settings).sizePercent == ReaderStyle::kMinSize);
    settings.set(L"reader.theme", L"neon");
    CHECK(loadReaderStyle(settings).theme == ReaderTheme::Automatic);
}

TEST(jsStringLiteralEscapes)
{
    CHECK_EQ(jsStringLiteral(L"plain"), L"\"plain\"");
    CHECK_EQ(jsStringLiteral(L"a\"b\\c"), L"\"a\\\"b\\\\c\"");
    CHECK_EQ(jsStringLiteral(L"x\ny"), L"\"x\\ny\"");
    CHECK_EQ(jsStringLiteral(L"</script>"), L"\"\\u003c/script>\"");
    CHECK_EQ(jsStringLiteral(std::wstring(1, wchar_t(0x2028))), L"\"\\u2028\"");
    CHECK_EQ(jsStringLiteral(std::wstring(1, wchar_t(1))), L"\"\\u0001\"");
    CHECK_EQ(jsStringLiteral(L"é日本"), L"\"é日本\"");
}
