// Standalone regression test for even start-zone distribution: the race ->
// start-zone mapping and the least-populated pick that keep the six racial
// starting zones balanced. Exercises the pure rules without a database or a
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

static void TestEmptyPoolKeepsHistoricalDefaults()
{
    uint32_t counts[kStartZoneCount] = {};
    CHECK(LeastPopulatedStartZone(counts, true) == 0);
    CHECK(LeastPopulatedStartZone(counts, false) == 3);
}

static void TestEmptiestZoneWins()
{
    uint32_t horde[kStartZoneCount] = { 147, 40, 42, 0, 0, 0 };
    CHECK(LeastPopulatedStartZone(horde, true) == 1);
    uint32_t alliance[kStartZoneCount] = { 0, 0, 0, 117, 106, 42 };
    CHECK(LeastPopulatedStartZone(alliance, false) == 5);
}

static void TestCrossFactionCountsAreIgnored()
{
    // A full Alliance side must not push a Horde bot out of Valley of Trials.
    uint32_t counts[kStartZoneCount] = { 5, 9, 9, 0, 0, 0 };
    CHECK(LeastPopulatedStartZone(counts, true) == 0);
    CHECK(LeastPopulatedStartZone(counts, false) == 3);
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

int main()
{
    TestRaceMapping();
    TestEmptyPoolKeepsHistoricalDefaults();
    TestEmptiestZoneWins();
    TestCrossFactionCountsAreIgnored();
    TestSpawnTableMatchesCorePlayercreateinfo();
    std::printf("start zone balance: %d checks passed\n", checks);
    return 0;
}
