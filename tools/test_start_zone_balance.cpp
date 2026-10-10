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

static void TestStartZoneLevelBand()
{
    // A fresh pool levels to 2-3 while creation still runs: those bots are
    // still in their start zone and must keep counting.
    CHECK(CountsTowardStartZone(1));
    CHECK(CountsTowardStartZone(3));
    CHECK(CountsTowardStartZone(9));
    CHECK(!CountsTowardStartZone(10));
    CHECK(!CountsTowardStartZone(60));
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
    TestStartZoneLevelBand();
    TestSpawnTableMatchesCorePlayercreateinfo();
    std::printf("start zone balance: %d checks passed\n", checks);
    return 0;
}
