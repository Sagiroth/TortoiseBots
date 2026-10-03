#pragma once

#include <cstdint>

#include "PullRegenPolicy.h"

namespace ai
{
    // Whether the travel point itself is safe to stand on (q-points).
    //
    // #418 keeps a quest/grind destination inside the sub-10 grind cap by the
    // spawn entry's own template, but the template says nothing about the
    // point's surroundings: a level-4 pool bot on the item-750 trip (Timber
    // Wolf entry 69, level_max 2, in cap) walks to a Timber Wolf spawn point
    // outside Northshire and dies to the Defias Cutpurse 5 / Forest Spider 6 /
    // Mangy Wolf 6 standing next to it. Live pool 2026-10-03 (fresh level-1
    // pool, server on #418): 211 of 523 deaths since 10:51 UTC are bots level
    // 1-4 killed by mobs 2+ above, most of them on quest/grind trips whose
    // destination entry was itself in cap.
    //
    // Rule: a pool bot below level 10 walks no quest-objective / quest-loot /
    // grind point whose surroundings (hostile spawns within
    // POINT_DANGER_RADIUS_YD, static spawn data) hold a creature past its
    // grind cap (PullGrindLevelCap, same numbers the order cap uses). When
    // every point of a destination is dangerous the search comes back empty
    // and the caller parks the purpose like any other empty search.
    // Owned/hired bots keep today's behaviour: their player decides.
    //
    // Donor comparison (mod-playerbots, read-only reference): the donor has no
    // such gate - its TravelMgr never looks at neighbouring spawns
    // (getCreaturesNear is only used to build the destination and node tables)
    // - so this is local, measured on the live pool. The decision below is
    // pure (highest neighbouring level in, verdict out); the spawn lookup
    // lives in WorldPosition's static danger-spawn index next to the
    // hostile-town index, so no per-tick world scan and no per-search spawn
    // table walk.

    // Radius around a quest/grind point inside which hostile spawns threaten
    // the bot standing there: aggro range plus wander, in yards.
    constexpr float POINT_DANGER_RADIUS_YD = 40.0f;

    // The gate only binds pool bots below level 10: from 10 the +4 order cap
    // and the area gates carry the risk, and owned/hired bots follow their
    // player anywhere.
    inline bool PointDangerApplies(std::uint32_t botLevel, bool masterlessRandom)
    {
        return masterlessRandom && botLevel < 10;
    }

    // Whether the highest hostile spawn near the point bars a bot of this
    // level: anything past the grind cap the bot's own orders keep.
    inline bool PointDangerous(int highestHostileLevelMax, std::uint32_t botLevel)
    {
        // Floor at level 4: start valleys hold level 3 hostiles next to almost
        // every point, and treating them as danger for a level-1 bot left it
        // with nothing to do (live 2026-10-03: grind picks -40%).
        int const threshold = (int)botLevel + (int)PullGrindLevelCap(botLevel, false) + 1;
        return highestHostileLevelMax >= (threshold < 4 ? 4 : threshold);
    }
}
