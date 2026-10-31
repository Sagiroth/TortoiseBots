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
    bool selfCanCraft = false;
    bool selfCanFarm = false;
    bool subAttempted = false;
};

inline SupplyStep NextSupplyStep(SupplyOptions const& options)
{
    if (options.bankEnabled && !options.bankAttempted)
        return SupplyStep::Bank;
    if (!options.auctionHouseAttempted)
        return SupplyStep::AuctionHouse;
    if (options.selfCanCraft)
        return SupplyStep::Craft;
    if (options.selfCanFarm)
        return SupplyStep::Farm;
    if (!options.subAttempted)
        return SupplyStep::Sub;
    return SupplyStep::Need;
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

} // namespace GuildOrders
} // namespace TortoiseBots
