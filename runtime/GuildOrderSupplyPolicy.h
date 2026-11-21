#pragma once

#include "GuildOrderContract.h"

namespace TortoiseBots
{
namespace GuildOrders
{

struct SupplyOptions
{
    bool bankEnabled = true;
    bool bankAttempted = false;
    bool auctionHouseAttempted = false;
    // The bot is standing in a running auction leg: it keeps its turn instead
    // of yielding to craft or farm.
    bool auctionHouseLive = false;
    bool selfCanCraft = false;
    bool selfCanFarm = false;
    bool subAttempted = false;
    // A craft reagent the bot could farm is first offered to a guild bot
    // that makes it; set once that offer found no maker (or failed).
    bool makerSubAttempted = false;
};
// The item the bot actually farms for its Working order: the supply leg's
// item while the ladder walks bank/AH/farm/bags, 0 on any other leg. A
// vendor-unlimited item travels for nothing (it is bought, never farmed),
// so it maps to 0 like a finished leg.
inline uint32_t SupplyFarmItemFor(SupplyStep step, uint32_t supplyItemId, OrderType type,
    OrderReason reason, bool supplyItemVendorUnlimited)
{
    (void)type;
    if (step != SupplyStep::Bank && step != SupplyStep::AuctionHouse && step != SupplyStep::Farm &&
        step != SupplyStep::Bags)
        return 0;
    // A vendor-unlimited item is bought, never farmed: the vendor leg runs
    // on Farm for craft reagents and farm-order items alike, so the wild
    // farm plan never walks for it unless the leg gave up on it.
    if (supplyItemVendorUnlimited && reason != OrderReason::NoSource)
        return 0;
    return supplyItemId;
}

inline SupplyStep NextSupplyStep(SupplyOptions const& options)
{
    if (options.bankEnabled && !options.bankAttempted)
        return SupplyStep::Bank;
    if (options.auctionHouseLive && !options.auctionHouseAttempted)
        return SupplyStep::AuctionHouse;
    if (options.selfCanCraft)
        return SupplyStep::Craft;
    if (!options.auctionHouseAttempted)
        return SupplyStep::AuctionHouse;
    // Farming is the bot's own last resort: a guild bot that makes the item
    // goes first. Handing a farm to another bot only pays when this one
    // cannot farm at all.
    if (options.selfCanFarm && !options.makerSubAttempted && !options.subAttempted)
        return SupplyStep::Sub;
    if (options.selfCanFarm)
        return SupplyStep::Farm;
    if (!options.subAttempted)
        return SupplyStep::Sub;
    return SupplyStep::Need;
}

// A sub-order the ladder places while the bot could still farm the item
// itself goes only to a bot that makes it.
inline bool SubOrderMakerOnly(SupplyOptions const& options)
{
    return options.selfCanFarm;
}

// The mailbox leg walks for the supply item (a reagent bought at the AH),
// not the order item: one source for the travel branch, the take-mail
// action and the travel-target expiry, so they never disagree on what waits.
inline uint32_t SupplyMailItemId(SupplyStep step, uint32_t supplyItemId, uint32_t orderItemId)
{
    return (step == SupplyStep::Bags && supplyItemId) ? supplyItemId : orderItemId;
}

// Consecutive empty or rejected travel searches for one supply leg. A new
// leg (or item) restarts the count; a landed search clears it in the store.
struct SupplySearchStreak
{
    SupplyStep step = SupplyStep::None;
    uint32_t itemId = 0;
    uint32_t fails = 0;
};

inline SupplySearchStreak NextSupplySearchStreak(SupplySearchStreak current, SupplyStep step,
    uint32_t itemId)
{
    if (step == current.step && itemId == current.itemId)
    {
        ++current.fails;
        return current;
    }
    current.step = step;
    current.itemId = itemId;
    current.fails = 1;
    return current;
}

inline uint32_t GuildGoldCapRemaining(uint32_t cap, bool hasCap, uint32_t withdrawn)
{
    if (!hasCap)
        return UINT32_MAX;
    return withdrawn >= cap ? 0 : cap - withdrawn;
}

inline uint32_t GuildGoldShortfall(uint32_t price, uint32_t ownedMoney, uint32_t heldGuildMoney,
    uint32_t capRemaining)
{
    uint64_t const available = uint64_t(ownedMoney) + heldGuildMoney;
    if (price <= available)
        return 0;
    uint64_t const shortfall = uint64_t(price) - available;
    return static_cast<uint32_t>(shortfall < capRemaining ? shortfall : capRemaining);
}

// Guild gold pays first: before a visit's purchases the bot tops up from the
// guild bank to cover the whole visit (within the cap), and its own gold
// pays only what the guild could not.
inline uint32_t GuildGoldFirstTopUp(uint32_t visitCost, uint32_t heldGuildMoney, uint32_t capRemaining)
{
    if (visitCost <= heldGuildMoney)
        return 0;
    uint32_t const wanted = visitCost - heldGuildMoney;
    return wanted < capRemaining ? wanted : capRemaining;
}

// Share of a purchase paid from the guild gold the bot holds.
inline uint32_t GuildGoldSpent(uint32_t price, uint32_t heldGuildMoney)
{
    return price < heldGuildMoney ? price : heldGuildMoney;
}

} // namespace GuildOrders
} // namespace TortoiseBots
