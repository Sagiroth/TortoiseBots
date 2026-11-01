#pragma once

// Open-world PvP seek rules for pool bots: which enemy players a bot holding
// the seek role may start a fight with. Level-gap odds come from the owner
// spec (never 5+ above; 4 apart or 5+ below 25 %, 3 -> 50 %, 2 -> 75 %,
// closer 100 %); the yes/no stays stable per (bot, target) pair for about
// two minutes so the bot does not re-roll every tick until it says yes.
//
// Pure and core-free, so tools/test_world_pvp_policy.cpp includes it directly.

#include <cstdint>

namespace TortoiseBots
{

// Odds (0-100) of accepting a target `targetLevel - botLevel` levels apart.
// A return of 0 means never, 100 means always.
inline std::uint32_t WorldPvpLevelGapOdds(std::int32_t levelGap)
{
    if (levelGap >= 5)
        return 0;
    std::int32_t const apart = levelGap < 0 ? -levelGap : levelGap;
    if (apart >= 4)
        return 25;
    if (apart == 3)
        return 50;
    if (apart == 2)
        return 75;
    return 100;
}

// Window in seconds during which one (bot, target) pair keeps its level-gap
// decision.
inline std::uint32_t WorldPvpLevelGapStabilitySeconds()
{
    return 120;
}

// True when a stored level-gap decision made at `decidedAtSec` still holds
// at `nowSec`.
inline bool WorldPvpLevelGapDecisionFresh(std::uint64_t decidedAtSec, std::uint64_t nowSec)
{
    if (nowSec < decidedAtSec)
        return true;
    return nowSec - decidedAtSec < WorldPvpLevelGapStabilitySeconds();
}

}
