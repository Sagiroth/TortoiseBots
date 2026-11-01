#pragma once

// Sub-order legs change faster than the once-a-minute request cadence, so the
// throttle keys on the leg plus the order identity (parent, order item, type):
// a changed key logs at once, an unchanged key at most once a minute.
// Header-only and free of core/database types on purpose: exercised standalone
// in tools/test_guild_order_telemetry_policy.cpp.
#include "GuildOrderContract.h"
#include <cstdint>
#include <ctime>
#include <string>

namespace TortoiseBots
{
namespace GuildOrders
{

// SupplyStepToken(step) + ":" + std::to_string(itemId) -> e.g. "bank:2319", "-:0".
inline std::string OrderLegToken(SupplyStep step, uint32_t itemId)
{
    return std::string(SupplyStepToken(step)) + ":" + std::to_string(itemId);
}

// Parent GUID + order item + order type -> e.g. "0:2319:farm". A replacement
// order on the same bot starts a new identity even when the leg matches.
inline std::string OrderIdentityToken(uint32_t parentGuidLow, uint32_t orderItemId, OrderType type)
{
    return std::to_string(parentGuidLow) + ":" + std::to_string(orderItemId) + ":" + TypeToken(type);
}

// Cached throttle key: leg plus order identity, e.g. "bank:2319|0:2319:farm".
// A change in any part logs at once; an unchanged key at most once a minute.
inline std::string OrderRequestKey(SupplyStep step, uint32_t supplyItemId,
    uint32_t parentGuidLow, uint32_t orderItemId, OrderType type)
{
    return OrderLegToken(step, supplyItemId) + "|" + OrderIdentityToken(parentGuidLow, orderItemId, type);
}

// A row is due when the key changed since the last logged row, otherwise at
// most once a minute (today's cadence).
inline bool OrderRequestLogDue(std::string const& lastKey, std::string const& key, time_t nowSec, time_t nextDueSec)
{
    return key != lastKey || nowSec >= nextDueSec;
}

// Top-level orders carry no parent ("-"); a parent whose name lookup failed
// falls back to "?" like the store's BotName.
inline std::string OrderParentToken(uint32_t parentGuidLow, std::string const& parentName)
{
    if (parentGuidLow == 0)
        return "-";
    return parentName.empty() ? "?" : parentName;
}

} // namespace GuildOrders
} // namespace TortoiseBots
