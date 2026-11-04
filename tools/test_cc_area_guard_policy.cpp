// Standalone regression test for the shared crowd-control area scan
// (ai/playerbot/policy/combat/CcAreaGuardPolicy.h): the cast-start guard and a
// running channel's cancel read the same "possible targets no los" list, so a
// mob sheeped before this bot touched the pack (no threat on it, missing from
// "attackers") blocks the cast instead of letting Blizzard start, get
// cancelled and start again every tick.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_cc_area_guard_policy.cpp -o /tmp/test_cc_area_guard_policy
//   /tmp/test_cc_area_guard_policy

#include "../ai/playerbot/policy/combat/CcAreaGuardPolicy.h"

#include <cmath>
#include <cstdio>
#include <list>

using ai::AnyBreakableCcInArea;

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

namespace
{
using Guid = std::uint64_t;

struct Unit
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::uint32_t mapId = 0;
    bool alive = true;
    bool breakableCc = false;
};

Unit const* Lookup(Unit const* const* slots, Guid guid)
{
    return slots[guid];
}

struct World
{
    std::list<Guid> possible;
    Unit const* slots[8] = {};
};

std::size_t g_checks = 0;

bool Scan(World const& world, float radius, float x = 0.0f, float y = 0.0f, float z = 0.0f, std::uint32_t mapId = 1)
{
    ++g_checks;
    return AnyBreakableCcInArea(world.possible,
        [&world](Guid guid) { return Lookup(world.slots, guid); },
        [](Unit const* unit) { return unit->alive; },
        [](Unit const* unit, std::uint32_t map) { return unit->mapId == map; },
        [](Unit const* unit, float px, float py, float pz)
        {
            float const dx = unit->x - px, dy = unit->y - py, dz = unit->z - pz;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        },
        [](Unit const* unit) { return unit->breakableCc; },
        mapId, x, y, z, radius);
}
} // namespace

int main()
{
    // The reported case: a sheep with no threat on this bot is only in the
    // sight list; it stands 6 yd from the Blizzard. The start guard and the
    // running-channel cancel call the same scan, and both find it: no cast,
    // no cancel, no loop.
    {
        Unit const sheep{ 6.0f, 0.0f, 0.0f, 1, true, true };
        World world;
        world.slots[0] = &sheep;
        world.possible = { 0 };
        bool const startBlocked = Scan(world, 8.0f);
        bool const channelCancelled = Scan(world, 8.0f);
        CHECK(startBlocked);
        CHECK(channelCancelled);
        CHECK(startBlocked == channelCancelled);
    }

    // The same list and geometry heuristics: outside the radius, another map,
    // dead, or not under breakable CC, the area is free.
    {
        Unit const far{ 12.0f, 0.0f, 0.0f, 1, true, true };
        Unit const elsewhere{ 1.0f, 0.0f, 0.0f, 2, true, true };
        Unit const dead{ 1.0f, 0.0f, 0.0f, 1, false, true };
        Unit const clean{ 1.0f, 0.0f, 0.0f, 1, true, false };
        World world;
        world.slots[0] = &far;
        world.slots[1] = &elsewhere;
        world.slots[2] = &dead;
        world.slots[3] = &clean;
        world.possible = { 0, 1, 2, 3 };
        CHECK(!Scan(world, 8.0f));

        // One real hit among the misses still blocks.
        Unit const sheep{ 0.0f, 5.0f, 0.0f, 1, true, true };
        world.slots[4] = &sheep;
        world.possible.push_back(4);
        CHECK(Scan(world, 8.0f));
        CHECK(!Scan(world, 4.0f));
    }

    // A stale guid (the unit left the map since the list was built) is skipped.
    {
        World world;
        world.possible = { 7 };
        CHECK(!Scan(world, 8.0f));
    }

    if (g_failed)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("test_cc_area_guard_policy: all checks passed (%zu scans)\n", g_checks);
    return 0;
}
