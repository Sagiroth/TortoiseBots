// Standalone test for the ammo cache pick: deprecated rows never win, the best
// damage per second a bot of that level can use does, ties go to the lowest
// entry, and the answer does not depend on row order. The rows are the real
// Turtle item_template projectiles (class 6, white).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_ammo_cache_policy.cpp -o /tmp/test_ammo_cache_policy
//   /tmp/test_ammo_cache_policy

#include "../ai/playerbot/strategy/values/AmmoCachePolicy.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

using namespace ai;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static AmmoCandidate Row(uint32_t entry, uint32_t level, uint32_t flags, char const* name,
    double dmgMin, double dmgMax)
{
    AmmoCandidate c;
    c.entry = entry;
    c.requiredLevel = level;
    c.flags = flags;
    c.name = name;
    c.dmgMin = dmgMin;
    c.dmgMax = dmgMax;
    c.delayMs = 3000;
    return c;
}

static std::vector<AmmoCandidate> Arrows()
{
    return {
        Row(2103, 1, 16, "Test Arrow", 1, 1),
        Row(2512, 1, 0, "Rough Arrow", 1, 2),
        Row(2514, 3, 16, "Depricated Sharp Arrow", 2, 2),
        Row(2515, 10, 0, "Sharp Arrow", 3, 4),
        Row(2517, 1, 16, "Deprecated Standard Arrow", 1, 2),
        Row(3029, 15, 16, "Depricated Whipwood Arrow", 4, 5),
        Row(3030, 25, 0, "Razor Arrow", 7, 8),
        Row(3031, 30, 16, "Depricated Razor Arrow", 8, 9),
        Row(11285, 40, 0, "Jagged Arrow", 13, 13),
        Row(19082, 1, 0, "Monster - Fire Arrow", 1, 1),
    };
}

static std::vector<AmmoCandidate> Shot()
{
    return {
        Row(2104, 1, 16, "Deprecated Standard Shot", 1, 1),
        Row(2516, 1, 0, "Light Shot", 1, 2),
        Row(3032, 15, 16, "Deprecated Impact Shot", 4, 5),
        Row(3034, 30, 16, "\"BKP Impact Shot\"", 9, 9),
        Row(8069, 30, 0, "Crafted Solid Shot", 8, 9),
        Row(3033, 25, 0, "Solid Shot", 7, 8),
        Row(11284, 40, 0, "Accurate Slugs", 13, 13),
    };
}

int main()
{
    // 19i: the level-30 bucket picked 3031 (Depricated Razor Arrow). It is the
    // flagged row, so the real Razor Arrow (3030) wins now.
    CHECK(PickAmmo(Arrows(), 31) == 3030);
    CHECK(PickAmmo(Arrows(), 21) == 2515);
    CHECK(PickAmmo(Arrows(), 11) == 2515);
    CHECK(PickAmmo(Arrows(), 1) == 2512);     // not the test / monster rows
    CHECK(PickAmmo(Arrows(), 41) == 11285);
    CHECK(PickAmmo(Shot(), 31) == 8069);      // 8069 (rl30) over 3033 (rl25): more damage
    CHECK(PickAmmo(Shot(), 21) == 2516);
    CHECK(PickAmmo(Shot(), 41) == 11284);

    // A bot never gets a row above its level.
    CHECK(PickAmmo(Arrows(), 9) == 2512);
    CHECK(PickAmmo(Arrows(), 0) == 0);

    // Nothing usable: 0 (the caller keeps the old bucket empty).
    CHECK(PickAmmo({ Row(3031, 30, 16, "Depricated Razor Arrow", 8, 9) }, 60) == 0);
    CHECK(PickAmmo({}, 60) == 0);

    // The flag alone rejects, and so does each placeholder name on its own.
    CHECK(!IsObtainableAmmo(Row(1, 1, 16, "Razor Arrow", 7, 8)));
    CHECK(!IsObtainableAmmo(Row(1, 1, 0, "Depricated Razor Arrow", 7, 8)));
    CHECK(!IsObtainableAmmo(Row(1, 1, 0, "Deprecated Iron Shot", 7, 8)));
    CHECK(!IsObtainableAmmo(Row(1, 1, 0, "Fast Test Ammo", 7, 8)));
    CHECK(!IsObtainableAmmo(Row(1, 1, 0, "Monster - Fire Arrow", 7, 8)));
    CHECK(IsObtainableAmmo(Row(1, 1, 0, "Razor Arrow", 7, 8)));

    // Ties: same damage, same level -> the lowest entry, either row order.
    std::vector<AmmoCandidate> tie = { Row(900, 30, 0, "Arrow B", 8, 9), Row(700, 30, 0, "Arrow A", 8, 9) };
    CHECK(PickAmmo(tie, 30) == 700);
    std::reverse(tie.begin(), tie.end());
    CHECK(PickAmmo(tie, 30) == 700);
    // Same damage, higher required level wins before the entry does.
    std::vector<AmmoCandidate> level = { Row(100, 20, 0, "Arrow C", 8, 9), Row(900, 30, 0, "Arrow D", 8, 9) };
    CHECK(PickAmmo(level, 30) == 900);
    // More damage beats a higher required level.
    std::vector<AmmoCandidate> dps = { Row(100, 20, 0, "Arrow E", 12, 14), Row(900, 30, 0, "Arrow F", 8, 9) };
    CHECK(PickAmmo(dps, 30) == 100);
    // A slower shot of equal damage is worth less.
    AmmoCandidate slow = Row(100, 30, 0, "Arrow G", 8, 9);
    slow.delayMs = 6000;
    CHECK(PickAmmo({ slow, Row(900, 30, 0, "Arrow H", 8, 9) }, 30) == 900);
    // Row order never changes the pick.
    std::vector<AmmoCandidate> all = Arrows();
    for (int i = 0; i < 12; ++i)
    {
        std::next_permutation(all.begin(), all.end(),
            [](AmmoCandidate const& a, AmmoCandidate const& b) { return a.entry < b.entry; });
        CHECK(PickAmmo(all, 31) == 3030);
    }
    std::printf("ammo cache policy: %d checks passed\n", checks);
    return 0;
}
