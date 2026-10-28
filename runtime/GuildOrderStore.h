#pragma once

#include "runtime/GuildOrderContract.h"

#include <optional>

namespace TortoiseBots
{
namespace GuildOrders
{

// In-memory order store backed by the characters database. Safe to call from
// any map thread; database writes are asynchronous.
std::optional<OrderRecord> Find(uint32_t botGuidLow);

// Remember the zone the bot farms in (0 = re-plan next time).
void SetFarmZone(uint32_t botGuidLow, uint32_t zoneId, uint8_t chance);

// Change state and reason; sends a live update to the giver when it changed.
void SetState(uint32_t botGuidLow, OrderState state, OrderReason reason);

} // namespace GuildOrders
} // namespace TortoiseBots
