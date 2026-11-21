// Pure decision rules for opt-in Holy Paladin melee-weaving
// (AiPlayerbot.HolyPaladinMeleeWeave): when a Holy Paladin may walk into
// melee and stay there, and Holy Strike vs Crusader Strike fallback selection.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_holy_melee_weave_policy.cpp -o /tmp/test_holy_melee_weave
//   /tmp/test_holy_melee_weave

#include "../runtime/policy/class/HolyMeleeWeavePolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

#define CHECK(x) do { \
    if (!(x)) { \
        std::fprintf(stderr, "Assertion failed at line %d: %s\n", __LINE__, #x); \
        std::exit(1); \
    } \
} while (0)

static HolyMeleeWeaveInput BaseInput()
{
    HolyMeleeWeaveInput in;
    in.enabled = true;
    in.isHolyPaladin = true;
    in.inGroup = true;
    in.hasTarget = true;
    in.targetAlive = true;
    in.targetIsPlayer = false;
    in.targetIsRaidBoss = false;
    in.targetEngaged = true;
    in.selfHealthPct = 100;
    in.partyMemberToHealPct = 95;
    in.mediumHealthPct = 70;
    return in;
}

static void TestWeaveAllowed()
{
    CHECK(HolyMeleeWeaveAllowed(BaseInput()));

    // Toggle off -> never allowed.
    {
        auto in = BaseInput();
        in.enabled = false;
        CHECK(!HolyMeleeWeaveAllowed(in));
    }

    // Solo -> never allowed.
    {
        auto in = BaseInput();
        in.inGroup = false;
        CHECK(!HolyMeleeWeaveAllowed(in));
    }

    // Non-holy or non-paladin -> never allowed.
    {
        auto in = BaseInput();
        in.isHolyPaladin = false;
        CHECK(!HolyMeleeWeaveAllowed(in));
    }

    // Raid boss or PvP player or unengaged/dead/missing target -> never allowed.
    {
        auto in = BaseInput();
        in.targetIsRaidBoss = true;
        CHECK(!HolyMeleeWeaveAllowed(in));
    }
    {
        auto in = BaseInput();
        in.targetIsPlayer = true;
        CHECK(!HolyMeleeWeaveAllowed(in));
    }
    {
        auto in = BaseInput();
        in.targetEngaged = false;
        CHECK(!HolyMeleeWeaveAllowed(in));
    }
    {
        auto in = BaseInput();
        in.targetAlive = false;
        CHECK(!HolyMeleeWeaveAllowed(in));
    }
    {
        auto in = BaseInput();
        in.hasTarget = false;
        CHECK(!HolyMeleeWeaveAllowed(in));
    }

    // Light-heal band (>= mediumHealthPct) allows melee-weave; medium/low heal
    // (< mediumHealthPct) on self or party member suspends it.
    {
        auto in = BaseInput();
        in.partyMemberToHealPct = 70;
        CHECK(HolyMeleeWeaveAllowed(in));
        in.partyMemberToHealPct = 69;
        CHECK(!HolyMeleeWeaveAllowed(in));
    }
    {
        auto in = BaseInput();
        in.selfHealthPct = 70;
        CHECK(HolyMeleeWeaveAllowed(in));
        in.selfHealthPct = 69;
        CHECK(!HolyMeleeWeaveAllowed(in));
    }
}

static void TestStrikePick()
{
    // Toggle off keeps old Crusader-Strike-only behaviour.
    CHECK(PickHolyMeleeStrike(false, true, true) == HolyMeleeStrikePick::CrusaderStrikeFallback);
    CHECK(PickHolyMeleeStrike(false, true, false) == HolyMeleeStrikePick::None);

    // Toggle on prefers Holy Strike; Crusader Strike is fallback only when Holy Strike is unknown.
    CHECK(PickHolyMeleeStrike(true, true, true) == HolyMeleeStrikePick::HolyStrike);
    CHECK(PickHolyMeleeStrike(true, true, false) == HolyMeleeStrikePick::HolyStrike);
    CHECK(PickHolyMeleeStrike(true, false, true) == HolyMeleeStrikePick::CrusaderStrikeFallback);
    CHECK(PickHolyMeleeStrike(true, false, false) == HolyMeleeStrikePick::None);
}

int main()
{
    TestWeaveAllowed();
    TestStrikePick();
    std::printf("test_holy_melee_weave_policy: all assertions passed\n");
    return 0;
}
