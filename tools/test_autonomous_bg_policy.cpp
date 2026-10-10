// Autonomous bot-only BG seeding policy tests (i-queue follow-up).
// Ported donor behaviour: mod-playerbots RandomPlayerbotMgr::CheckBgQueue
// autonomous seeder (src/Bot/RandomPlayerbotMgr.cpp:1162-1217).

#include "../runtime/AutonomousBgPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using TortoiseBots::AutonomousBgTeamTarget;
using TortoiseBots::BracketCanSeed;
using TortoiseBots::ClampAutonomousMaxInstances;
using TortoiseBots::kAutonomousBgMaxInstances;
using TortoiseBots::kAutonomousBgTeamSize;
using TortoiseBots::ShouldSeedAutonomousBg;
using TortoiseBots::TeamNeedsSeedBots;

int main()
{
    std::cout << "Starting TortoiseBots autonomous BG policy tests...\n";

    // Cap: exactly one concurrent bot-only instance on an average PC.
    CHECK(kAutonomousBgMaxInstances == 1);
    CHECK(kAutonomousBgTeamSize == 10);
    CHECK(AutonomousBgTeamTarget() == 10);
    std::cout << "  [PASS] one WSG instance of 10v10 is the cap\n";

    // Happy path: enabled, no human demand, nothing running, a team needs bots.
    CHECK(ShouldSeedAutonomousBg(true, false, 0, true) == true);
    std::cout << "  [PASS] seeds when idle\n";

    // Disabled -> off.
    CHECK(ShouldSeedAutonomousBg(false, false, 0, true) == false);
    std::cout << "  [PASS] stays off unless enabled\n";

    // Human demand owns queueing; the seeder yields.
    CHECK(ShouldSeedAutonomousBg(true, true, 0, true) == false);
    std::cout << "  [PASS] yields to human demand\n";

    // Cap reached: a running bot-only match blocks a second.
    CHECK(ShouldSeedAutonomousBg(true, false, 1, true) == false);
    CHECK(ShouldSeedAutonomousBg(true, false, 2, true) == false);
    std::cout << "  [PASS] one running match blocks another\n";

    // Need-based top-up (no first-seed latch): queued seeds do NOT block —
    // batches accumulate across ticks until both teams are full.
    CHECK(ShouldSeedAutonomousBg(true, false, 0, true) == true);
    // Both teams full -> stop.
    CHECK(ShouldSeedAutonomousBg(true, false, 0, false) == false);
    // Bracket-scoped cap: the count passed in is per-bracket now — a match in
    // another bracket does not block this one (0 running HERE seeds).
    CHECK(ShouldSeedAutonomousBg(true, false, 0, true, 1) == true);
    std::cout << "  [PASS] top-up continues while a team needs bots\n";

    // Max-instances clamp: huge/negative-wrapped values cap at 2, never uncapped.
    CHECK(ClampAutonomousMaxInstances(0) == 0);
    CHECK(ClampAutonomousMaxInstances(1) == 1);
    CHECK(ClampAutonomousMaxInstances(2) == 2);
    CHECK(ClampAutonomousMaxInstances(10) == 2);
    CHECK(ClampAutonomousMaxInstances(4294967295u) == 2);
    CHECK(ShouldSeedAutonomousBg(true, false, 2, true, 4294967295u) == false);
    std::cout << "  [PASS] max-instances knob clamps to 2\n";

    // Bracket readiness: both factions need a full team of eligibles.
    CHECK(BracketCanSeed(10, 10) == true);
    CHECK(BracketCanSeed(19, 1) == false);
    CHECK(BracketCanSeed(1, 19) == false);
    CHECK(BracketCanSeed(0, 20) == false);
    std::cout << "  [PASS] lopsided brackets never seed\n";

    // Per-team fill stops exactly at 10v10.
    CHECK(TeamNeedsSeedBots(0) == true);
    CHECK(TeamNeedsSeedBots(9) == true);
    CHECK(TeamNeedsSeedBots(10) == false);
    CHECK(TeamNeedsSeedBots(11) == false);
    std::cout << "  [PASS] team fill stops at 10\n";

    std::cout << "All autonomous BG policy tests passed.\n";
    return 0;
}
