#include "services/UrlFixup.h"
#include "test.h"

using namespace meridian::services;
static const std::wstring kSearch = L"https://search.example/?q=";

TEST(bareHostGetsHttps)
{
    CHECK_EQ(fixupInput(L"webkit.org", kSearch).url, L"https://webkit.org");
    CHECK_EQ(fixupInput(L"  example.com/path?x=1 ", kSearch).url, L"https://example.com/path?x=1");
}

TEST(explicitSchemeKept)
{
    CHECK_EQ(fixupInput(L"http://neverssl.com", kSearch).url, L"http://neverssl.com");
    CHECK_EQ(fixupInput(L"about:blank", kSearch).url, L"about:blank");
    CHECK_EQ(fixupInput(L"file:///C:/x.html", kSearch).url, L"file:///C:/x.html");
}

TEST(localhostUsesHttp)
{
    CHECK_EQ(fixupInput(L"localhost:8080", kSearch).url, L"http://localhost:8080");
    CHECK_EQ(fixupInput(L"127.0.0.1:3000/a", kSearch).url, L"http://127.0.0.1:3000/a");
}

TEST(textBecomesSearch)
{
    auto r = fixupInput(L"what is webkit", kSearch);
    CHECK(r.isSearch);
    CHECK_EQ(r.url, kSearch + L"what+is+webkit");
    CHECK(fixupInput(L"hello", kSearch).isSearch);
    CHECK(fixupInput(L"version 1.2", kSearch).isSearch);
    CHECK_EQ(fixupInput(L"c++ & rust", kSearch).url, kSearch + L"c%2B%2B+%26+rust");
}

TEST(unicodeSearchIsUtf8Encoded)
{
    CHECK_EQ(fixupInput(L"caf\u00e9", kSearch).url, kSearch + L"caf%C3%A9");
}

TEST(emptyInput)
{
    CHECK(fixupInput(L"   ", kSearch).url.empty());
}

TEST(displayHostStripsWww)
{
    CHECK_EQ(displayHost(L"https://www.apple.com/mac/"), L"apple.com");
    CHECK_EQ(displayHost(L"https://user:pw@sub.example.org:8443/x"), L"sub.example.org:8443");
    CHECK_EQ(displayHost(L"about:blank"), L"about:blank");
}

TEST(secureDetection)
{
    CHECK(isSecureURL(L"https://a.b"));
    CHECK(!isSecureURL(L"http://a.b"));
}
