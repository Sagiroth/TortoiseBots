#pragma once

#include <cstdint>

namespace TortoiseBots
{
// Level-up gear top-up for masterless pool bots (AiPlayerbot.PoolGearTopUpOnLevel).
//
// The fresh-bot seed (GearSeedingGuard.h) gears a pool bot once, on its first
// login at level 5+. A bot that was created at level 1, or that outlevels its
// seed, then levels on drops alone, and the 11-20 band ended up in 63 %
// white/grey gear with half the bots under 13 filled slots (Oct 2026 pool).
// mod-playerbots runs an incremental InitEquipment on every random-bot
// level-up (AutoMaintenanceOnLevelupAction::AutoUpgradeEquip); this module
// does the same through its own fresh-seed item selection, but only for the
// slots below, so earned gear is never replaced.
//
// Pure functions, unit-tested in tools/test_pool_gear_topup_policy.cpp.

// Grey/white items this many item levels (or more) below the bot are
// "clearly outdated": the level-1 starter kit at level 7+, a level-5 white
// at level 11+.
constexpr std::uint32_t kPoolGearOutdatedItemLevelGap = 6;

// Below this level InitEquipment keeps the starting outfit, so the top-up
// has nothing to do either.
constexpr std::uint32_t kPoolGearTopUpMinLevel = 5;

// A real player's companions are never touched: hired companions (random
// record but an active hire), guild-claimed bots and any bot with a live
// player master keep the gear their player chose.
inline bool PoolGearTopUpEligible(bool enabled, bool isRandomBot, bool isHired, bool isClaimed,
    bool hasRealPlayerMaster)
{
    return enabled && isRandomBot && !isHired && !isClaimed && !hasRealPlayerMaster;
}

// Once per level: the last level handled is kept in memory (facade value),
// so a restart gives every bot one check at its current level.
inline bool PoolGearTopUpDue(std::uint32_t botLevel, std::uint32_t lastTopUpLevel)
{
    return botLevel >= kPoolGearTopUpMinLevel && botLevel != lastTopUpLevel;
}

// Which equipment slots the top-up may fill: an empty slot, or a grey/white
// item (quality 0-1) at least kPoolGearOutdatedItemLevelGap item levels below
// the bot. Green or better is never replaced, whatever its item level.
inline bool PoolGearTopUpSlot(bool hasItem, std::uint32_t quality, std::uint32_t itemLevel,
    std::uint32_t botLevel)
{
    if (!hasItem)
        return true;
    if (quality > 1)
        return false;
    return itemLevel + kPoolGearOutdatedItemLevelGap <= botLevel;
}
}
