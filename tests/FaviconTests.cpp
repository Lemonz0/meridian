#include "services/Favicons.h"
#include "test.h"

#include <windows.h>

using namespace meridian::services;

// 2x2 PNG: red, green / blue, transparent.
static const char* kPng2x2 =
    "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFklEQVR4nGP4z8DwHwyBNMMfBgYGAEbSB/m4Yr8lAAAAAElFTkSuQmCC";

TEST(dataUrlDecodes)
{
    std::wstring url = L"data:image/png;base64,";
    for (const char* p = kPng2x2; *p; ++p)
        url.push_back(wchar_t(*p));
    auto bytes = decodeDataURL(url);
    CHECK(bytes.has_value());
    CHECK(bytes && bytes->size() > 8 && (*bytes)[1] == 'P' && (*bytes)[2] == 'N' && (*bytes)[3] == 'G');
}

TEST(dataUrlRejectsNonBase64)
{
    CHECK(!decodeDataURL(L"data:image/svg+xml,<svg/>").has_value());
    CHECK(!decodeDataURL(L"https://example.com/x.png").has_value());
}

TEST(decodeScalesToRequestedSize)
{
    std::wstring url = L"data:image/png;base64,";
    for (const char* p = kPng2x2; *p; ++p)
        url.push_back(wchar_t(*p));
    auto bytes = decodeDataURL(url);
    CHECK(bytes.has_value());
    if (!bytes)
        return;
    auto bmp = decodeImage(bytes->data(), bytes->size(), 16);
    CHECK(bmp.has_value());
    if (!bmp)
        return;
    CHECK(bmp->width == 16 && bmp->height == 16);
    CHECK(bmp->bgra.size() == 16 * 16 * 4);
}

TEST(decodeRejectsGarbage)
{
    const uint8_t junk[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    CHECK(!decodeImage(junk, sizeof(junk), 16).has_value());
    CHECK(!decodeImage(nullptr, 0, 16).has_value());
}
