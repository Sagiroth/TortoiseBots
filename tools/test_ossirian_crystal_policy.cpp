#include "../ai/playerbot/OssirianCrystalPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::ShouldRunToOssirianCrystal;
using ai::ShouldUseOssirianCrystal;

int main()
{
    std::cout << "Starting TortoiseBots Ossirian-crystal policy tests...\n";

    // No run outside combat, even with the buff up.
    CHECK(!ShouldRunToOssirianCrystal(false, true, 60000, 1000));
    // Buff up in combat: run now.
    CHECK(ShouldRunToOssirianCrystal(true, true, 60000, 20000));
    // Debuff nearly out (<5s): run now.
    CHECK(ShouldRunToOssirianCrystal(true, false, 4000, 20000));
    // Inside the 30s window: run when remaining minus the 5s arm lead is
    // under the run time.
    CHECK(ShouldRunToOssirianCrystal(true, false, 20000, 16000));
    CHECK(!ShouldRunToOssirianCrystal(true, false, 20000, 5000));
    // Outside the window: hold.
    CHECK(!ShouldRunToOssirianCrystal(true, false, 45000, 1000));
    std::cout << "  [PASS] crystal-run timing matches donor windows\n";

    // Use guards: boss range and in-use first.
    CHECK(!ShouldUseOssirianCrystal(30.0f, false, true, 0));
    CHECK(!ShouldUseOssirianCrystal(10.0f, true, true, 0));
    CHECK(ShouldUseOssirianCrystal(10.0f, false, true, 60000));
    CHECK(ShouldUseOssirianCrystal(10.0f, false, false, 4000));
    CHECK(!ShouldUseOssirianCrystal(10.0f, false, false, 20000));
    std::cout << "  [PASS] crystal-use guards match donor checks\n";

    std::cout << "All Ossirian-crystal policy tests passed.\n";
    return 0;
}
