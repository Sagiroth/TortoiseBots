// Standalone check for the run leader's healer rules (Uldaman run 4, 2026-10-17):
//   g++ -std=c++17 -Wall -Wextra tools/test_run_healer_policy.cpp -o /tmp/test_run_healer_policy && /tmp/test_run_healer_policy
//  - a healer in a run heals and does not wand (HealerMayWand, healOnly),
//  - its stand for a pull is behind the tank, away from the pack (HealerBackPoint),
//  - a reach in a run-leader pull holds the healer like the others (wiring checks in tools/test_run_leader_wiring.py).
#include "../ai/playerbot/WandPolicy.h"
#include "../runtime/DungeonRunPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

static void TestHealOnlyHealerNeverWands()
{
    HealerWandGate g;
    g.groupedHealer = true;
    g.targetEngaged = true; // a mob fighting the tank, a healthy party: the ordinary healer wands
    CHECK(HealerMayWand(g));
    g.healOnly = true;
    CHECK(!HealerMayWand(g));
    // Out of mana too: the run's healer heals or stands.
    g.noMana = true;
    CHECK(!HealerMayWand(g));
    // An explicit order to this bot (player control first) and a bot with nobody near keep their wand.
    g.explicitOrder = true;
    CHECK(HealerMayWand(g));
    g.explicitOrder = false;
    g.aloneNear = true;
    CHECK(HealerMayWand(g));
    // Not a grouped healer: never held back here.
    HealerWandGate s;
    s.healOnly = true;
    CHECK(HealerMayWand(s));
}

static void TestHealerStandsBehindTheTank()
{
    using namespace TortoiseBots::DungeonRun;
    float x = 0, y = 0;
    // Tank at (-145, 330), the camp to the north (+y) at (-140, 389): behind is south.
    CHECK(HealerBackPoint(-145, 330, -140, 389, kHealerBackYd, x, y));
    CHECK(y < 330 && std::fabs((330 - y) - kHealerBackYd * 59.0f / std::sqrt(25.0f + 59.0f * 59.0f)) < 0.01f);
    // On the line from the pack through the tank: 9 yd from the tank, farther from the pack than the tank.
    float const tankToPack = std::sqrt(25.0f + 59.0f * 59.0f);
    float const healerToPack = std::sqrt((x + 140) * (x + 140) + (y - 389) * (y - 389));
    CHECK(std::fabs(healerToPack - (tankToPack + kHealerBackYd)) < 0.01f);
    CHECK(kHealerBackYd >= 8.0f && kHealerBackYd <= 10.0f);
    // Heal range of the tank (40 yd) holds for the half distance fallback too.
    CHECK(HealerBackPoint(0, 0, 100, 0, kHealerBackYd * 0.5f, x, y) && x < 0 && std::fabs(y) < 0.001f);
    // Standing on the target: no side is behind.
    CHECK(!HealerBackPoint(5, 5, 5.2f, 5.1f, kHealerBackYd, x, y));
}

int main()
{
    TestHealOnlyHealerNeverWands();
    TestHealerStandsBehindTheTank();
    std::cout << "ok\n";
    return 0;
}
