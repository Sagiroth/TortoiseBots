#include "../ai/playerbot/DpsTargetPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::CasterTargetBucket;
using ai::GeneralTargetBucket;

int main()
{
    std::cout << "Starting TortoiseBots dps-target bucket tests...\n";

    // -------------------------------------------------------------
    // (1) Caster buckets: preferred window in range wins.
    // -------------------------------------------------------------
    {
        // 5-30 s in range beats everything.
        CHECK(CasterTargetBucket(10.0f, true) > CasterTargetBucket(2.0f, true));
        CHECK(CasterTargetBucket(10.0f, true) > CasterTargetBucket(60.0f, true));
        CHECK(CasterTargetBucket(10.0f, true) > CasterTargetBucket(10.0f, false));
        // Low/out-of-window in range still beats out-of-range preferred.
        CHECK(CasterTargetBucket(2.0f, true) > CasterTargetBucket(10.0f, false));
        CHECK(CasterTargetBucket(60.0f, true) > CasterTargetBucket(10.0f, false));
        // Window edges inclusive.
        CHECK(CasterTargetBucket(5.0f, true) == CasterTargetBucket(30.0f, true));
        CHECK(CasterTargetBucket(5.0f, true) > CasterTargetBucket(4.9f, true));
        CHECK(CasterTargetBucket(30.0f, true) > CasterTargetBucket(30.1f, true));
        std::cout << "  [PASS] caster prefers 5-30 s in range\n";
    }

    // -------------------------------------------------------------
    // (2) General buckets: range decides, lifetime orders within.
    // -------------------------------------------------------------
    {
        CHECK(GeneralTargetBucket(true) > GeneralTargetBucket(false));
        CHECK(GeneralTargetBucket(true) == GeneralTargetBucket(true));
        std::cout << "  [PASS] in-range beats out-of-range\n";
    }

    std::cout << "All dps-target bucket checks PASSED!\n";
    return 0;
}
