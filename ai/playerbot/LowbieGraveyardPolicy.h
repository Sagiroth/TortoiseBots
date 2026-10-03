#pragma once

#include <cstdint>

namespace ai
{
    // Graveyard-strand guard for masterless pool bots below level 10 (owner
    // refinement of the lowbie-graveyard task): every start valley's nearest
    // graveyard for its eastern half sits in the next town (Brill, Goldshire,
    // Kharanos, Razor Hill, Bloodhoof, Dolanaar), so a valley death that goes
    // to the spirit healer wakes the bot in an area above its level, and the
    // walk back crosses mobs it cannot survive.
    //
    // Two pure rules; the callers own the AI/world reads:
    //
    // 1. ShouldSkipDeathCountSpiritHeal: below level 10 a masterless pool bot
    //    never goes to the spirit healer just for the death count - the corpse
    //    run stays the default. The stall and long-dead-time gates still fire,
    //    so a genuinely stuck ghost is still rescued. Owned/hired bots and
    //    level 10+ keep today's behaviour.
    //
    // 2. ShouldSendLowbieHome: after any revive/teleport, a sub-10 pool bot
    //    standing in an area rated above its level (town area ~5 vs a level
    //    1-3 bot) goes back to its birthplace via homebind teleport
    //    (TeleportToHomebind, no hearth cooldown) instead of walking. The
    //    home area must itself fit (fail closed otherwise), so a mis-set
    //    homebind can never bounce the bot somewhere worse.
    //
    // Area levels are the validated ai_playerbot_zone_level cache values
    // (TravelMgr::TryGetValidatedAreaLevel): towns rate 5 (87/131/159/186/
    // 222/362), valleys 2-7 (Northshire 9:2, Deathknell 154:3, Coldridge
    // 132:4, Valley of Trials 363:4, Red Cloud Mesa 220:4, Shadowglen 188:4,
    // Camp Narache 221:6, Anvilmar 77:7, Aldrassil 256:7). Unknown (<= 0)
    // fails closed on both halves.

    // Highest level whose corpse run stays the default: below 10 the pool
    // grind caps (GrindSpotPolicy.h, PullRegenPolicy.h) keep the bot in its
    // valley, so a death-count spirit heal only strands it.
    std::uint32_t const LOWBIE_GRAVEYARD_MAX_LEVEL = 10;

    // A revive counts as "above the bot's level" when the area rates more
    // than this above it. Towns rate 5: a level 1-2 bot woken there is over
    // its head, a level 3+ bot (5 <= level + 2) can walk home. Must stay below
    // the +5 misplaced rule (BotManager.cpp): this fires first, on weaker
    // evidence, and only sends the bot to its own homebind, never onward.
    std::int32_t const LOWBIE_HOME_AREA_MARGIN = 2;

    inline bool ShouldSkipDeathCountSpiritHeal(std::uint32_t botLevel,
        bool masterlessRandom)
    {
        return masterlessRandom && botLevel < LOWBIE_GRAVEYARD_MAX_LEVEL;
    }

    inline bool ShouldSendLowbieHome(std::uint32_t botLevel, bool masterlessRandom,
        int currentAreaLevel, int homeAreaLevel)
    {
        if (!masterlessRandom || botLevel >= LOWBIE_GRAVEYARD_MAX_LEVEL)
            return false;
        if (currentAreaLevel <= 0 || homeAreaLevel <= 0)
            return false;
        if (currentAreaLevel <= (int)botLevel + LOWBIE_HOME_AREA_MARGIN)
            return false;
        if (homeAreaLevel > (int)botLevel + LOWBIE_HOME_AREA_MARGIN)
            return false;
        return true;
    }
}
