#pragma once

#include <cstddef>
#include <string>

namespace TortoiseBots
{
namespace GuildOrders
{
namespace StoreColumns
{

// One entry per column the guild order load SELECT reads, in SELECT order.
// The load builds its statement from SelectList and reads each field through
// the Index constants below, so the column order lives in this one list; the
// list and the constants are pinned against each other by
// tools/test_guild_order_columns.cpp.
inline constexpr char const* kSelect[] = {
    "bot_guid",
    "guild_id",
    "giver_guid",
    "parent_bot",
    "root_giver",
    "type",
    "item_id",
    "amount",
    "hours",
    "delivered",
    "state",
    "reason",
    "farm_zone",
    "farm_chance",
    "farm_item",
    "zone_index",
    "deaths_in_zone",
    "worked_sec",
    "worked_total",
    "worked_nogain",
    "reported",
    "farm_rest",
    "keep",
    "gold_cap",
    "gold_cap_set",
    "guild_gold_withdrawn",
    "guild_gold_held",
    "cancelled",
    "supply_step",
    "supply_return_step",
    "supply_item",
    "supply_count",
    "supply_gold_count",
    "need_items",
    "need_whispered",
    "last_need_whisper",
    "need_bank_denied",
    "stock_target",
    "last_bank_check",
    "created_at",
    "updated_at",
    // The carry column came last: the columns read before it existed keep
    // the position they always had. `bring` and `given` append after it for
    // the same reason: every earlier index stays put.
    "carried",
    "bring",
    "given",
};

inline constexpr size_t kCount = sizeof(kSelect) / sizeof(kSelect[0]);

enum Index : size_t
{
    ColBotGuid = 0,
    ColGuildId,
    ColGiverGuid,
    ColParentBot,
    ColRootGiver,
    ColType,
    ColItemId,
    ColAmount,
    ColHours,
    ColDelivered,
    ColState,
    ColReason,
    ColFarmZone,
    ColFarmChance,
    ColFarmItem,
    ColZoneIndex,
    ColDeathsInZone,
    ColWorkedSec,
    ColWorkedTotal,
    ColWorkedNoGain,
    ColReported,
    ColFarmRest,
    ColKeep,
    ColGoldCap,
    ColGoldCapSet,
    ColGuildGoldWithdrawn,
    ColGuildGoldHeld,
    ColCancelled,
    ColSupplyStep,
    ColSupplyReturnStep,
    ColSupplyItem,
    ColSupplyCount,
    ColSupplyGoldCount,
    ColNeedItems,
    ColNeedWhispered,
    ColLastNeedWhisper,
    ColNeedBankDenied,
    ColStockTarget,
    ColLastBankCheck,
    ColCreatedAt,
    ColUpdatedAt,
    ColCarried,
    ColBring,
    ColGiven,
};

inline std::string SelectList()
{
    std::string out;
    for (size_t i = 0; i < kCount; ++i)
    {
        if (i)
            out += ", ";
        out += '`';
        out += kSelect[i];
        out += '`';
    }
    return out;
}

} // namespace StoreColumns
} // namespace GuildOrders
} // namespace TortoiseBots
