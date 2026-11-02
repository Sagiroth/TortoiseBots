#pragma once

#include "runtime/GuildOrderContract.h"
#include "runtime/GuildOrderRelistPolicy.h"
#include "runtime/GuildOrderStepHistoryPolicy.h"
#include "runtime/GuildOrderSourcesPolicy.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
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

// Remove the bot's order (cancel, character deleted, guild left). Cancelling
// a parent removes its whole branch; children keep what they gathered (the
// bank deposit path stands alone per order).
bool Cancel(uint32_t botGuidLow);
// True when the item is reserved for the bot's unfinished order: the order
// item, the current supply item, or (craft orders) a known-recipe reagent
// from the stored protected set. A set lookup under the lock, no spell-map
// walk, safe on any thread (the world-thread market service included).
// A cancelled (parked refund stub) or finished order protects nothing, and a
// guild mismatch protects nothing either.
bool IsOrderProtectedItem(Player* bot, uint32_t itemId);
// Recompute the bot's stored protected set when its keys changed (order or
// supply item, craft flag). Call only on the bot's own map thread, where its
// spell map is safe to read (the supply-ladder refresh and the item-usage
// gate do this); the world thread never touches the spell map. No-op when
// the stored keys already match, and clears the set for cancelled, finished
// or absent orders. Only the finished set crosses the lock.
void RefreshOrderProtectedSet(Player* bot);
// A sub-order was placed or is already running: the giver becomes the
// parent bot, the row goes live to the root giver. Called from a bot's
// map-thread action; only the lock is touched there, the world thread does
// the player and planner work in Update.
bool RequestSupply(uint32_t parentBotGuidLow, uint32_t itemId, uint32_t count);
// The parent's sub-order for the item finished with its delivery in the
// guild bank and no row for the item still runs. True once per delivery:
// the call marks the rows handed over.
bool SubOrderDelivered(uint32_t parentBotGuidLow, uint32_t itemId);
// A working sub-order (or its whole branch, recursively) ended without
// delivering, or was cancelled: the parent gets the item on its need list. World thread only.
void NoteSubOrderFailed(uint32_t parentBotGuidLow, uint32_t itemId, uint32_t count);
// Move a sub-order to another guild bot (`.bot order reassign`), recomputing
// the order type for the new bot (craft when it knows the recipe, else
// farm). World thread only (the `.bot order` command).
bool ReassignSubOrder(uint32_t subOrderBotGuidLow, uint32_t newBotGuidLow, OrderType type);
// Bot guids of a branch (the bot itself plus every sub-order below it).
std::vector<uint32_t> BranchBots(uint32_t botGuidLow);
// Items already delegated to direct sub-orders (running rows and still
// queued requests): the craft ladder skips these when picking the next
// missing component, so one component never gets two sub-orders and the
// parent works or delegates the rest instead. Finished rows count while
// their delivery waits for the hand-over; handed-over ones do not, so a
// later shortage runs the ladder again. Best effort under one lock hold.
std::vector<uint32_t> CoveredSubOrderItems(uint32_t botGuidLow);
// Direct sub-order deliveries waiting for the hand-over (finished rows the
// parent has not collected yet): the craft ladder prefers these over
// covered items so a delivered sibling gets fetched instead of skipped.
std::vector<uint32_t> CollectableSubOrderItems(uint32_t botGuidLow);
// Durable module bots only for sub-order work: pool-account or claimed,
// never autonomous alts or hires. Same-guild membership is checked live at
// each call site (member rows plus GetGuildId()). World thread only.
bool IsDurableOrderBot(uint32_t guidLow);
// Whether a live bot can take a sub-order for the item: knows the recipe or
// has a usable farm plan. Reads only the passed player; the world-thread
// drain and the reassign path share it.
bool SubOrderCandidateUsable(Player* bot, uint32_t itemId, bool& canMake, bool& canFarm, bool& gathers);
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
// Put one item on the need list without parking: a craft order keeps working
// while the player owes it something. A working row drops its supply leg so
// the ladder re-plans around the item, clears any dead-end reason the leg
// left behind, and records whether the guild bank refused this order.
void AddNeedItem(uint32_t botGuidLow, uint32_t itemId, uint32_t count, bool bankDenied = false);
// Replace the list with the player's items the whole-tree walk still names
// (empty clears it), but only while the stored list still equals `expected`:
// the walk pruned a snapshot, and a merge or clear in between must win.
// Returns whether the write landed; a false leaves the caller's copy stale,
// so it re-reads the row. State and reason never change here: the ladder
// decides when to park.
bool ReplaceNeedItems(uint32_t botGuidLow, std::vector<NeedItem> const& expected,
    std::vector<NeedItem> const& items);
void RecordGuildGoldWithdrawal(uint32_t botGuidLow, uint32_t amount);
void ConsumeGuildGoldHeld(uint32_t botGuidLow, uint32_t amount);
// Guild gold still spendable anywhere on the order's branch: the smallest
// cap remaining over the row and every ancestor up to the root. Map thread
// only (the buy action reads it before bidding).
uint32_t BranchGoldCapRemaining(uint32_t botGuidLow);
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
// A mailbox search came back empty for the item: skip its mail leg until
// the cooldown. Map thread only (the travel pick); Update reports.
void NoteMailEmpty(uint32_t botGuidLow, uint32_t itemId);
void ClearMailEmpty(uint32_t botGuidLow);

// An order-leg travel search came back empty or rejected for a service leg
// (bags mailbox, bank, AH, vendor). Returns the consecutive count for that
// leg; the caller moves the ladder down once it reaches the farm-zone
// limit. A leg or item change restarts it, never a landed pick: the search
// layer re-picks between cities while the bot walks. Map thread only
// (travel pick).
uint32_t NoteSupplySearchFailed(uint32_t botGuidLow, SupplyStep step, uint32_t itemId);
// The auction leg commits to one city: the first pick stores its zone, later
// picks reuse it. A city change commits nothing, so flapping searches cannot
// dodge the visit count below. Map thread only (travel pick).
void CommitAuctionZone(uint32_t botGuidLow, uint32_t zoneId);
// An arrived auction-leg trip ended: with an auctioneer in reach the buy
// action owns the ladder and the count clears, otherwise it grows. Returns
// the consecutive count; the caller moves the ladder down at the visit
// limit. Map thread only (arrival).
uint32_t NoteArrivedAuctionVisit(uint32_t botGuidLow, bool auctioneerInReach);
// The auction leg has run this long without an auctioneer in reach: the
// caller hands off with OrderReason::NoAuctioneer. Starts the clock on the
// first call after the leg was entered and reports true once the deadline
// passed. Map thread only (the ladder refresh).
bool AuctionLegUnreachable(uint32_t botGuidLow, uint32_t nowMs);
// An auctioneer came into interact range on the auction leg: the reach clock
// clears, so a later arrival never trips the deadline. Map thread only.
void NoteAuctioneerInReach(uint32_t botGuidLow);
// An oversized listing was bought: remember what has to go back to the
// auction house. Memory only (a restart leaves the surplus to the delivery
// leftover path, which is the same fallback as a failed relist).
void NoteRelistSurplus(uint32_t botGuidLow, uint32_t itemId, uint32_t count, uint32_t unitPrice);
// The pending relist, if any (item 0 = none). Map thread only.
bool PendingRelist(uint32_t botGuidLow, uint32_t& itemId, uint32_t& count, uint32_t& unitPrice);
// The pending relist is over: posted, abandoned, or the leg handed off.
void ClearRelistSurplus(uint32_t botGuidLow);

// Relist ledger, persisted next to the order rows: one row per surplus stack
// the order posted on the auction house. A sale or an expired listing comes
// back by mail, often after the order is gone, and its value belongs to the
// guild until the bank trip pays it back.
// A relist was posted, with the listing terms the auction mail reports back
// and its duration (the row's expiry is the posting plus that duration).
// Map thread only (the buy action).
void NoteRelistPosted(uint32_t botGuidLow, uint32_t guildId, uint32_t itemId, uint32_t count,
    uint32_t unitPrice, uint32_t bid, uint32_t buyout, uint32_t deposit, uint32_t durationSec);
// The bot's ledger rows, oldest first. Any thread.
std::vector<RelistLedgerEntry> RelistEntries(uint32_t botGuidLow);
// A matched relist mail was collected: the gold or the item is in hand.
// Map thread only (the mail read).
bool NoteRelistReceived(uint32_t botGuidLow, uint32_t seq, uint32_t gold, uint32_t items);
// Writes one ledger row to the table and to memory. Returns false, with memory
// untouched, when the table write failed. World thread only (the relist drain).
bool SaveRelistRow(RelistLedgerEntry const& row);
// Drops one ledger row the same way. World thread only (the relist drain).
bool DeleteRelistRow(uint32_t botGuidLow, uint32_t seq);
// Value collected from a relist mail is due to the guild bank. Any thread.
bool RelistDepositPending(uint32_t botGuidLow);
// The received rows a bank trip has to pay out, oldest first. World thread
// only (the relist drain).
std::vector<RelistLedgerEntry> RelistDeposits(uint32_t botGuidLow);

// Drop parked refund stubs whose gold is back. World thread only (Update).
void FinishRepaid();
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

// The farm leg's travel probe found no route to one target point in the
// planned zone (a mesh hole or a partial path): remember that point per bot
// and zone, so the plan leaves the zone out once two different points in it
// failed inside kFarmZoneUnreachableMs. Only the zone the order currently
// farms and a Farm supply step count. Map thread only (travel move).
void NoteFarmZoneTargetFailed(uint32_t botGuidLow, uint32_t zoneId, uint32_t mapId,
    float x, float y, float z);
// The failed farm-leg travel targets remembered for the zone, policy-shaped:
// the planner asks FarmZoneSkipped whether enough of them are still fresh.
std::vector<FarmZoneTargetFailure> FarmZoneFailures(uint32_t botGuidLow,
    uint32_t zoneId);

// An order-leg search came back rejected or empty in the planned zone.
// Counts consecutive failures there; the third moves the order to the next
// plan zone through the zone-move path. World thread only (travel pick).
void NoteSearchFailed(uint32_t botGuidLow, uint32_t zoneId);

// An order-leg zone search landed: the consecutive-failure count for the
// zone resets. Farm only; the leg streak restarts on a leg change instead.
// World thread only (travel pick).
void NoteSearchLanded(uint32_t botGuidLow);

// The bags filled up before the amount was reached: partial delivery.
void NoteBagsFull(uint32_t botGuidLow);

// What the bot already delivered into the bank or by mail.
void AddDelivered(uint32_t botGuidLow, uint32_t count);

// What the bot carries for the order but has not delivered: bags plus
// mailbox. World thread only. A live Player is read; an offline bot reports
// the last carry Tick stored on its row, so the list reply never claims zero.
uint32_t Carry(Player* bot, OrderRecord const& order);

// One TBM:ORDER line for the row (the list reply and live updates share it).
// World thread only. The caller passes the reported have count, which never
// drops (offline bots report max(delivered, last reported)), and the carry:
// bags plus mailbox not yet delivered.
std::string FormatOrderLine(uint32_t guildId, OrderRecord const& order, uint32_t have, uint32_t carry);

// Step history of the bot's running or finished order (memory only, bounded);
// nullopt for no order or a cancelled stub.
std::optional<OrderStepHistory> FindStepHistory(uint32_t botGuidLow);

// Live updates and startup. World thread only.
void Initialize();
void Update(uint32_t diff);
void OnGuildDelMember(uint32_t botGuidLow);
void OnGuildDisband(uint32_t guildId);

} // namespace GuildOrders
} // namespace TortoiseBots
