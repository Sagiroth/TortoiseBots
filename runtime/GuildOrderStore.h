#pragma once

#include "runtime/GuildOrderContract.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class Player;
struct ItemPrototype;

namespace TortoiseBots
{
namespace GuildOrders
{

// In-memory order store backed by the characters database. The map-thread
// mutators below only change the record under the lock and mark it dirty;
// only Update on the world thread looks up players, counts bag items and
// sends live updates.
std::optional<OrderRecord> Find(uint32_t botGuidLow);

// False before the startup load ran: an order lookup before then misses rows
// that are still only in the table.
bool IsAvailable();
// True when the item can go to the guild bank (search and give share it).
bool IsOrderableItem(ItemPrototype const* proto);

// All orders of one guild (the `.bot order list` reply), best-effort order.
std::vector<OrderRecord> ListForGuild(uint32_t guildId);

// Give or replace the bot's order. Returns false when the database write
// failed; the in-memory row is only replaced after the write succeeded.
// World thread only (the `.bot order` command).
bool Give(OrderRecord record);

// All orders, for the per-tick update. World thread only.
std::vector<OrderRecord> All();

// Remove the bot's order (cancel, character deleted, guild left).
bool Cancel(uint32_t botGuidLow);
// Remember the zone the bot farms in (0 = re-plan next time), with its
// position in the current farm plan and the item actually farmed there (the
// order item, or the reagent of the current craft leg; 0 = the order item).
void SetFarmZone(uint32_t botGuidLow, uint32_t zoneId, uint8_t chance, uint32_t zoneIndex, uint32_t farmItemId);

// Change state and reason (the next Update reports it to the giver).
void SetState(uint32_t botGuidLow, OrderState state, OrderReason reason);

// Current supply-ladder leg and item. Mutators may be called from a bot's
// map-thread action; Update reports the change on the world thread.
void SetSupply(uint32_t botGuidLow, SupplyStep step, uint32_t itemId, uint32_t count,
    SupplyStep returnStep = SupplyStep::None, OrderReason reason = OrderReason::None, uint32_t goldCount = 0);
void SetNeedItems(uint32_t botGuidLow, std::vector<NeedItem> const& items, bool bankDenied = false);
void RecordGuildGoldWithdrawal(uint32_t botGuidLow, uint32_t amount);
void ConsumeGuildGoldHeld(uint32_t botGuidLow, uint32_t amount);
// A refund pass moved `deposited` copper of held guild gold back into the
// bank. Order-caused inflow per item for the leftover cap. World thread
// only (the drains).
void RefundGuildGold(uint32_t botGuidLow, uint32_t deposited);
void NoteSupplyGain(uint32_t botGuidLow, uint32_t itemId, uint32_t count);
void NoteSupplySpent(uint32_t botGuidLow, uint32_t itemId, uint32_t count);
// A bank, AH, mail or vendor pickup landed: the no-progress watchdog starts
// over, like any farmed item. World thread only (the drains) or the bot's
// map-thread action.
void NoteSupplyProgress(uint32_t botGuidLow);
// A banker visit found none of the item: skip the bank leg for it until the
// cooldown. A move clears the marker. World thread only (the drains).
void NoteBankEmpty(uint32_t botGuidLow, uint32_t itemId);
void ClearBankEmpty(uint32_t botGuidLow);
// Drop parked refund stubs whose gold is back. World thread only (Update).
void FinishRepaid();
// A sub-order was placed or already exists. Worker C replaces the separate
// false stub with the hierarchy-aware store implementation when merging.
bool RequestSupply(uint32_t parentBotGuidLow, uint32_t itemId, uint32_t count);

// A keep round landed in the bank: the order watches it from now on. Sets
// the stocked state, the stock target back as the round's quota, and the
// last bank look to now so the first re-check waits a full interval.
// World thread only (the delivery drain).
void StartKeepWatch(uint32_t botGuidLow);

// Drop the keep flag (a round that ended without a bank, or without skill):
// the order ends like a one-shot one. World thread only.
void DropKeep(uint32_t botGuidLow);

// The buy order's auction attempt is over (no gold, no listings): farm what
// is still missing. Keeps type Buy; GuildOrderValue maps it to Farm.
void NoteFarmRest(uint32_t botGuidLow);

// The bot died while the order ran. Only deaths in Working count.
void NoteDeath(uint32_t botGuidLow);

// An order-leg search came back rejected or empty in the planned zone.
// Counts consecutive failures there; the third moves the order to the next
// plan zone through the zone-move path. World thread only (travel pick).
void NoteSearchFailed(uint32_t botGuidLow, uint32_t zoneId);

// An order-leg search landed: the consecutive-failure count for the zone
// resets. World thread only (travel pick).
void NoteSearchLanded(uint32_t botGuidLow);

// The bags filled up before the amount was reached: partial delivery.
void NoteBagsFull(uint32_t botGuidLow);

// What the bot already delivered into the bank or by mail.
void AddDelivered(uint32_t botGuidLow, uint32_t count);

// Current progress: delivered plus bags plus order items in the mailbox.
// World thread only (reads the live Player when it is online).
uint32_t Progress(Player* bot, OrderRecord const& order);

// One TBM:ORDER line for the row (the list reply and live updates share it).
// World thread only. The caller passes the reported have count, which never
// drops (offline bots report max(delivered, last reported)).
std::string FormatOrderLine(uint32_t guildId, OrderRecord const& order, uint32_t have);

// Live updates and startup. World thread only.
void Initialize();
void Update(uint32_t diff);
void OnGuildDelMember(uint32_t botGuidLow);
void OnGuildDisband(uint32_t guildId);

} // namespace GuildOrders
} // namespace TortoiseBots
