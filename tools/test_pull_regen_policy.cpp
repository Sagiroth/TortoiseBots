#include "../ai/playerbot/PullRegenPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::AllowPreemptiveStrike;
using ai::PullGrindLevelCap;
using ai::PullLevelWithinCap;
using ai::ShouldDeferGrindPull;

namespace
{
    // Live config defaults (PlayerbotAIConfig.cpp): MediumHealth 70,
    // MediumMana 40.
    std::uint32_t const MEDIUM_HEALTH = 70;
    std::uint32_t const MEDIUM_MANA = 40;
}

int main()
{
    std::cout << "Starting TortoiseBots pull-regen gate tests...\n";

    // -------------------------------------------------------------
    // (1) New-pull regen gate: wounded bots sit out the next pull.
    // -------------------------------------------------------------
    {
        // Full health and mana: pull.
        CHECK(!ShouldDeferGrindPull(100, true, 100, MEDIUM_HEALTH, MEDIUM_MANA));
        // Exactly at the thresholds: ready, not wounded.
        CHECK(!ShouldDeferGrindPull(70, true, 40, MEDIUM_HEALTH, MEDIUM_MANA));
        // Just below the health line: sit out, even with full mana.
        CHECK(ShouldDeferGrindPull(69, true, 100, MEDIUM_HEALTH, MEDIUM_MANA));
        // A fresh corpse-run ghost at 40%: the chain-pull shape from
        // m-deaths finding 1 (33% of deaths <= 60 s after the last kill).
        CHECK(ShouldDeferGrindPull(40, true, 90, MEDIUM_HEALTH, MEDIUM_MANA));
        std::cout << "  [PASS] health below medium defers the pull\n";
    }

    // Mana side: only mana users are gated, and only below medium mana.
    {
        // OOM caster: sit out even at full health.
        CHECK(ShouldDeferGrindPull(100, true, 0, MEDIUM_HEALTH, MEDIUM_MANA));
        CHECK(ShouldDeferGrindPull(100, true, 39, MEDIUM_HEALTH, MEDIUM_MANA));
        // A warrior (no mana bar) at full health: pull.
        CHECK(!ShouldDeferGrindPull(100, false, 0, MEDIUM_HEALTH, MEDIUM_MANA));
        // Rogue energy bundles as no-mana here: health alone decides.
        CHECK(ShouldDeferGrindPull(50, false, 0, MEDIUM_HEALTH, MEDIUM_MANA));
        std::cout << "  [PASS] mana users below medium mana defer, others do not\n";
    }

    // -------------------------------------------------------------
    // (2) Grind level cap: +1 below 10, +4 from 10 (solo grind only).
    // -------------------------------------------------------------
    {
        // A level-6 bot: +1 is fair, +2 is not.
        CHECK(PullGrindLevelCap(6, false) == 1);
        CHECK(PullLevelWithinCap(7, 6, false));
        CHECK(!PullLevelWithinCap(8, 6, false));
        // From level 10 the long-standing +4 ceiling stands.
        CHECK(PullGrindLevelCap(10, false) == 4);
        CHECK(PullLevelWithinCap(14, 10, false));
        CHECK(!PullLevelWithinCap(15, 10, false));
        // A bot following a real player is told what to fight: no cap.
        CHECK(PullGrindLevelCap(6, true) == 4);
        CHECK(PullLevelWithinCap(10, 6, true));
        std::cout << "  [PASS] grind level cap is +1 below 10, +4 above\n";
    }

    // -------------------------------------------------------------
    // Pre-emptive strike: no adds and inside the cap, or not at all.
    // -------------------------------------------------------------
    {
        // Lone even-level mob in front: strike.
        CHECK(AllowPreemptiveStrike(6, 6, false, false));
        // Same mob with possible adds nearby: walk on, not in.
        CHECK(!AllowPreemptiveStrike(6, 6, false, true));
        // Alone but over the cap (37% of sub-10 deaths were by mobs above
        // the +1 cap, m-deaths finding 3): no strike.
        CHECK(!AllowPreemptiveStrike(8, 6, false, false));
        CHECK(AllowPreemptiveStrike(8, 6, false, false) == PullLevelWithinCap(8, 6, false));
        // From 10 the cap is +4, so a +2 lone mob still strikes.
        CHECK(AllowPreemptiveStrike(12, 10, false, false));
        CHECK(!AllowPreemptiveStrike(12, 10, false, true));
        std::cout << "  [PASS] pre-emptive strike needs no adds and the cap\n";
    }

    std::cout << "All pull-regen gate checks PASSED!\n";
    return 0;
}
