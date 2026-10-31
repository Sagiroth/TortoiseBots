// Standalone check for ai/playerbot/policy/combat/RaidSnarePolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_raid_snare_policy.cpp -o /tmp/test_raid_snare && /tmp/test_raid_snare
#include "../ai/playerbot/policy/combat/RaidSnarePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)
using namespace ai;

int main()
{
    std::cout << "Starting TortoiseBots raid snare policy tests...\n";

    // Outside a raid group nothing changes: any victim is still snared.
    CHECK(SnareAllowedInRaid(false, true, false));
    CHECK(SnareAllowedInRaid(false, false, false));
    // Fleeing/assist mobs are never a positioning problem: always allowed.
    CHECK(SnareAllowedInRaid(true, true, true));
    CHECK(SnareAllowedInRaid(true, false, true));
    // Raid group: only snare a mob attacking someone who is not a tank,
    // however the tank was identified (IsTank read or raid Main Tank flag).
    CHECK(SnareAllowedInRaid(true, false, false));
    CHECK(!SnareAllowedInRaid(true, true, false));
    std::cout << "  [PASS] raid snares only non-tank victims\n";

    // Piercing Howl: the trigger picks any mob within spell range (30 yd),
    // but the Howl is a 10-yd AoE around the warrior cast at the melee
    // target. A unit it cannot reach must not arm it (walk-15: a mob
    // chasing a healer 25 yd out made the warrior Howl the tank's mob).
    CHECK(!PiercingHowlAllowed(true, 25, true, false));
    // Raid: only a fleeing runner beside us is a Howl target; a mob chasing
    // a healer inside the radius is the off-tank's job.
    CHECK(!PiercingHowlAllowed(true, 6, true, false));
    CHECK(PiercingHowlAllowed(true, 6, true, true));
    CHECK(!PiercingHowlAllowed(true, 12, true, true));
    // Dungeon/party: a non-fleeing snare inside the radius is allowed.
    CHECK(PiercingHowlAllowed(true, 6, false, false));
    CHECK(!PiercingHowlAllowed(true, 15, false, false));
    CHECK(!PiercingHowlAllowed(false, 0, false, false));
    CHECK(!PiercingHowlAllowed(false, 0, true, true));
    // Boundary: the radius is inclusive (10.0 allowed, 10.1 refused).
    CHECK(PiercingHowlAllowed(true, PIERCING_HOWL_RADIUS_YD, false, false));
    CHECK(PiercingHowlAllowed(true, 10.0f, false, false));
    CHECK(!PiercingHowlAllowed(true, 10.1f, false, false));
    CHECK(PIERCING_HOWL_RADIUS_YD == 10.0f);
    std::cout << "  [PASS] piercing howl only fires on a snare unit it can reach\n";

    // Howl gate: a refused Howl has no useful alternative in a raid (Arms
    // and Fury fall back to Hamstring, a 10-rage snare walk up to 30 yd off
    // the melee target), so it must be useless there and push nothing. In a
    // dungeon the Hamstring fallback stays.
    CHECK(PiercingHowlGate(true, 25, true, false) == HowlGate::Skip);
    CHECK(PiercingHowlGate(true, 6, true, true) == HowlGate::Cast);
    CHECK(PiercingHowlGate(true, 6, true, false) == HowlGate::Skip);
    CHECK(PiercingHowlGate(true, 15, false, false) == HowlGate::FallBack);
    CHECK(PiercingHowlGate(false, 0, false, false) == HowlGate::FallBack);
    CHECK(PiercingHowlGate(false, 0, true, false) == HowlGate::Skip);
    CHECK(PiercingHowlGate(true, 10.0f, false, false) == HowlGate::Cast);
    std::cout << "  [PASS] a refused howl skips the raid fallback\n";

    std::cout << "All TortoiseBots raid snare policy tests passed.\n";
    return 0;
}
