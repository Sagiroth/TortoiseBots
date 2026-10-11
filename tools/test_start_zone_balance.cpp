// Standalone regression test for even start-zone distribution: the race ->
// start-zone mapping and the level band counted toward the six racial
// starting zones. Exercises the pure rules without a database or a
// running core.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_start_zone_balance.cpp -o /tmp/test_start_zone_balance
//   /tmp/test_start_zone_balance

#include "../runtime/StartZoneBalance.h"

#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void TestRaceMapping()
{
    CHECK(StartZoneIndexForRace(2) == 0);  // orc -> Valley of Trials
    CHECK(StartZoneIndexForRace(8) == 0);  // troll -> Valley of Trials
    CHECK(StartZoneIndexForRace(9) == 0);  // goblin -> Valley of Trials
    CHECK(StartZoneIndexForRace(6) == 1);  // tauren -> Camp Narache
    CHECK(StartZoneIndexForRace(5) == 2);  // undead -> Deathknell
    CHECK(StartZoneIndexForRace(1) == 3);  // human -> Northshire
    CHECK(StartZoneIndexForRace(10) == 3); // high elf -> Northshire
    CHECK(StartZoneIndexForRace(3) == 4);  // dwarf -> Coldridge
    CHECK(StartZoneIndexForRace(7) == 4);  // gnome -> Coldridge
    CHECK(StartZoneIndexForRace(4) == 5);  // night elf -> Shadowglen
    CHECK(StartZoneIndexForRace(0) == -1);
    CHECK(StartZoneIndexForRace(11) == -1);
}

static void TestSpawnTableMatchesCorePlayercreateinfo()
{
    CHECK(kStartZoneSpawns[0].map == 1 && kStartZoneSpawns[0].area == 14);
    CHECK(kStartZoneSpawns[3].map == 0 && kStartZoneSpawns[3].area == 12);
    CHECK(kStartZoneSpawns[1].area == 215);
    CHECK(kStartZoneSpawns[2].area == 85);
    CHECK(kStartZoneSpawns[4].area == 1);
    CHECK(kStartZoneSpawns[5].area == 141);
}


static bool Parse(char const* text, uint32_t (&out)[kStartZoneCount])
{
    return ParseStartZoneWeights(text, out);
}

static void TestWeightParser()
{
    uint32_t w[kStartZoneCount] = {};
    // Empty/blank keeps today's even spread (not an error).
    CHECK(Parse("", w));
    CHECK(Parse("  \t ", w));
    for (uint32_t zone = 0; zone < kStartZoneCount; ++zone)
        CHECK(w[zone] == 0);
    // Region names, spawn-name aliases, spacing/case variants.
    CHECK(Parse("Durotar=100", w));
    CHECK(w[0] == 100 && w[1] == 0 && w[3] == 0);
    CHECK(Parse("Durotar=100,Elwynn=50", w));
    CHECK(w[0] == 100 && w[3] == 50 && w[1] == 0);
    CHECK(Parse("northshire=7", w));
    CHECK(w[3] == 7 && w[0] == 0);
    CHECK(Parse(" Dun_Morogh = 30 , TELDRASSIL=20 ", w));
    CHECK(w[4] == 30 && w[5] == 20 && w[0] == 0);
    // Last weight wins for a repeated zone; huge values cap, never overflow.
    CHECK(Parse("Durotar=10,Durotar=5", w));
    CHECK(w[0] == 5);
    CHECK(Parse("Durotar=99999999999999999999", w));
    CHECK(w[0] == kStartZoneMaxWeight);
    // Malformed text fails so the caller logs and falls back to the even spread.
    CHECK(!Parse("Durotar", w));
    CHECK(!Parse("Durotar=", w));
    CHECK(!Parse("=100", w));
    CHECK(!Parse("Stormwind=100", w));
    CHECK(!Parse("Durotar=abc", w));
    CHECK(!Parse("Durotar=-5", w));
    CHECK(!Parse("Durotar=10.5", w));
    // All-zero weights are unusable, not "unset": fail so the caller logs
    // instead of silently running the even spread on an accepted config.
    CHECK(!Parse("Durotar=0", w));
    CHECK(!Parse("Durotar=0,Mulgore=0,Tirisfal=0,Elwynn=0,Dun Morogh=0,Teldrassil=0", w));
}

static void TestWeightedPick()
{
    uint32_t w[kStartZoneCount] = {};
    CHECK(Parse("Durotar=100", w));
    bool horde[kStartZoneCount] = { true, true, true, false, false, false };
    CHECK(StartZoneWeightedTotal(w, horde) == 100);
    // Every roll in [0, 100) lands in Durotar.
    CHECK(StartZoneWeightedPick(w, horde, 0) == 0);
    CHECK(StartZoneWeightedPick(w, horde, 99) == 0);
    // Past the end is -1 (caller falls back); zero-weight zones are skipped.
    CHECK(StartZoneWeightedPick(w, horde, 100) == -1);
    bool alliance[kStartZoneCount] = { false, false, false, true, true, true };
    CHECK(StartZoneWeightedTotal(w, alliance) == 0);
    // Durotar-only weights with an Alliance-only account: nothing selectable.
    CHECK(Parse("Durotar=100,Elwynn=50", w));
    CHECK(StartZoneWeightedTotal(w, alliance) == 50);
    CHECK(StartZoneWeightedPick(w, alliance, 0) == 3);
    CHECK(StartZoneWeightedPick(w, alliance, 49) == 3);
    CHECK(StartZoneWeightedPick(w, alliance, 50) == -1);
    // Split weights map each roll to its zone.
    bool all[kStartZoneCount] = { true, true, true, true, true, true };
    CHECK(StartZoneWeightedTotal(w, all) == 150);
    CHECK(StartZoneWeightedPick(w, all, 0) == 0);
    CHECK(StartZoneWeightedPick(w, all, 99) == 0);
    CHECK(StartZoneWeightedPick(w, all, 100) == 3);
    CHECK(StartZoneWeightedPick(w, all, 149) == 3);
}

int main()
{
    TestRaceMapping();
    TestWeightParser();
    TestWeightedPick();
    TestSpawnTableMatchesCorePlayercreateinfo();
    std::printf("start zone balance: %d checks passed\n", checks);
    return 0;
}
