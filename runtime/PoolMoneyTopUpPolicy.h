#pragma once

#include <algorithm>
#include <cstdint>

namespace TortoiseBots
{
// Level-up money top-up for masterless pool bots (AiPlayerbot.PoolMoneyTopUp).
//
// Pool money is seeded once (PlayerbotFactory::SeedFreshMoney, 1 g x
// urand(1, 5 x level)) and every class rank is bought at a trainer, so a bot
// that spends its seed early stays broke: 121 of 504 pool bots held under 1 g
// and TrainerNoMoney fired 1,020 times on 164 bots in 16 h (Oct 2026 pool).
// mod-playerbots pays random bots a small stipend on every refresh
// (RandomPlayerbotMgr::Refresh); this module instead lifts a broke bot to
// what its next trainer visit needs, once per level.
//
// Pure functions, unit-tested in tools/test_pool_money_topup_policy.cpp.

// Upper bound of the floor per bot level: 1 g per level, the per-level unit
// of the fresh seed (whose median is about 2.5 g per level). A bot that
// skipped many trainer visits is lifted at most to this, never to the full
// backlog.
constexpr std::uint32_t kPoolMoneyTopUpCapPerLevel = 10000; // copper

// A real player's companions are never paid: hired companions, guild-claimed
// bots and any bot with a live player master keep the purse their player
// manages. Gold-cheat bots never run short, so they are skipped too.
inline bool PoolMoneyTopUpEligible(bool enabled, bool isRandomBot, bool isHired, bool isClaimed,
    bool hasRealPlayerMaster, bool hasGoldCheat)
{
    return enabled && isRandomBot && !isHired && !isClaimed && !hasRealPlayerMaster && !hasGoldCheat;
}

// Once per level: the last level handled is kept in memory (facade value).
inline bool PoolMoneyTopUpDue(std::uint32_t botLevel, std::uint32_t lastTopUpLevel)
{
    return botLevel != lastTopUpLevel;
}

// The floor: what the bot's next class-trainer visit costs with the reserves
// the trip gate keeps aside first (the module's "total money needed for"
// spells: repair, ammo, AH deposit, guild, then every trainable class rank),
// capped at kPoolMoneyTopUpCapPerLevel per level.
inline std::uint32_t PoolMoneyTopUpFloor(std::uint32_t trainerVisitNeed, std::uint32_t botLevel)
{
    std::uint64_t const cap = std::uint64_t(botLevel) * kPoolMoneyTopUpCapPerLevel;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(trainerVisitNeed, cap));
}

// Copper to add: up to the floor, never more, nothing when the purse already
// covers it.
inline std::uint32_t PoolMoneyTopUpAmount(std::uint32_t money, std::uint32_t floor)
{
    return floor > money ? floor - money : 0;
}
}
