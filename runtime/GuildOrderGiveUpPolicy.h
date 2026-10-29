#pragma once

// Farm-time rules for guild orders: deaths never end an order, they move the
// bot to the next zone of its plan, and so does an hour worked without a new
// item. The order itself ends once its chosen farm time runs out. Header-only
// and free of core/database types on purpose: the thresholds are exercised
// standalone in tools/test_guild_order_giveup_policy.cpp. The order store
// feeds this function plain counters it already keeps per order.
#include <cstddef>
#include <cstdint>

namespace TortoiseBots
{
namespace GuildOrders
{

// An hour of worked time without a new item, or three deaths in the current
// farm zone, moves the bot to the next zone of the plan (wrapping around to
// the first when the list is exhausted). Only time the bot actually works
// counts: not while it waits in a group, not while delivering, not while
// offline.
inline constexpr uint32_t kZoneMoveWorkedSeconds = 3600;
inline constexpr uint32_t kZoneMoveDeaths = 3;

// Player-chosen farm time on `.bot order ... [hours]`: whole hours.
inline constexpr uint32_t kMinOrderHours = 1;
inline constexpr uint32_t kMaxOrderHours = 24;
inline constexpr uint32_t kDefaultOrderHours = 1;

enum class ZoneMoveTrigger : uint8_t
{
    None,
    NoProgress, // an hour worked in this zone without a new item
    Deaths,     // three deaths in this zone: the zone is too dangerous
};

inline ZoneMoveTrigger ClassifyZoneMove(uint32_t workedSinceNewItemSec, uint32_t deathsInZone)
{
    if (deathsInZone >= kZoneMoveDeaths)
        return ZoneMoveTrigger::Deaths;
    if (workedSinceNewItemSec >= kZoneMoveWorkedSeconds)
        return ZoneMoveTrigger::NoProgress;
    return ZoneMoveTrigger::None;
}

// Next zone of the plan after a move, re-plan or outgrown zone: the zone
// after the listed one, or the stored index when the current zone is cleared
// (a farm-rule move bumps the stored index first) or no longer planned.
// Wraps around to the first when the list is exhausted; the caller treats
// an empty plan as no source.
inline size_t NextZonePick(bool listed, size_t listedIndex, uint32_t zoneIndex, size_t zoneCount)
{
    if (zoneCount == 0)
        return 0;
    size_t start = listed ? listedIndex + 1 : zoneIndex;
    return start % zoneCount;
}

// Consecutive rejected or empty order-leg searches in the planned zone move
// the bot on: a zone whose spots never come active costs a few short parks,
// not the hour the death/no-progress rules would take to notice.
inline constexpr uint32_t kSearchFailZoneMove = 3;
inline constexpr uint32_t kOrderLegParkSeconds = 15;

// Whether the failed-search count moves the zone. Counts only consecutive
// failures in one zone: a landed search resets it.
inline bool SearchFailsMoveZone(uint32_t searchFailsInZone)
{
    return searchFailsInZone >= kSearchFailZoneMove;
}

// The order ends once the bot has worked its chosen hours in total without
// reaching the amount. Out-of-range hours fall back to the default.
inline bool IsOrderTimeout(uint32_t workedTotalSec, uint32_t hours)
{
    if (hours < kMinOrderHours || hours > kMaxOrderHours)
        hours = kDefaultOrderHours;
    return workedTotalSec >= hours * 3600;
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
// the zone-move clock. A login after an offline gap never counts as new items.
inline bool IsNewItemRise(bool botOnline, bool sawOnline, uint32_t bagCount, uint32_t lastBagCount)
{
    return botOnline && sawOnline && bagCount > lastBagCount;
}

} // namespace GuildOrders
} // namespace TortoiseBots
