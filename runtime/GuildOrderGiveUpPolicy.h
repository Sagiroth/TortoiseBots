#pragma once

// Give-up rule for guild orders: a bot that works a zone without progress
// moves once to the second-best zone, then stops. Header-only and free of
// core/database types on purpose: the thresholds are exercised standalone in
// tools/test_guild_order_giveup_policy.cpp. The order store feeds this
// function plain counters it already keeps per order.
#include <cstdint>

namespace TortoiseBots
{
namespace GuildOrders
{

// An hour of worked time without a new item, or five deaths during the order,
// ends the current attempt. Only time the bot actually works counts: not
// while it waits in a group, not while delivering, not while offline.
inline constexpr uint32_t kGiveUpWorkedSeconds = 3600;
inline constexpr uint32_t kGiveUpDeaths = 5;

enum class GiveUpAction : uint8_t
{
    KeepWorking,
    MoveZone,   // first threshold hit: re-plan to the second-best zone
    GiveUp,     // second hit: the order ends as cant / stuck
};

inline GiveUpAction ClassifyGiveUp(uint32_t workedSinceNewItemSec, uint32_t deaths, bool movedZone)
{
    if (workedSinceNewItemSec < kGiveUpWorkedSeconds && deaths < kGiveUpDeaths)
        return GiveUpAction::KeepWorking;
    return movedZone ? GiveUpAction::GiveUp : GiveUpAction::MoveZone;
}

// Reported progress never drops: offline (or not yet re-seen) bots keep
// max(delivered, last reported).
inline uint32_t ReportedHave(uint32_t delivered, uint32_t bagCount, bool botOnline, uint32_t reportedHave)
{
    uint32_t have = delivered + (botOnline ? bagCount : 0);
    uint32_t floor = delivered > reportedHave ? delivered : reportedHave;
    return have > floor ? have : floor;
}

// A real rise in bag count seen while online on consecutive updates resets
// the give-up clock. A login after an offline gap never counts as new items.
inline bool IsNewItemRise(bool botOnline, bool sawOnline, uint32_t bagCount, uint32_t lastBagCount)
{
    return botOnline && sawOnline && bagCount > lastBagCount;
}

} // namespace GuildOrders
} // namespace TortoiseBots
