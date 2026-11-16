// Tiny dependency-free test runner.
#include "test.h"
#include <cstdio>

std::vector<meridian::test::Case>& meridian::test::registry()
{
    static std::vector<Case> cases;
    return cases;
}
int meridian::test::failures = 0;

int main()
{
    for (auto& c : meridian::test::registry()) {
        int before = meridian::test::failures;
        c.fn();
        std::printf("[%s] %s\n", meridian::test::failures == before ? "PASS" : "FAIL", c.name);
    }
    std::printf("%zu cases, %d failed checks\n", meridian::test::registry().size(), meridian::test::failures);
    return meridian::test::failures ? 1 : 0;
}
