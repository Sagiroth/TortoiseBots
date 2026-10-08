#include "../ai/playerbot/GrindSpotPolicy.h"

#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::GetGrindLevelBand;
using ai::GrindLevelBand;
using ai::GrindIdleFallbackAllowed;
using ai::GRIND_IDLE_FALLBACK_RANGE_YD;
using ai::GrindLevelFits;
using ai::GrindPreyAllowed;
using ai::GrindSpotCapacity;
using ai::GrindValleyExempted;
using ai::BeginnerValleyLeashAllows;
using ai::BEGINNER_HOME_LEASH_YD;

namespace
{
    GrindLevelBand Ladder(std::int32_t level, std::uint8_t power = 100) { return GetGrindLevelBand((std::uint32_t)level, power, true); }
    GrindLevelBand Owned(std::int32_t level, std::uint8_t power = 100) { return GetGrindLevelBand((std::uint32_t)level, power, false); }

    // Creature level_max of real entries from the starting zones (tw_world.creature_template).
    std::int32_t const KOBOLD_VERMIN = 2;   // Northshire / Coldridge filler
    std::int32_t const KOBOLD_WORKER = 3;   // the only entry the old level-5 window admitted
    std::int32_t const DEFIAS_THUG = 4;     // Northshire camp
    std::int32_t const DEFIAS_CUTPURSE = 6; // Goldshire fields
    std::int32_t const ICE_CLAW_BEAR = 8;   // Kharanos / Dun Morogh fields
}

int main()
{
    std::cout << "Starting TortoiseBots grind-spot level-band regression tests...\n";

    // -------------------------------------------------------------
    // Test 1: the measured level-5 wall is gone
    // -------------------------------------------------------------
    {
        // Before: level 5 got max = max(5 * 0.7, 5 - 3) = 3 and min = max(5 * 0.6, 0) = 3,
        // i.e. exactly level-3 creatures - green kills (~32 raw XP measured) with no
        // destination in the bot's own starting valley once the zone floor applied.
        GrindLevelBand const old = Owned(5);
        CHECK(old.minLevel == 3 && old.maxLevel == 3);
        std::cout << "  [PASS] owned level-5 window is still the old [3,3]\n";

        // Now: [botLevel - 2, botLevel + 1] -> level-5 bots can walk to the
        // Goldshire/wolf fields (level-5/6 mobs) instead of idling in the camp.
        GrindLevelBand const band = Ladder(5);
        CHECK(band.minLevel == 3 && band.maxLevel == 6);
        CHECK(GrindLevelFits(band, DEFIAS_CUTPURSE));
        CHECK(GrindLevelFits(band, DEFIAS_THUG));
        CHECK(!GrindLevelFits(band, KOBOLD_VERMIN));
        std::cout << "  [PASS] level-5 ladder window admits the next field [3,6]\n";
    }

    // -------------------------------------------------------------
    // Test 2: the window travels with the bot, level by level
    // -------------------------------------------------------------
    {
        // Level 6: level-3 camp fillers no longer hold the bot (band floor is 4),
        // level-7 Kharanos wolves do.
        GrindLevelBand const l6 = Ladder(6);
        CHECK(l6.minLevel == 4 && l6.maxLevel == 7);
        CHECK(!GrindLevelFits(l6, KOBOLD_WORKER));
        CHECK(GrindLevelFits(l6, DEFIAS_CUTPURSE));

        // Level 8: the whole Northshire camp is out (its best mob is level 4).
        GrindLevelBand const l8 = Ladder(8);
        CHECK(l8.minLevel == 6 && l8.maxLevel == 9);
        CHECK(!GrindLevelFits(l8, DEFIAS_THUG));
        CHECK(GrindLevelFits(l8, ICE_CLAW_BEAR));

        // Level 10: the ceiling steps to +2 (the combat cap is +4 from here, but
        // autonomous bots run on weak gear).
        GrindLevelBand const l9 = Ladder(9);
        GrindLevelBand const l10 = Ladder(10);
        CHECK(l9.maxLevel == 10 && l10.maxLevel == 12);
        CHECK(l10.minLevel == 8);

        // Level 60: high-level zones, same shape.
        GrindLevelBand const l60 = Ladder(60);
        CHECK(l60.minLevel == 58 && l60.maxLevel == 62);
        std::cout << "  [PASS] level 6 / 8 / 10 / 60 windows step with the bot\n";
    }

    // -------------------------------------------------------------
    // Test 3: every level has a band that pays real XP and stays fightable
    // -------------------------------------------------------------
    {
        for (std::int32_t level = 1; level <= 60; ++level)
        {
            GrindLevelBand const band = Ladder(level);

            // The ceiling is the order cap below level 10 (+1), and +2 from level
            // 10 on; the grinder's own combat cap (+4) is always above it.
            CHECK(band.maxLevel == level + (level < 10 ? 1 : 2));
            CHECK(band.maxLevel <= level + 4);

            // No green-low / grey latches: the floor sits at `level - 2` or higher.
            CHECK(band.minLevel >= level - 2);

            // A mob at the floor is still above the grey (zero XP) level.
            if (level >= 10)
                CHECK(band.minLevel > level - 5 - level / 10);

            // Monotone: outgrowing a spot moves the window forward, never back.
            CHECK(band.minLevel <= Ladder(level + 1 <= 60 ? level + 1 : 60).minLevel);
            CHECK(band.maxLevel <= Ladder(level + 1 <= 60 ? level + 1 : 60).maxLevel);
        }
        std::cout << "  [PASS] levels 1-60 all keep a paying, fightable band\n";
    }

    // -------------------------------------------------------------
    // Test 4: owned bots keep the conservative window (no behaviour change)
    // -------------------------------------------------------------
    {
        GrindLevelBand const full = Owned(60);
        CHECK(full.minLevel == 50 && full.maxLevel == 57);

        GrindLevelBand const beginner = Owned(4);
        CHECK(beginner.minLevel == 2 && beginner.maxLevel == 4);

        // A beaten-up bot still gets its easier window on the owned path.
        GrindLevelBand const hurt = Owned(60, 20);
        CHECK(hurt.maxLevel < full.maxLevel);
        CHECK(hurt.minLevel < full.minLevel);
        CHECK(hurt.maxLevel == 55 && hurt.minLevel == 48);
        std::cout << "  [PASS] owned-bot windows are unchanged\n";
    }

    // -------------------------------------------------------------
    // Test 5: gear condition cannot drop the ladder floor back to green mobs
    // -------------------------------------------------------------
    {
        for (std::uint8_t power : { 0, 20, 50, 100 })
        {
            GrindLevelBand const band = Ladder(20, power);
            CHECK(band.minLevel >= 18);
            CHECK(band.maxLevel >= 22);
        }
        std::cout << "  [PASS] ladder band holds at every gear condition\n";
    }

    // -------------------------------------------------------------
    // Test 6: coinless wildlife is prey for autonomous bots, critters never are
    // -------------------------------------------------------------
    {
        // Copper-bearing creatures always were grind prey.
        CHECK(GrindPreyAllowed(7, false, true, false));
        CHECK(GrindPreyAllowed(7, false, false, false));

        // Coinless wildlife (wolves, boars, spiders): autonomous bots take it at any
        // level, owned bots keep the copper-only rule (bar the beginner allowance).
        CHECK(GrindPreyAllowed(0, false, true, false));
        CHECK(!GrindPreyAllowed(0, false, false, false));
        CHECK(GrindPreyAllowed(0, false, false, true));

        // Critters pay no XP: never worth a walk, whoever the bot is.
        CHECK(!GrindPreyAllowed(0, true, true, false));
        CHECK(!GrindPreyAllowed(0, true, true, true));
        CHECK(!GrindPreyAllowed(0, true, false, true));
        std::cout << "  [PASS] coinless wildlife in, critters out\n";
    }

    // -------------------------------------------------------------
    // Test 7: a spot only holds as many bots as it has spawns to give
    // -------------------------------------------------------------
    {
        CHECK(GrindSpotCapacity(0) == 2);
        CHECK(GrindSpotCapacity(1) == 2);   // Garrick Padfoot's single spawn
        CHECK(GrindSpotCapacity(6) == 2);
        CHECK(GrindSpotCapacity(9) == 3);
        CHECK(GrindSpotCapacity(39) == 13); // Defias Cutpurse, Goldshire
        CHECK(GrindSpotCapacity(43) == 14); // Young Wolf, Northshire
        CHECK(GrindSpotCapacity(52) == 17); // Stonetusk Boar, Elwynn
        std::cout << "  [PASS] per-spot capacity follows the spawn count\n";
    }

    // -------------------------------------------------------------
    // Test 8: starter-valley exemption from the area-average ceiling
    // -------------------------------------------------------------
    {
        // Durotar (rated 8) and Dun Morogh (rated 7) veto every local grind
        // point for a level 1-4 pool bot through the area ceiling, while its
        // mobs are vetted in-cap by the band. The exemption mirrors the
        // IsLocationLevelValid beginnerGrind gate at the other two area gates.
        CHECK(GrindValleyExempted(1, true));
        CHECK(GrindValleyExempted(4, true));
        CHECK(!GrindValleyExempted(5, true));
        CHECK(!GrindValleyExempted(1, false));
        CHECK(!GrindValleyExempted(60, true));
        std::cout << "  [PASS] level 1-4 masterless bots skip the valley ceiling\n";
    }

    // -------------------------------------------------------------
    // Test 9: idle fallback gate
    // -------------------------------------------------------------
    {
        // An idle masterless bot with no journey and an empty normal pick
        // may take the wider fallback scan at any level (idle brief: bots
        // above 5 stood through whole quest parks with no other rule moving
        // them). The cap, range and per-mob gates are unchanged.
        CHECK(GrindIdleFallbackAllowed(true, 1, false, false, false, true, true, true));
        CHECK(GrindIdleFallbackAllowed(true, 5, false, false, false, true, true, true));
        CHECK(GrindIdleFallbackAllowed(true, 37, false, false, false, true, true, true));
        CHECK(GrindIdleFallbackAllowed(true, 60, false, false, false, true, true, true));
        // Owned/hired bots keep today's behaviour.
        CHECK(!GrindIdleFallbackAllowed(false, 1, false, false, false, true, true, true));
        CHECK(!GrindIdleFallbackAllowed(false, 60, false, false, false, true, true, true));
        // A bot with a journey keeps walking it.
        CHECK(!GrindIdleFallbackAllowed(true, 1, true, false, false, true, true, true));
        CHECK(!GrindIdleFallbackAllowed(true, 60, true, false, false, true, true, true));
        // Fighting, battleground, instance and stuck bots are excluded.
        CHECK(!GrindIdleFallbackAllowed(true, 1, false, true, false, true, true, true));
        CHECK(!GrindIdleFallbackAllowed(true, 1, false, false, true, true, true, true));
        CHECK(!GrindIdleFallbackAllowed(true, 1, false, false, false, false, true, true));
        CHECK(!GrindIdleFallbackAllowed(true, 1, false, false, false, true, false, true));
        // Only when the normal pick came back empty.
        CHECK(!GrindIdleFallbackAllowed(true, 1, false, false, false, true, true, false));
        // The fallback reaches past the 60 yd combat scan but stays nearby.
        CHECK(GRIND_IDLE_FALLBACK_RANGE_YD == 150.0f);
        std::cout << "  [PASS] idle fallback gate is tight\n";
    }

    {
        // Starter-valley leash: only applies to the exempted level 1-4 pool picks.
        CHECK(BeginnerValleyLeashAllows(false, 14, 17, false, 5000.0f));
        // Same zone, inside the circle: allowed.
        CHECK(BeginnerValleyLeashAllows(true, 14, 14, true, 300.0f));
        // Valley of Trials (Durotar 14) bot, Barrens (17) point: refused.
        CHECK(!BeginnerValleyLeashAllows(true, 14, 17, true, 300.0f));
        // Northshire bot, Goldshire point ~580 yd from the abbey: refused.
        CHECK(!BeginnerValleyLeashAllows(true, 12, 12, true, 580.0f));
        // Unresolved zones (async search, vmap not loaded) fall back to distance.
        CHECK(BeginnerValleyLeashAllows(true, 0, 17, true, 400.0f));
        CHECK(!BeginnerValleyLeashAllows(true, 0, 0, true, 501.0f));
        // Homebind on another map: refused.
        CHECK(!BeginnerValleyLeashAllows(true, 14, 14, false, 0.0f));
        CHECK(BEGINNER_HOME_LEASH_YD == 500.0f);
        std::cout << "  [PASS] starter-valley leash keeps exempted picks home\n";
    }

    std::cout << "All grind-spot level-band tests passed.\n";
    return 0;
}
