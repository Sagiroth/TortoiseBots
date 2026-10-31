#pragma once

#include <cstdint>

namespace TortoiseBots
{
namespace GuildOrders
{

// World-thread drain for queued guild-order deliveries. Called from the
// world-thread order update, after the store tick.
void UpdateDeliveries();

// Drain queued guild-bank reagent and money withdrawals on the world thread.
void UpdateSupplyWithdrawals();
void OnSupplyOrderGone(uint32_t botGuidLow);

// Forget a bot's empty-pass count when its order is gone (cancel, guild
// left, character deleted).
void OnDeliveryOrderGone(uint32_t botGuidLow);

// Expire the bot's forced guild-order travel target, if any. World thread
// only. Called on every leg change (Working <-> Delivering, Delivering ->
// Done/Mailed) so a forced target of the wrong leg cannot pin the bot.
void ExpireGuildOrderTarget(uint32_t botGuidLow);

} // namespace GuildOrders
} // namespace TortoiseBots
