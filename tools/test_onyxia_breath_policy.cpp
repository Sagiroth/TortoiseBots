#include "../ai/playerbot/OnyxiaBreathPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::BreathAxisFromFacing;
using ai::BreathAxisIndex;
using ai::IsOnyxiaBreathSpell;
using ai::kBreathSafeZoneRadius;
using ai::kOnyxiaEntry;
using ai::ShouldMoveToBreathSafeZone;

int main()
{
    std::cout << "Starting TortoiseBots onyxia-breath policy tests...\n";

    CHECK(kOnyxiaEntry == 10184);
    CHECK(kBreathSafeZoneRadius == 5.0f);
    std::cout << "  [PASS] entry and radius pinned\n";

    // All 8 breath ids recognized; neighbors rejected.
    CHECK(IsOnyxiaBreathSpell(17086));
    CHECK(IsOnyxiaBreathSpell(18351));
    CHECK(IsOnyxiaBreathSpell(18576));
    CHECK(IsOnyxiaBreathSpell(18609));
    CHECK(IsOnyxiaBreathSpell(18564));
    CHECK(IsOnyxiaBreathSpell(18584));
    CHECK(IsOnyxiaBreathSpell(18596));
    CHECK(IsOnyxiaBreathSpell(18617));
    CHECK(!IsOnyxiaBreathSpell(18392));
    CHECK(!IsOnyxiaBreathSpell(0));
    std::cout << "  [PASS] all 8 breath ids recognized\n";

    // Axis pairing: opposite directions share safe zones.
    CHECK(BreathAxisIndex(17086) == 0);
    CHECK(BreathAxisIndex(18351) == 0);
    CHECK(BreathAxisIndex(18576) == 1);
    CHECK(BreathAxisIndex(18609) == 1);
    CHECK(BreathAxisIndex(18564) == 2);
    CHECK(BreathAxisIndex(18584) == 2);
    CHECK(BreathAxisIndex(18596) == 3);
    CHECK(BreathAxisIndex(18617) == 3);
    CHECK(BreathAxisIndex(18392) == -1);
    std::cout << "  [PASS] axis pairing correct\n";

    // Facing maps to lane axes (N=0, E=PI/2→1, SE→2, SW→3).
    CHECK(BreathAxisFromFacing(0.0f) == 0);
    CHECK(BreathAxisFromFacing(3.1415926536f) == 0);
    CHECK(BreathAxisFromFacing(1.5707963268f) == 1);
    CHECK(BreathAxisFromFacing(4.7123889804f) == 1);
    CHECK(BreathAxisFromFacing(2.3561944902f) == 2);
    CHECK(BreathAxisFromFacing(5.4977871438f) == 2);
    CHECK(BreathAxisFromFacing(3.9269908170f) == 3);
    CHECK(BreathAxisFromFacing(0.7853981634f) == 3);
    CHECK(BreathAxisFromFacing(6.2831853072f) == 0);
    std::cout << "  [PASS] facing-to-axis mapping\n";

    // Move only while breath casts and not already safe.
    CHECK(ShouldMoveToBreathSafeZone(true, false));
    CHECK(!ShouldMoveToBreathSafeZone(true, true));
    CHECK(!ShouldMoveToBreathSafeZone(false, false));
    std::cout << "  [PASS] move/hold rule\n";

    std::cout << "All onyxia-breath policy tests passed.\n";
    return 0;
}
