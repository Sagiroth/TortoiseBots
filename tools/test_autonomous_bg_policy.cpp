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

    // Happy path: enabled, no human demand, nothing running or queued.
    CHECK(ShouldSeedAutonomousBg(true, false, 0, 0) == true);
    std::cout << "  [PASS] seeds when idle\n";

    // Disabled by default (opt-in).
    CHECK(ShouldSeedAutonomousBg(false, false, 0, 0) == false);
    std::cout << "  [PASS] stays off unless enabled\n";

    // Human demand owns queueing; the seeder yields.
    CHECK(ShouldSeedAutonomousBg(true, true, 0, 0) == false);
    std::cout << "  [PASS] yields to human demand\n";

    // Cap reached: a running bot-only match blocks a second.
    CHECK(ShouldSeedAutonomousBg(true, false, 1, 0) == false);
    CHECK(ShouldSeedAutonomousBg(true, false, 2, 0) == false);
    std::cout << "  [PASS] one running match blocks another\n";

    // Anti-over-queue (the donor's known bug): seeds still waiting in queue
    // block new seeds until the core starts them or they time out.
    CHECK(ShouldSeedAutonomousBg(true, false, 0, 1) == false);
    CHECK(ShouldSeedAutonomousBg(true, false, 0, 20) == false);
    std::cout << "  [PASS] queued seeds block further seeding\n";

    // Per-team fill stops exactly at 10v10.
    CHECK(TeamNeedsSeedBots(0) == true);
    CHECK(TeamNeedsSeedBots(9) == true);
    CHECK(TeamNeedsSeedBots(10) == false);
    CHECK(TeamNeedsSeedBots(11) == false);
    std::cout << "  [PASS] team fill stops at 10\n";

    std::cout << "All autonomous BG policy tests passed.\n";
    return 0;
}
