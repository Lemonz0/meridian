#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace meridian::test {
struct Case { const char* name; std::function<void()> fn; };
std::vector<Case>& registry();
extern int failures;
struct Registrar { Registrar(const char* n, std::function<void()> f) { registry().push_back({ n, std::move(f) }); } };
}

#define TEST(name) \
    static void name(); \
    static meridian::test::Registrar name##_reg(#name, name); \
    static void name()

#define CHECK_EQ(a, b) \
    do { \
        auto _a = (a); auto _b = (b); \
        if (!(_a == _b)) { \
            ++meridian::test::failures; \
            std::fwprintf(stderr, L"  %hs:%d CHECK_EQ failed: got '%ls' expected '%ls'\n", __FILE__, __LINE__, \
                std::wstring(_a).c_str(), std::wstring(_b).c_str()); \
        } \
    } while (0)

#define CHECK(x) \
    do { if (!(x)) { ++meridian::test::failures; std::fprintf(stderr, "  %s:%d CHECK failed: %s\n", __FILE__, __LINE__, #x); } } while (0)
