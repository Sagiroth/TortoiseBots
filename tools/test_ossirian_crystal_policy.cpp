#include "../ai/playerbot/policy/raid/OssirianCrystalPolicy.h"

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

int UpstreamOssirianCrystalPolicyTest()
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

#include "../ai/playerbot/policy/raid/OssirianCrystalPolicy.h"
#include <cassert>
#include <limits>
int main()
{
    if (UpstreamOssirianCrystalPolicyTest()) return 1;
    using namespace ai;
    assert(OssirianWeaknessDue(true,true,40000));
    assert(OssirianWeaknessDue(false,false,0));
    assert(OssirianWeaknessDue(false,true,6000));
    assert(!OssirianWeaknessDue(false,true,6001));
    assert(!OssirianWeaknessDue(false,true,-1));
    assert(OssirianCrystalInRange(25));
    assert(!OssirianCrystalInRange(25.1f));
    assert(!OssirianCrystalInRange(std::numeric_limits<float>::quiet_NaN()));
    OssirianClickWindow w;
    assert(!w.Claim(100,2,1,0));
    assert(w.Claim(100,1,1,0));
    assert(!w.Claim(101,2,2,1)); // different crystal still shares the boss claim
    assert(!w.Ready(100,7999));
    assert(w.Claim(100,2,2,8000)); // replacement may take the bounded retry
    assert(!w.Ready(100,16000));
    assert(w.Claim(101,2,2,16000));
    OssirianClickWindow wrap;
    assert(wrap.Claim(100,1,1,0xfffffff0u));
    assert(!wrap.Ready(101,10));
    assert(wrap.Ready(101,8000));
}
