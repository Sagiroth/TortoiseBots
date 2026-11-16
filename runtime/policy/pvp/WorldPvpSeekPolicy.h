#pragma once

// Open-world PvP seek role: who may hold it and when it is re-rolled.
// A share of free pool bots gets the role on login and strategy resets, and
// the role is rolled again about every WorldPvpSeekRerollMinutes (with
// jitter), never while the bot is in combat. Owned and hired bots, and bots
// grouped with a real player, never get the role.
//
// Pure and core-free, so tools/test_world_pvp_seek_policy.cpp includes it
// directly.

#include <cstdint>

namespace TortoiseBots
{

struct WorldPvpSeekInputs
{
    bool seekEnabled = false;       // WorldPvpSeek != 0
    bool freePoolBot = false;       // masterless random pool bot
    bool ownedBot = false;          // player-owned bot
    bool hiredBot = false;          // hired companion
    bool groupedWithRealPlayer = false;
    bool inBattleground = false;
    bool pvpFlagged = false;        // in the PvP-flagged share of the pool
};

inline bool WorldPvpSeekEligible(WorldPvpSeekInputs const& in)
{
    if (!in.seekEnabled)
        return false;
    if (!in.freePoolBot)
        return false;
    if (in.ownedBot || in.hiredBot)
        return false;
    if (in.groupedWithRealPlayer)
        return false;
    if (in.inBattleground)
        return false;
    if (!in.pvpFlagged)
        return false;
    return true;
}

// Whether a pool bot is in the PvP-flagged share. Stable per character, so the
// same bots carry the flag after every login.
inline bool PoolBotInPvpFlaggedShare(std::uint32_t guidLow, std::uint32_t percent)
{
    if (percent >= 100)
        return true;
    std::uint32_t const bucket = static_cast<std::uint32_t>((guidLow * 2654435761u) >> 16) % 100u;
    return bucket < percent;
}

// True when a bot holding the role must lose it right away: eligibility is
// re-checked every service pass, not only at the next re-roll, so grouping
// with a real player (or becoming owned/hired) strips the role promptly.
inline bool WorldPvpSeekMustStrip(bool holdsRole, WorldPvpSeekInputs const& in)
{
    return holdsRole && !WorldPvpSeekEligible(in);
}

// True when the role may be rolled again: the interval (minutes, with the
// caller applying jitter to the next deadline) has passed and the bot is not
// in combat.
inline bool WorldPvpSeekRerollDue(std::uint64_t lastRollMs, std::uint64_t nowMs,
    std::uint32_t rerollMinutes, bool inCombat)
{
    if (inCombat)
        return false;
    if (rerollMinutes == 0)
        return false;
    if (nowMs < lastRollMs)
        return false;
    return nowMs - lastRollMs >= std::uint64_t(rerollMinutes) * 60 * 1000;
}

}
