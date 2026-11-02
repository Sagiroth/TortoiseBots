#pragma once

#include <cstdint>

namespace TortoiseBots
{
namespace GuildOrders
{

// World-thread drain for queued guild-order deliveries. Called from the
// world-thread order update, after the store tick.
void UpdateDeliveries();

void UpdateSupplyWithdrawals();
void OnSupplyOrderGone(uint32_t botGuidLow);
// Refund queued unspent guild gold on the world thread. Runs after
// UpdateSupplyWithdrawals from the order update.
void UpdateGoldRefunds();
// Pay collected relist ledger value back to the guild bank on the world
// thread. Runs after UpdateGoldRefunds from the order update; an entry whose
// bank visit moved nothing stays for the next one.
void UpdateRelistDeposits();

// Forget a bot's empty-pass count when its order is gone (cancel, guild
// left, character deleted).
void OnDeliveryOrderGone(uint32_t botGuidLow);

// Expire the bot's forced guild-order travel target, if any. World thread
// only. Called on every leg change (Working <-> Delivering, Delivering ->
// Done/Mailed) so a forced target of the wrong leg cannot pin the bot.
void ExpireGuildOrderTarget(uint32_t botGuidLow);

} // namespace GuildOrders
} // namespace TortoiseBots
