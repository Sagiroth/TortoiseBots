#pragma once

// Open-world PvP seek rules for pool bots: which enemy players a bot holding
// the seek role may start a fight with. Level-gap odds come from the owner
// spec, tightened after the 2026-11-01 pool window (4.3k PvP deaths in 7 h,
// 28 % of all deaths): a seeker never starts up (0 % at +3 or more, where
// the grind side already refuses new pulls above its cap); 5+ below stays
// 25 % (ganking grey players is still usually refused), closer 100 %.
// The yes/no stays stable per (bot, target) pair for about two minutes so
// the bot does not re-roll every tick until it says yes.
// Pure and core-free, so tools/test_world_pvp_policy.cpp includes it directly.

#include <cstdint>

namespace TortoiseBots
{

// Odds (0-100) of accepting a target `targetLevel - botLevel` levels apart.
// A return of 0 means never, 100 means always. Only the upward side is
// gated: seeking down keeps the old odds (the revenge/assist answers below
// are untouched, and a higher bot is expected to win).
inline std::uint32_t WorldPvpLevelGapOdds(std::int32_t levelGap)
{
    if (levelGap >= 3)
        return 0;
    if (levelGap == 2)
        return 75;
    if (levelGap == 1 || levelGap == 0 || levelGap == -1)
        return 100;
    std::int32_t const apart = levelGap < 0 ? -levelGap : levelGap;
    if (apart == 2)
        return 75;
    if (apart == 3)
        return 50;
    return 25;
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
