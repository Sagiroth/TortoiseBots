#pragma once

#include "runtime/GuildOrderContract.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class Player;

namespace TortoiseBots
{
namespace GuildOrders
{

// In-memory order store backed by the characters database. Safe to call from
// any map thread; the map is locked, database writes go through the
// character database's own queue.
std::optional<OrderRecord> Find(uint32_t botGuidLow);

// All orders of one guild (the `.bot order list` reply), best-effort order.
std::vector<OrderRecord> ListForGuild(uint32_t guildId);

// Give or replace the bot's order. Returns false when the database write
// failed; the in-memory row is only replaced after the write succeeded.
bool Give(OrderRecord record);
// All orders, for the per-tick update. World thread only.
std::vector<OrderRecord> All();
// Remove the bot's order (cancel, character deleted, guild left).
bool Cancel(uint32_t botGuidLow);

// Remember the zone the bot farms in (0 = re-plan next time).
void SetFarmZone(uint32_t botGuidLow, uint32_t zoneId, uint8_t chance);

// Change state and reason; sends a live update to the giver when it changed.
void SetState(uint32_t botGuidLow, OrderState state, OrderReason reason);

// The bot brought home more of the item: reset the give-up clock for it.
// Returns true when the order moved to the second zone or ended as stuck.
bool NoteNewItems(uint32_t botGuidLow, uint32_t have);

// The bot died while the order ran.
void NoteDeath(uint32_t botGuidLow);

// Add worked seconds since the last call; runs the give-up rule.
void AddWorkedTime(uint32_t botGuidLow, uint32_t seconds);

// The bags filled up before the amount was reached: partial delivery.
void NoteBagsFull(uint32_t botGuidLow);

// What the buyer already delivered into the bank or by mail.
void AddDelivered(uint32_t botGuidLow, uint32_t count);

// Current progress: delivered plus the count of the item in the bot's bags.
// World thread only (reads the live Player when it is online).
uint32_t Progress(Player* bot, OrderRecord const& order);

// One TBM:ORDER line for the row (the list reply and live updates share it).
// World thread only.
std::string FormatOrderLine(uint32_t guildId, OrderRecord const& order);

// Live updates and startup. World thread only.
void SendLiveUpdate(OrderRecord const& order, uint32_t have);
void Initialize();
void Update(uint32_t diff);
void OnGuildDelMember(uint32_t botGuidLow);
void OnGuildDisband(uint32_t guildId);

} // namespace GuildOrders
} // namespace TortoiseBots
