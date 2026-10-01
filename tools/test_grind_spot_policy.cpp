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
using ai::GrindLevelFits;

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

        // Level 60: high-level zones, same shape.
        GrindLevelBand const l60 = Ladder(60);
        CHECK(l60.minLevel == 58 && l60.maxLevel == 61);
        std::cout << "  [PASS] level 6 / 8 / 60 windows step with the bot\n";
    }

    // -------------------------------------------------------------
    // Test 3: every level has a band that pays real XP and stays fightable
    // -------------------------------------------------------------
    {
        for (std::int32_t level = 1; level <= 60; ++level)
        {
            GrindLevelBand const band = Ladder(level);

            // The band never demands a mob the grinder refuses: below level 10 the
            // order cap is +1, at 10+ it is +4.
            CHECK(band.maxLevel >= level + 1);
            if (level < 10)
                CHECK(band.maxLevel <= level + 1);
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
            CHECK(band.maxLevel >= 21);
        }
        std::cout << "  [PASS] ladder band holds at every gear condition\n";
    }

    std::cout << "All grind-spot level-band tests passed.\n";
    return 0;
}
