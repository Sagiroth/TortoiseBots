#include "GuildOrderStore.h"
#include "GuildOrderStoreColumns.h"

#include "GuildOrderAuctionPolicy.h"
#include "GuildOrderBringPolicy.h"
#include "GuildOrderCancelDepositPolicy.h"
#include "GuildOrderDelivery.h"
#include "GuildOrderGiveUpPolicy.h"
#include "GuildOrderKeepPolicy.h"
#include "GuildOrderNeedPolicy.h"
#include "GuildOrderRelistPolicy.h"
#include "GuildOrderSourcesPolicy.h"
#include "GuildOrderSupplyPolicy.h"
#include "GuildOrderSubOrderPolicy.h"
#include "GuildOrderProtectedItems.h"
#include "../commands/CommandShared.h"
#include "../host/ModuleLog.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/GuildOrderSources.h"
#include "../ai/playerbot/GuildOrderCraft.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/RandomBotFacade.h"
#include "../ai/playerbot/strategy/values/ItemUsageValue.h"
#include "../runtime/AltBotService.h"
#include "../runtime/BotManager.h"
#include "../runtime/ClaimLifecycle.h"
#include "../runtime/HireLifecycle.h"
#include "../runtime/RandomBotAccountRegistry.h"
#include "PlayerbotAIStorage.h"
#include "Database/DatabaseEnv.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "DBCStores.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Guild/GuildMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Mail/Mail.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "MapNodes/MasterPlayer.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace TortoiseBots
{
namespace GuildOrders
{
namespace
{
using ::ai::FindCraftSpellFor;
using ::ai::PlanGuildOrderSources;

// Progress lines at most every 5 s per order; state changes always go out.
inline constexpr uint32_t kProgressReportMs = 5000;

// Progress scans at most every 5 s per order: the world thread still ticks
// the worked clocks every pass, but bag and mailbox walks (per item, per
// reagent) only run on the cadence the progress lines report on.
inline bool ProgressScanDue(uint32_t nowMs, uint32_t lastScanMs)
{
    return nowMs - lastScanMs >= kProgressReportMs;
}

// True when an npc_vendor row sells the item with no stock limit. Template
// rows are skipped: they need the creature's vendor_id, which the store
// never reads; the AI-side checks (ChooseSourceStep, vendor action) cover
// the template case, so the store only guards the direct rows.
inline bool VendorSellsUnlimited(uint32_t itemId)
{
    if (!itemId)
        return false;
    // Item ids an npc_vendor row sells with no stock limit, built once per
    // process (startup scan of the vendor stock, one pass). The ladder never
    // delegates these to a sub-order: the crafter buys them at a vendor.
    static std::unordered_set<uint32_t> const items = []()
    {
        std::unordered_set<uint32_t> out;
        for (uint32_t entry = 0; entry < sCreatureStorage.GetMaxEntry(); ++entry)
        {
            VendorItemData const* vendorItems = sObjectMgr.GetNpcVendorItemList(entry);
            if (!vendorItems)
                continue;
            for (VendorItem const* vendorItem : vendorItems->m_items)
                if (vendorItem && vendorItem->item && vendorItem->maxcount == 0)
                    out.insert(vendorItem->item);
        }
        return out;
    }();
    return items.count(itemId) != 0;
}

// Worked seconds live in memory; the database sees them at most this often.
inline constexpr uint32_t kWorkedFlushMs = 60000;
inline constexpr uint32_t kNeedBankCheckMs = 10000;

class OrderStore
{
public:
    static OrderStore& Instance()
    {
        static OrderStore instance;
        return instance;
    }
    // A branch cancel (or a parent ending) that removes a child row with its
    // quota already in the bags: detach the child as a live top-level order
    // set to delivering, so the bank deposit path (travel, banker, drain)
    // still runs and the guild keeps the finished items. World thread only,
    // with the store mutex held; the bag count is read before the lock (the
    // world thread owns player lookups, the mutex never covers them).
    static bool DetachChildDepositLocked(OrderRecord& record, uint32_t bagCount)
    {
        if (record.parentBotGuidLow == 0 || record.cancelled || !record.itemId || !record.amount)
            return false;
        if (!CancelledChildDeposits(record.state, record.reason, record.delivered, bagCount, 0, record.amount))
            return false;
        record.parentBotGuidLow = 0;
        record.state = OrderState::Delivering;
        record.reason = OrderReason::None;
        record.supplyStep = SupplyStep::Deliver;
        record.supplyItemId = record.itemId;
        record.supplyCount = 0;
        record.supplyGoldCount = 0;
        record.supplyReturnStep = SupplyStep::None;
        return true;
    }


    // Row write outside the store mutex (the caller snapshotted it out of
    // Tick): a failed write re-marks the row dirty for the next pass.
    void WriteAndRescue(OrderRecord const& record)
    {
        if (TryWriteRecord(record))
            return;
        MarkDirty(record.botGuidLow);
    }

    void Initialize()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_initialized)
            return;

        m_orders.clear();

        std::string select = "SELECT ";
        select += StoreColumns::SelectList();
        select += " FROM `tortoise_bots_guild_order`";
        std::unique_ptr<QueryResult> result(CharacterDatabase.Query(select.c_str()));
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                OrderRecord record;
                record.botGuidLow = fields[StoreColumns::ColBotGuid].GetUInt32();
                record.guildId = fields[StoreColumns::ColGuildId].GetUInt32();
                record.giverGuidLow = fields[StoreColumns::ColGiverGuid].GetUInt32();
                record.parentBotGuidLow = fields[StoreColumns::ColParentBot].GetUInt32();
                record.rootGiverGuidLow = fields[StoreColumns::ColRootGiver].GetUInt32();
                ParseType(fields[StoreColumns::ColType].GetCppString(), record.type);
                record.itemId = fields[StoreColumns::ColItemId].GetUInt32();
                record.amount = fields[StoreColumns::ColAmount].GetUInt32();
                record.hours = fields[StoreColumns::ColHours].GetUInt32();
                record.delivered = fields[StoreColumns::ColDelivered].GetUInt32();
                ParseState(fields[StoreColumns::ColState].GetCppString(), record.state);
                ParseReason(fields[StoreColumns::ColReason].GetCppString(), record.reason);
                record.farmZoneId = fields[StoreColumns::ColFarmZone].GetUInt32();
                record.farmChance = static_cast<uint8_t>(fields[StoreColumns::ColFarmChance].GetUInt32());
                record.farmItemId = fields[StoreColumns::ColFarmItem].GetUInt32();
                record.zoneIndex = fields[StoreColumns::ColZoneIndex].GetUInt32();
                record.deathsInZone = fields[StoreColumns::ColDeathsInZone].GetUInt32();
                record.workedSinceNewItemSec = fields[StoreColumns::ColWorkedSec].GetUInt32();
                record.workedTotalSec = fields[StoreColumns::ColWorkedTotal].GetUInt32();
                record.workedNoGainSec = fields[StoreColumns::ColWorkedNoGain].GetUInt32();
                record.reportedHave = fields[StoreColumns::ColReported].GetUInt32();
                record.farmRest = fields[StoreColumns::ColFarmRest].GetUInt32() != 0;
                record.keep = fields[StoreColumns::ColKeep].GetUInt32() != 0;
                record.goldCap = fields[StoreColumns::ColGoldCap].GetUInt32();
                record.hasGoldCap = fields[StoreColumns::ColGoldCapSet].GetUInt32() != 0;
                record.guildGoldWithdrawn = fields[StoreColumns::ColGuildGoldWithdrawn].GetUInt32();
                record.guildGoldHeld = fields[StoreColumns::ColGuildGoldHeld].GetUInt32();
                record.cancelled = fields[StoreColumns::ColCancelled].GetUInt32() != 0;
                ParseSupplyStep(fields[StoreColumns::ColSupplyStep].GetCppString(), record.supplyStep);
                ParseSupplyStep(fields[StoreColumns::ColSupplyReturnStep].GetCppString(), record.supplyReturnStep);
                record.supplyItemId = fields[StoreColumns::ColSupplyItem].GetUInt32();
                record.supplyCount = fields[StoreColumns::ColSupplyCount].GetUInt32();
                record.supplyGoldCount = fields[StoreColumns::ColSupplyGoldCount].GetUInt32();
                record.needItems = ParseNeedItems(fields[StoreColumns::ColNeedItems].GetCppString());
                record.needWhispered = fields[StoreColumns::ColNeedWhispered].GetUInt32() != 0;
                record.lastNeedWhisper = fields[StoreColumns::ColLastNeedWhisper].GetCppString();
                record.needBankDenied = fields[StoreColumns::ColNeedBankDenied].GetUInt32() != 0;
                record.stockTarget = fields[StoreColumns::ColStockTarget].GetUInt32();
                record.lastBankCheckMs = fields[StoreColumns::ColLastBankCheck].GetUInt32();
                record.createdAt = fields[StoreColumns::ColCreatedAt].GetUInt64();
                record.updatedAt = fields[StoreColumns::ColUpdatedAt].GetUInt64();
                record.reportedCarry = fields[StoreColumns::ColCarried].GetUInt32();
                record.bringItems = ParseBringItemsToken(fields[StoreColumns::ColBring].GetCppString());
                if (record.hours != kUnlimitedOrderHours &&
                    (record.hours < kMinOrderHours || record.hours > kMaxOrderHours))
                    record.hours = kDefaultOrderHours;
                if (!record.rootGiverGuidLow && !record.parentBotGuidLow)
                    record.rootGiverGuidLow = record.giverGuidLow;
                if (record.botGuidLow && record.itemId && record.amount)
                    m_orders[record.botGuidLow] = Stored{record};
            } while (result->NextRow());
        }

        // Relist ledger rows ride with the order store: a sale or an expired
        // listing may land long after the order row is gone, and the value
        // still belongs to the guild until the bank trip pays it back.
        m_relist.clear();
        std::unique_ptr<QueryResult> relists(CharacterDatabase.Query(
            "SELECT `bot_guid`, `seq`, `guild_id`, `item_id`, `count`, `unit_price`, `bid`, "
            "`buyout`, `deposit`, `posted_at`, `expires_at`, `state`, `claim_mail`, `held_gold`, "
            "`held_count` FROM `tortoise_bots_guild_relist`"));
        size_t relistCount = 0;
        if (relists)
        {
            do
            {
                Field* fields = relists->Fetch();
                RelistLedgerEntry entry;
                entry.botGuidLow = fields[0].GetUInt32();
                entry.seq = fields[1].GetUInt32();
                entry.guildId = fields[2].GetUInt32();
                entry.itemId = fields[3].GetUInt32();
                entry.count = fields[4].GetUInt32();
                entry.unitPrice = fields[5].GetUInt32();
                entry.bid = fields[6].GetUInt32();
                entry.buyout = fields[7].GetUInt32();
                entry.deposit = fields[8].GetUInt32();
                entry.postedAtSec = fields[9].GetUInt64();
                entry.expiresAtSec = fields[10].GetUInt64();
                entry.state = RelistStateFromDb(fields[11].GetUInt32());
                entry.claimMailId = fields[12].GetUInt32();
                entry.heldGold = fields[13].GetUInt32();
                entry.heldCount = fields[14].GetUInt32();
                if (!entry.botGuidLow || !entry.seq || !entry.itemId || !entry.count)
                    continue;
                m_relist[entry.botGuidLow].push_back(entry);
                ++relistCount;
            } while (relists->NextRow());
        }

        m_initialized = true;
        TB_LOG_BASIC("TortoiseBots: loaded %zu guild order(s) from database.", m_orders.size());
        if (relistCount)
            TB_LOG_BASIC("TortoiseBots: loaded %zu guild relist ledger row(s).", relistCount);
    }

    // The pool reads the store before the startup sequence may have loaded
    // it; an unloaded store reports empty instead of the table rows.
    bool IsAvailable() { return m_initialized; }
    std::optional<OrderRecord> Find(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return std::nullopt;
        return it->second.record;
    }
    // The addon timeline: a copy of the row's step history under the lock, or
    // nullopt for no order or a cancelled stub.
    std::optional<OrderStepHistory> FindStepHistory(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return std::nullopt;
        if (it->second.record.cancelled)
            return std::nullopt;
        return it->second.steps;
    }
    // True when the item is reserved for the bot's unfinished order: the
    // live order and supply items always, plus the stored reagent set, even
    // a stale one until the map thread recomputes it: over-protecting for a
    // tick is harmless, a gap would let the world-thread AH list a reagent.
    // A set lookup under the lock, safe on any thread; cancelled or finished
    // rows protect nothing.
    bool IsProtectedItem(Player* bot, uint32_t itemId)
    {
        if (!bot || !itemId || !bot->GetGuildId())
            return false;
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(bot->GetGUIDLow());
        if (it == m_orders.end())
            return false;
        Stored& stored = it->second;
        OrderRecord const& record = stored.record;
        if (record.cancelled || record.guildId != bot->GetGuildId() || IsFinished(record.state))
            return false;
        if (itemId == record.itemId || itemId == record.supplyItemId)
            return true;
        OrderProtectedSet const& kept = stored.kept;
        if (!kept.valid)
            return false;
        return kept.items.count(itemId) != 0;
    }

    // Recompute the stored set on the bot's own map thread (the spell map
    // is safe to read there). Snapshots the keys under the lock, walks the
    // tree outside it, then swaps in the finished set under the lock with
    // a key re-check. No-op when the keys already match; clears the set
    // for cancelled, finished or absent rows.
    void RefreshProtectedSet(Player* bot)
    {
        if (!bot || !bot->GetGuildId())
            return;
        uint32_t botGuidLow = bot->GetGUIDLow();
        uint32_t orderItemId = 0;
        uint32_t supplyItemId = 0;
        bool orderIsCraft = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_orders.find(botGuidLow);
            if (it == m_orders.end())
                return;
            OrderRecord const& record = it->second.record;
            if (record.cancelled || record.guildId != bot->GetGuildId() || IsFinished(record.state))
            {
                it->second.kept = OrderProtectedSet{};
                return;
            }
            if (it->second.kept.valid && it->second.kept.orderItemId == record.itemId &&
                it->second.kept.supplyItemId == record.supplyItemId &&
                it->second.kept.orderIsCraft == (record.type == OrderType::Craft))
                return;
            orderItemId = record.itemId;
            supplyItemId = record.supplyItemId;
            orderIsCraft = record.type == OrderType::Craft;
        }
        if (!orderItemId)
            return;
        auto reagentsFor = [bot](uint32_t parent) { return ::ai::KnownReagentsFor(bot, parent); };
        std::unordered_set<uint32_t> kept;
        CollectOrderProtectedItems(orderItemId, false, orderIsCraft, supplyItemId, reagentsFor,
            [](uint32_t itemId) { return VendorSellsUnlimited(itemId); }, kept);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_orders.find(botGuidLow);
            if (it == m_orders.end())
                return;
            OrderRecord const& record = it->second.record;
            if (record.cancelled || record.guildId != bot->GetGuildId() || IsFinished(record.state))
            {
                it->second.kept = OrderProtectedSet{};
                return;
            }
            if (record.itemId != orderItemId || record.supplyItemId != supplyItemId ||
                (record.type == OrderType::Craft) != orderIsCraft)
                return;
            it->second.kept.items = std::move(kept);
            it->second.kept.orderItemId = orderItemId;
            it->second.kept.supplyItemId = supplyItemId;
            it->second.kept.orderIsCraft = orderIsCraft;
            it->second.kept.valid = true;
        }
    }

    std::vector<OrderRecord> ListForGuild(uint32_t guildId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<OrderRecord> out;
        for (auto const& [guid, stored] : m_orders)
            if (stored.record.guildId == guildId && !stored.record.cancelled)
                out.push_back(stored.record);
        return out;
    }

    std::vector<OrderRecord> All()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<OrderRecord> out;
        out.reserve(m_orders.size());
        for (auto const& [guid, stored] : m_orders)
            if (!stored.record.cancelled)
                out.push_back(stored.record);
        return out;
    }

    bool Give(OrderRecord record)
    {
        if (!record.botGuidLow || !record.guildId || !record.itemId || !record.amount)
            return false;

        record.updatedAt = static_cast<uint64_t>(std::time(nullptr));
        if (!record.createdAt)
            record.createdAt = record.updatedAt;
        {
            // A parked refund stub never drops its debt: a new order for the
            // same bot inherits the unrepaid guild gold, so the refund drain
            // still finds and repays it. The new cap budget starts empty.
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_orders.find(record.botGuidLow);
            if (it != m_orders.end())
            {
                record.guildGoldHeld = it->second.record.guildGoldHeld;
                record.cancelled = false;
            }
        }
        if (!CharacterDatabase.DirectPExecute(
                "REPLACE INTO `tortoise_bots_guild_order` "
                "(`bot_guid`, `guild_id`, `giver_guid`, `parent_bot`, `root_giver`, `type`, `item_id`, `amount`, `hours`, `delivered`, "
                "`state`, `reason`, `farm_zone`, `farm_chance`, `farm_item`, `zone_index`, `deaths_in_zone`, "
                "`worked_sec`, `worked_total`, `worked_nogain`, `reported`, `farm_rest`, `keep`, `gold_cap`, `gold_cap_set`, "
                "`guild_gold_withdrawn`, `guild_gold_held`, `cancelled`, `supply_step`, `supply_return_step`, `supply_item`, `supply_count`, `supply_gold_count`, "
                "`need_items`, `need_whispered`, `last_need_whisper`, `need_bank_denied`, `stock_target`, `last_bank_check`, "
                "`created_at`, `updated_at`, `carried`, `bring`) "
                "VALUES ('%u', '%u', '%u', '%u', '%u', '%s', '%u', '%u', '%u', '%u', '%s', '%s', '%u', '%u', '%u', '%u', '%u', "
                "'%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%s', '%s', '%u', '%u', '%u', '%s', '%u', '%s', '%u', '%u', '%u', "
                "FROM_UNIXTIME('%u'), FROM_UNIXTIME('%u'), '%u', '%s')",
                record.botGuidLow, record.guildId, record.giverGuidLow, record.parentBotGuidLow,
                record.rootGiverGuidLow, TypeToken(record.type),
                record.itemId, record.amount, record.hours, record.delivered, StateToken(record.state),
                ReasonToken(record.reason), record.farmZoneId, record.farmChance, record.farmItemId,
                record.zoneIndex, record.deathsInZone, record.workedSinceNewItemSec,
                record.workedTotalSec, record.workedNoGainSec, record.reportedHave,
                record.farmRest ? 1u : 0u,
                record.keep ? 1u : 0u, record.goldCap, record.hasGoldCap ? 1u : 0u,
                record.guildGoldWithdrawn, record.guildGoldHeld, record.cancelled ? 1u : 0u,
                SupplyStepToken(record.supplyStep),
                SupplyStepToken(record.supplyReturnStep), record.supplyItemId, record.supplyCount, record.supplyGoldCount,
                NeedItemsToken(record.needItems).c_str(), record.needWhispered ? 1u : 0u,
                record.lastNeedWhisper.c_str(), record.needBankDenied ? 1u : 0u,
                record.stockTarget, record.lastBankCheckMs,
                static_cast<uint32_t>(record.createdAt), static_cast<uint32_t>(record.updatedAt),
                record.reportedCarry, BringItemsToken(record.bringItems).c_str()))
        {
            sLog.outError("TortoiseBots: failed to persist guild order for bot %u", record.botGuidLow);
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_orders[record.botGuidLow] = Stored{record};
        }
        return true;
    }

    void FinishRepaid()
    {
        std::vector<uint32_t> deletes;
        std::vector<OrderRecord> writes;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto it = m_orders.begin(); it != m_orders.end();)
            {
                // The refund drain paid the last copper: the parked row goes, in
                // memory and in the table. A still-owed row survives restarts.
                if (it->second.record.cancelled && it->second.record.guildGoldHeld == 0)
                {
                    deletes.push_back(it->first);
                    it = m_orders.erase(it);
                }
                else
                {
                    // Parked stubs never reach Tick (All skips them): this is
                    // their persist path for the cancelled row.
                    if (it->second.record.cancelled)
                    {
                        OrderRecord snapshot;
                        if (TakeDirtyLocked(it->second, snapshot))
                            writes.push_back(snapshot);
                    }
                    ++it;
                }
            }
        }
        for (uint32_t guidLow : deletes)
            CharacterDatabase.DirectPExecute(
                "DELETE FROM `tortoise_bots_guild_order` WHERE `bot_guid` = '%u'", guidLow);
        for (OrderRecord const& record : writes)
            TryWriteRecord(record);
    }
    bool Cancel(uint32_t botGuidLow)
    {
        if (!botGuidLow)
            return false;
        uint32_t bagCount = 0;
        {
            std::optional<OrderRecord> row = Find(botGuidLow);
            if (!row)
                return false;
            if (row->itemId)
                if (Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, botGuidLow)))
                    bagCount = bot->GetItemCount(row->itemId);
        }
        bool parked = false;
        bool detachedDeposit = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_orders.find(botGuidLow);
            if (it == m_orders.end())
                return false;
            // A child removed with its quota in the bags keeps its row as a
            // live top-level delivery instead of ending with the stack in
            // its bags: the bank deposit path (travel, banker, drain) runs
            // per order, so the guild keeps the finished items.
            if (DetachChildDepositLocked(it->second.record, bagCount))
            {
                it->second.record.reportedHave = 0;
                MarkDirtyLocked(it->second);
                ClearQueuedSubOrdersLocked(botGuidLow);
                detachedDeposit = true;
            }
            else if (it->second.record.guildGoldHeld > 0)
            {
                it->second.record.cancelled = true;
                it->second.record.state = OrderState::Working;
                it->second.record.reason = OrderReason::None;
                it->second.record.supplyStep = SupplyStep::None;
                it->second.record.supplyItemId = 0;
                it->second.record.supplyCount = 0;
                it->second.record.supplyGoldCount = 0;
                it->second.record.supplyReturnStep = SupplyStep::None;
                it->second.record.needItems.clear();
                it->second.record.needWhispered = false;
                it->second.record.needBankDenied = false;
                it->second.record.reportedHave = 0;
                MarkDirtyLocked(it->second);
                parked = true;
                ClearQueuedSubOrdersLocked(botGuidLow);
            }
            else
            {
                m_orders.erase(it);
                ClearQueuedSubOrdersLocked(botGuidLow);
            }
        }
        if (parked || detachedDeposit)
            return true;
        CharacterDatabase.DirectPExecute(
            "DELETE FROM `tortoise_bots_guild_order` WHERE `bot_guid` = '%u'", botGuidLow);
        return true;
    }

    // Map-thread requests keyed by parent and item: one parent can hold
    // several sub-order requests in parallel (different items, on different
    // bots). RequestSupply only records them under the lock; Update drains
    // and places them with live players and the planner.
    // The running roster scan travels with the request: an attempt only
    // looks at part of the guild, so the resume point and the best candidate
    // found so far must survive until the walk completes.
    struct QueuedSubOrderScan
    {
        uint32_t lastMember = 0; // last roster member the scan looked at (guid)
        uint32_t scanStart = 0; // member the running scan started at (guid)
        uint32_t bestGuid = 0; // best free candidate found so far (guid)
        int bestRank = -1; // its rank, -1 while no free candidate was found
        bool bestCanMake = false; // that candidate crafts the item (else farms it)
        bool sawBusyUsable = false; // a busy bot could still make or farm it
        uint32_t busyScans = 0; // consecutive completed scans that saw only busy bots
    };
    struct QueuedSubOrder
    {
        uint32_t parentBotGuidLow = 0;
        uint32_t itemId = 0;
        uint32_t count = 0;
        uint32_t nextTryMs = 0;  // world uptime ms before this request retries
        uint32_t queuedAtMs = 0; // world uptime ms the request first queued
        QueuedSubOrderScan scan;
    };

    struct QueuedSubOrderKey
    {
        uint32_t parentBotGuidLow = 0;
        uint32_t itemId = 0;
        bool operator==(QueuedSubOrderKey const& other) const
        {
            return parentBotGuidLow == other.parentBotGuidLow && itemId == other.itemId;
        }
    };
    struct QueuedSubOrderKeyHash
    {
        size_t operator()(QueuedSubOrderKey const& key) const
        {
            return static_cast<size_t>(key.parentBotGuidLow) * 31u + key.itemId;
        }
    };

    bool QueueSubOrder(uint32_t parentBotGuidLow, uint32_t itemId, uint32_t count)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_orders.find(parentBotGuidLow) == m_orders.end() || !itemId || !count)
            return false;
        QueuedSubOrderKey key{ parentBotGuidLow, itemId };
        auto queued = m_queued.find(key);
        // Same item, higher count: keep the larger need, never shrink a
        // request another pass already sized.
        if (queued != m_queued.end())
        {
            if (count > queued->second.count)
                queued->second.count = count;
            return true;
        }
        QueuedSubOrder request;
        request.parentBotGuidLow = parentBotGuidLow;
        request.itemId = itemId;
        request.count = count;
        // Stamp the queue time once: a retry keeps it, so the clock pause a
        // queued request causes expires with the request, not with each retry.
        request.queuedAtMs = WorldTimer::getMSTime();
        m_queued.emplace(key, request);
        return true;
    }

    std::vector<QueuedSubOrder> TakeDueQueuedSubOrders(uint32_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<QueuedSubOrder> out;
        for (auto const& [key, queued] : m_queued)
        {
            if (SubOrderRequestDue(queued.nextTryMs, nowMs))
                out.push_back(queued);
        }
        for (QueuedSubOrder const& due : out)
            m_queued.erase(QueuedSubOrderKey{ due.parentBotGuidLow, due.itemId });
        // Due requests are drained in key order and rotated past the one
        // that used the last tick's planner budget: otherwise the same
        // request leads every tick and a second one never gets evaluated.
        std::sort(out.begin(), out.end(), [](QueuedSubOrder const& left, QueuedSubOrder const& right)
        {
            if (left.parentBotGuidLow != right.parentBotGuidLow)
                return left.parentBotGuidLow < right.parentBotGuidLow;
            return left.itemId < right.itemId;
        });
        if (!out.empty())
        {
            std::vector<uint32_t> parents;
            std::vector<uint32_t> items;
            parents.reserve(out.size());
            items.reserve(out.size());
            for (QueuedSubOrder const& request : out)
            {
                parents.push_back(request.parentBotGuidLow);
                items.push_back(request.itemId);
            }
            size_t const begin = SubOrderDrainOffset(parents.data(), items.data(), out.size(),
                m_drainedParentBotGuidLow, m_drainedItemId);
            std::rotate(out.begin(), out.begin() + begin, out.end());
        }
        return out;
    }

    void NoteSubOrderDrained(uint32_t parentBotGuidLow, uint32_t itemId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_drainedParentBotGuidLow = parentBotGuidLow;
        m_drainedItemId = itemId;
    }

    void RetryQueuedSubOrder(uint32_t parentBotGuidLow, uint32_t itemId, uint32_t count, uint32_t nextTryMs,
        uint32_t queuedAtMs, QueuedSubOrderScan const& scan)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_orders.find(parentBotGuidLow) == m_orders.end() || !itemId || !count)
            return;
        QueuedSubOrderKey key{ parentBotGuidLow, itemId };
        QueuedSubOrder& queued = m_queued[key];
        queued.parentBotGuidLow = parentBotGuidLow;
        queued.itemId = itemId;
        if (count > queued.count)
            queued.count = count;
        queued.nextTryMs = nextTryMs;
        queued.queuedAtMs = queuedAtMs;
        queued.scan = scan;
    }

    void ClearQueuedSubOrders(uint32_t parentBotGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ClearQueuedSubOrdersLocked(parentBotGuidLow);
    }

    void ClearQueuedSubOrdersLocked(uint32_t parentBotGuidLow)
    {
        for (auto it = m_queued.begin(); it != m_queued.end();)
            it = it->second.parentBotGuidLow == parentBotGuidLow ? m_queued.erase(it) : std::next(it);
    }

    void SetFarmZone(uint32_t botGuidLow, uint32_t zoneId, uint8_t chance, uint32_t zoneIndex, uint32_t farmItemId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return;
        it->second.record.farmZoneId = zoneId;
        it->second.record.farmChance = chance;
        it->second.record.zoneIndex = zoneIndex;
        it->second.record.farmItemId = farmItemId;
        it->second.record.searchFailsInZone = 0;
    }

    void SetState(uint32_t botGuidLow, OrderState state, OrderReason reason)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return;
        if (it->second.record.state == state && it->second.record.reason == reason)
            return;
        it->second.record.state = state;
        it->second.record.reason = reason;
        if (state == OrderState::Delivering)
        {
            it->second.record.supplyStep = SupplyStep::Deliver;
            it->second.record.supplyItemId = it->second.record.itemId;
            it->second.record.supplyCount = 0;
            it->second.record.supplyGoldCount = 0;
            it->second.record.supplyReturnStep = SupplyStep::None;
        }
        else if (state == OrderState::Working && it->second.record.supplyStep == SupplyStep::Deliver)
        {
            it->second.record.supplyStep = SupplyStep::None;
            it->second.record.supplyItemId = 0;
            it->second.record.supplyCount = 0;
            it->second.record.supplyGoldCount = 0;
            it->second.record.supplyReturnStep = SupplyStep::None;
        }
        else if (IsFinished(state))
        {
            it->second.record.supplyStep = SupplyStep::None;
            it->second.record.supplyItemId = 0;
            it->second.record.supplyCount = 0;
            it->second.record.supplyGoldCount = 0;
            it->second.record.supplyReturnStep = SupplyStep::None;
            it->second.record.needItems.clear();
            it->second.record.needWhispered = false;
            it->second.record.needBankDenied = false;
            // Only a successful hand-off empties the carry, and the final
            // line and the stored row must not repeat the last pre-delivery
            // scan. A cant or noskill row keeps the stack still in the bags.
            if (HandOffSucceeded(state))
                it->second.record.reportedCarry = 0;
            // Unspent guild gold outlives the order: the row parks instead
            // of disappearing, and the refund drain pays it from any banker
            // visit before FinishRepaid clears the row.
            if (it->second.record.guildGoldHeld > 0)
                it->second.record.cancelled = true;
        }
        // A round that ends cant or noskill ends the keep with it; mailed is
        // dropped by the delivery drain with the same rule. Keep survives on
        // working, waiting, delivering and stocked.
        if (it->second.record.keep && (state == OrderState::NoSkill || state == OrderState::Cant))
        {
            it->second.record.keep = false;
            it->second.record.stockTarget = 0;
        }
        MarkDirtyLocked(it->second);
    }

    void SetSupply(uint32_t botGuidLow, SupplyStep step, uint32_t itemId, uint32_t count,
        SupplyStep returnStep, OrderReason reason, uint32_t goldCount)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return;
        OrderRecord& record = it->second.record;
        // A denial belongs to the bank leg that produced it: it survives
        // only while the step and item stay put. A step or item change with
        // no explicit reason clears it, so the addon never shows a stale
        // denial while the bot farms or buys. The empty-counter verdict
        // survives the same way: the ladder keeps buying more of it on the
        // same visit, and a new listing may still succeed.
        if (reason == OrderReason::None && step == record.supplyStep && record.supplyItemId == itemId &&
            (record.reason == OrderReason::BankDenied || record.reason == OrderReason::NoBanker ||
                record.reason == OrderReason::NoSource || record.reason == OrderReason::NoListing))
            reason = record.reason;
        // The farm give-up verdict belongs to the farm leg: a sub-order is
        // normal progress, so the addon must not show it as a failure.
        if (step == SupplyStep::Sub && reason == OrderReason::NoSource)
            reason = OrderReason::None;
        if (record.state == OrderState::Waiting && record.reason == OrderReason::NeedItems && step != SupplyStep::Need)
        {
            record.state = OrderState::Working;
            record.reason = OrderReason::None;
        }
        if (record.supplyStep == step && record.supplyItemId == itemId &&
            record.supplyCount == count && record.supplyGoldCount == goldCount &&
            record.supplyReturnStep == returnStep && record.reason == reason)
            return;
        // The streak counts one leg's searches: a leg or item change restarts
        // it, and the committed city and the arrived-visit count belong to
        // the auction leg alone. A landed pick no longer clears it (the
        // search layer re-picks between cities while the bot walks, so a
        // clear per pick would never let three failures accumulate).
        if (step != SupplyStep::AuctionHouse || itemId != record.supplyItemId)
        {
            record.committedAuctionZoneId = 0;
            record.arrivedAuctionVisits = 0;
        }
        if (step != record.supplyStep || itemId != record.supplyItemId)
            record.searchZoneMoves = 0;
        // Two clocks belong to the auction leg: entering it (or changing its
        // item) starts the travel cap, and the reach deadline starts only when
        // the bot arrives in the auctioneer's city. Leaving the leg clears
        // both.
        bool const enteringAuction = step == SupplyStep::AuctionHouse &&
            (record.supplyStep != SupplyStep::AuctionHouse || record.supplyItemId != itemId);
        // A relist trip is the same purchase's bookkeeping, not a new
        // search of the counter, and neither is walking back from a gold
        // top-up: both belong to the visit that is already counted.
        bool const relistEntry = it->second.relistItemId == itemId && it->second.relistCount;
        bool const goldReturn = itemId && it->second.auctionGoldReturnItemId == itemId;
        if (enteringAuction && goldReturn)
            it->second.auctionGoldReturnItemId = 0;
        else if (enteringAuction && !relistEntry)
            NoteAuctionLegEntered(it->second.auctionCycles[itemId], itemId);
        if (step == SupplyStep::Bank && returnStep == SupplyStep::AuctionHouse && goldCount)
            it->second.auctionGoldReturnItemId = itemId;
        record.supplyStep = step;
        record.supplyItemId = itemId;
        record.supplyCount = count;
        record.supplyReturnStep = returnStep;
        record.supplyGoldCount = goldCount;
        record.reason = reason;
        if (step == SupplyStep::Need)
            MarkAuctionLadderEndLocked(it->second, itemId);
        if (enteringAuction)
        {
            it->second.auctionReachStartedMs = 0;
            it->second.auctionLegStartedMs = WorldTimer::getMSTime();
        }
        else if (step != SupplyStep::AuctionHouse)
        {
            it->second.auctionReachStartedMs = 0;
            it->second.auctionLegStartedMs = 0;
        }
        MarkDirtyLocked(it->second);
        it->second.supplySearch = SupplySearchStreak{};
    }

    void SetNeedItems(uint32_t botGuidLow, std::vector<NeedItem> const& items, bool bankDenied)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return;
        OrderRecord& record = it->second.record;
        bool same = NeedItemsEqual(record.needItems, items);
        if (same && record.state == OrderState::Waiting && record.reason == OrderReason::NeedItems)
        {
            // The denial expired: the bank may have stocked since. A repeat
            // park re-arms the ten-second lane instead of waiting forever.
            record.needBankDenied = record.needBankDenied && bankDenied;
            return;
        }
        record.needItems = items;
        for (NeedItem const& item : items)
            MarkAuctionLadderEndLocked(it->second, item.itemId);
        record.state = OrderState::Waiting;
        record.reason = OrderReason::NeedItems;
        record.supplyStep = SupplyStep::Need;
        record.supplyReturnStep = SupplyStep::None;
        record.supplyGoldCount = 0;
        record.needWhispered = !NeedListNeedsWhisper(record.lastNeedWhisper, items);
        record.needBankDenied = bankDenied;
        MarkDirtyLocked(it->second);
    }

    void AddNeedItem(uint32_t botGuidLow, uint32_t itemId, uint32_t count, bool bankDenied)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || IsFinished(it->second.record.state) || it->second.record.cancelled ||
            !itemId || !count)
            return;
        OrderRecord& record = it->second.record;
        MergeNeedItem(record.needItems, itemId, count);
        MarkAuctionLadderEndLocked(it->second, itemId);
        // One denied probe is enough to keep the bank lane off this order:
        // OR keeps a denial a previous item recorded, so a later candidate
        // does not re-arm the lane (a full re-list clears it in SetNeedItems,
        // as do the need-clear and need-resume paths).
        record.needBankDenied = record.needBankDenied || bankDenied;
        // A working row was on some other item: dropping the supply leg makes
        // the ladder re-plan the whole tree around the new need item. The
        // reason belonged to the leg that produced it, so the next candidate
        // does not inherit a dead end (a vendor/farm/bank leg it would skip).
        if (record.state == OrderState::Working)
        {
            record.reason = ResetDeadEndReasonOnWorking(record.reason);
            record.supplyStep = SupplyStep::None;
            record.supplyItemId = 0;
            record.supplyCount = 0;
            record.supplyGoldCount = 0;
            record.supplyReturnStep = SupplyStep::None;
        }
        record.needWhispered = !NeedListNeedsWhisper(record.lastNeedWhisper, record.needItems);
        MarkDirtyLocked(it->second);
    }

    bool ReplaceNeedItems(uint32_t botGuidLow, std::vector<NeedItem> const& expected,
        std::vector<NeedItem> const& items)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || IsFinished(it->second.record.state))
            return false;
        OrderRecord& record = it->second.record;
        // Compare-and-set: the prune ran on a snapshot, so a merge or a clear
        // in between wins and the next refresh re-prunes the live list. The
        // false return tells the caller the write did not land and the row it
        // worked from is stale.
        if (!NeedItemsEqual(record.needItems, expected))
            return false;
        record.needItems = items;
        record.needWhispered = !NeedListNeedsWhisper(record.lastNeedWhisper, record.needItems);
        MarkDirtyLocked(it->second);
        return true;
    }

    void RecordGuildGoldWithdrawal(uint32_t botGuidLow, uint32_t amount)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!amount)
            return;
        // A child's withdrawal counts against every ancestor's cap up to
        // the root: each row carries the inherited cap value, so charging
        // the whole branch keeps one shared budget.
        uint32_t current = botGuidLow;
        for (size_t guard = 0; current && guard < 64; ++guard)
        {
            auto it = m_orders.find(current);
            if (it == m_orders.end())
                break;
            OrderRecord& record = it->second.record;
            record.guildGoldWithdrawn = amount > UINT32_MAX - record.guildGoldWithdrawn ?
                UINT32_MAX : record.guildGoldWithdrawn + amount;
            if (current == botGuidLow)
                record.guildGoldHeld = amount > UINT32_MAX - record.guildGoldHeld ?
                    UINT32_MAX : record.guildGoldHeld + amount;
            MarkDirtyLocked(it->second);
            current = record.parentBotGuidLow;
        }
    }

    void ConsumeGuildGoldHeld(uint32_t botGuidLow, uint32_t amount)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || !amount)
            return;
        it->second.record.guildGoldHeld = amount >= it->second.record.guildGoldHeld ? 0 :
            it->second.record.guildGoldHeld - amount;
        MarkDirtyLocked(it->second);
    }
    // A refund pass moved `deposited` copper of held guild gold back into
    // the bank. The drop is exact: only the drain's DepositMoney call moves
    // held gold, and the world-thread drains run one at a time. Called on
    // the world thread only.
    void RefundGuildGold(uint32_t botGuidLow, uint32_t deposited)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || !deposited)
            return;
        OrderRecord& record = it->second.record;
        record.guildGoldHeld = deposited >= record.guildGoldHeld ? 0 :
            record.guildGoldHeld - deposited;
        MarkDirtyLocked(it->second);
    }

    void NoteSupplyGain(uint32_t botGuidLow, uint32_t itemId, uint32_t count)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || !itemId || !count)
            return;
        for (NeedItem& gain : it->second.record.supplyGains)
            if (gain.itemId == itemId)
            {
                gain.count = count > UINT32_MAX - gain.count ? UINT32_MAX : gain.count + count;
                MarkDirtyLocked(it->second);
                return;
            }
        it->second.record.supplyGains.push_back(NeedItem{ itemId, count });
        MarkDirtyLocked(it->second);
    }

    void NoteSupplySpent(uint32_t botGuidLow, uint32_t itemId, uint32_t count)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || !itemId || !count)
            return;
        for (NeedItem& gain : it->second.record.supplyGains)
            if (gain.itemId == itemId)
            {
                gain.count = count >= gain.count ? 0 : gain.count - count;
                MarkDirtyLocked(it->second);
                return;
            }
    }

    void NoteBankEmpty(uint32_t botGuidLow, uint32_t itemId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || !itemId)
            return;
        it->second.record.bankEmptyItemId = itemId;
        it->second.record.bankEmptyMs = WorldTimer::getMSTime();
        MarkDirtyLocked(it->second);
    }

    // One hand-over per delivered sub-order: the parent fetches it from the
    // bank once; a later shortage runs the ladder again (bank empty, then a
    // new sub-order) instead of bouncing off the same finished row.
    bool TakeDeliveredSubOrder(uint32_t parentBotGuidLow, uint32_t itemId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<Stored*> delivered;
        for (auto& [guid, stored] : m_orders)
        {
            OrderRecord const& row = stored.record;
            if (row.parentBotGuidLow != parentBotGuidLow || row.itemId != itemId || row.cancelled)
                continue;
            if (!IsFinished(row.state))
                return false;
            if ((row.state == OrderState::Done || row.state == OrderState::Stocked) && !row.handedOver)
                delivered.push_back(&stored);
        }
        for (Stored* stored : delivered)
            stored->record.handedOver = true;
        return !delivered.empty();
    }

    void ClearBankEmpty(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || !it->second.record.bankEmptyItemId)
            return;
        it->second.record.bankEmptyItemId = 0;
        it->second.record.bankEmptyMs = 0;
        MarkDirtyLocked(it->second);
    }
    void NoteMailEmpty(uint32_t botGuidLow, uint32_t itemId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || !itemId)
            return;
        it->second.record.mailEmptyItemId = itemId;
        it->second.record.mailEmptyMs = WorldTimer::getMSTime();
        MarkDirtyLocked(it->second);
    }

    void ClearMailEmpty(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || !it->second.record.mailEmptyItemId)
            return;
        it->second.record.mailEmptyItemId = 0;
        it->second.record.mailEmptyMs = 0;
        MarkDirtyLocked(it->second);
    }

    // Consecutive empty service-leg searches for the current leg. A leg or
    // item change restarts it; the caller advances the ladder at the limit.
    uint32_t NoteSupplySearchFailed(uint32_t botGuidLow, SupplyStep step, uint32_t itemId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || it->second.record.state != OrderState::Working ||
            it->second.record.supplyStep != step || !itemId)
            return 0;
        // The caller keys the streak on the leg's item: the mail item for
        // Bags (the supply item, or the order item when the leg collects
        // it), the supply item otherwise. Any other change restarts it.
        uint32_t legItem = (step == SupplyStep::Bags && !it->second.record.supplyItemId) ?
            it->second.record.itemId : it->second.record.supplyItemId;
        if (legItem != itemId)
            return 0;
        it->second.supplySearch = NextSupplySearchStreak(it->second.supplySearch, step, itemId);
        MarkDirtyLocked(it->second);
        return it->second.supplySearch.fails;
    }

    void NoteSupplyProgress(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return;
        it->second.record.workedNoGainSec = 0;
        it->second.record.workedSinceNewItemSec = 0;
        MarkDirtyLocked(it->second);
    }

    bool ShouldCheckNeedBank(uint32_t botGuidLow, uint32_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() ||
            !NeedBankLaneEligible(it->second.record.state, it->second.record.reason,
                it->second.record.supplyStep, !it->second.record.needItems.empty()))
            return false;
        return nowMs - it->second.lastNeedBankCheckMs >= kNeedBankCheckMs;
    }

    void MarkNeedBankChecked(uint32_t botGuidLow, uint32_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it != m_orders.end())
            it->second.lastNeedBankCheckMs = nowMs;
    }

    // A parked need row re-checks the vendor/AH facts on the slow cadence
    // (the policy header owns the interval); the ten-second bank lane above
    // keeps its own clock.
    bool ShouldCheckNeedLadder(uint32_t botGuidLow, uint32_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() ||
            !NeedLadderLaneEligible(it->second.record.state, it->second.record.reason,
                !it->second.record.needItems.empty()))
            return false;
        return NeedLadderCheckDue(nowMs, it->second.lastNeedLadderCheckMs);
    }

    void MarkNeedLadderChecked(uint32_t botGuidLow, uint32_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it != m_orders.end())
        {
            it->second.lastNeedLadderCheckMs = nowMs;
            ++it->second.needLadderChecks;
        }
    }

    uint32_t NeedLadderChecks(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        return it == m_orders.end() ? 0 : it->second.needLadderChecks;
    }

    bool ShouldScanProgress(uint32_t botGuidLow, uint32_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return false;
        return ProgressScanDue(nowMs, it->second.lastProgressScanMs);
    }

    void MarkProgressScanned(uint32_t botGuidLow, uint32_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it != m_orders.end())
            it->second.lastProgressScanMs = nowMs;
    }

    void StartKeepWatch(uint32_t botGuidLow, uint32_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return;
        OrderRecord& record = it->second.record;
        if (!record.keep || !record.stockTarget)
            return;
        record.state = OrderState::Stocked;
        record.reason = OrderReason::None;
        record.amount = record.stockTarget;
        record.delivered = 0;
        record.lastBankCheckMs = nowMs;
        MarkDirtyLocked(it->second);
    }

    void DropKeep(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || !it->second.record.keep)
            return;
        it->second.record.keep = false;
        it->second.record.stockTarget = 0;
        MarkDirtyLocked(it->second);
    }

    void NoteDeath(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return;
        if (it->second.record.state != OrderState::Working)
            return;
        ++it->second.record.deathsInZone;
        if (MoveZoneLocked(it->second))
            it->second.zoneMovePending = true;
        MarkDirtyLocked(it->second);
    }

    // Remember a failed farm-leg travel target point, so the planner leaves
    // the zone out once kFarmZoneFailedTargets different points in it failed
    // inside the window. Only the zone the order actually farms counts: a
    // banker or auctioneer leg fails in a city, never in the farm zone, and
    // must not mark it.
    void NoteFarmZoneTargetFailed(uint32_t botGuidLow, uint32_t zoneId, uint32_t mapId,
        float x, float y, float z)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return;
        if (it->second.record.state != OrderState::Working ||
            it->second.record.supplyStep != SupplyStep::Farm ||
            it->second.record.farmZoneId != zoneId)
            return;
        RecordFarmZoneTargetFailure(it->second.farmZoneFailures[zoneId],
            FarmZoneTargetPointAt(mapId, x, y, z), WorldTimer::getMSTime());
    }

    std::vector<FarmZoneTargetFailure> GetFarmZoneFailures(uint32_t botGuidLow, uint32_t zoneId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return {};
        auto found = it->second.farmZoneFailures.find(zoneId);
        return found == it->second.farmZoneFailures.end() ? std::vector<FarmZoneTargetFailure>() :
            found->second;
    }

    void NoteSearchFailed(uint32_t botGuidLow, uint32_t zoneId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return;
        if (it->second.record.state != OrderState::Working || it->second.record.farmZoneId != zoneId)
            return;
        ++it->second.record.searchFailsInZone;
        if (SearchFailsMoveZone(it->second.record.searchFailsInZone) && MoveZoneForSearchLocked(it->second))
            it->second.zoneMovePending = true;
        MarkDirtyLocked(it->second);
    }

    void NoteFarmRest(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || IsFinished(it->second.record.state))
            return;
        if (it->second.record.type != OrderType::Buy || it->second.record.farmRest)
            return;
        it->second.record.farmRest = true;
        it->second.record.farmZoneId = 0;
        it->second.record.farmChance = 0;
        it->second.record.zoneIndex = 0;
        MarkDirtyLocked(it->second);
    }

    void NoteSearchLanded(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return;
        // Only the farm-zone streak resets here: a landed zone pick serves
        // the zone, so the next rejection counts from zero. The leg streak
        // restarts on a leg or item change instead: the search layer
        // re-picks between cities while the bot walks the auction leg, and
        // clearing per pick would never let three failures accumulate.
        if (it->second.record.searchFailsInZone == 0 && it->second.record.searchZoneMoves == 0)
            return;
        it->second.record.searchFailsInZone = 0;
        it->second.record.searchZoneMoves = 0;
        MarkDirtyLocked(it->second);
    }

    // The auction leg commits to one city: the first pick stores its zone,
    // later picks reuse the stored one. A city change commits nothing: the
    // count below still runs, so flapping searches cannot dodge the exit.
    void CommitAuctionZone(uint32_t botGuidLow, uint32_t zoneId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || !zoneId ||
            it->second.record.state != OrderState::Working ||
            it->second.record.supplyStep != SupplyStep::AuctionHouse)
            return;
        if (it->second.record.committedAuctionZoneId == zoneId)
            return;
        if (it->second.record.committedAuctionZoneId != 0)
            return;
        it->second.record.committedAuctionZoneId = zoneId;
        MarkDirtyLocked(it->second);
    }

    // An arrived auction-leg trip ended with no auctioneer in reach. Counts
    // on the leg, not the pick, so re-picks between cities never reset it.
    // An auctioneer in reach clears it: the buy action owns the ladder from
    // there. Returns the consecutive count for the caller.
    uint32_t NoteArrivedAuctionVisit(uint32_t botGuidLow, bool auctioneerInReach)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || it->second.record.state != OrderState::Working ||
            it->second.record.supplyStep != SupplyStep::AuctionHouse)
            return 0;
        OrderRecord& record = it->second.record;
        record.arrivedAuctionVisits = auctioneerInReach ? 0 : record.arrivedAuctionVisits + 1;
        // The travel target landed: the reach deadline starts here (and
        // clears when the arrival already has the auctioneer in reach).
        if (auctioneerInReach)
        {
            it->second.auctionReachStartedMs = 0;
            it->second.auctionLegStartedMs = 0;
        }
        else if (!it->second.auctionReachStartedMs)
            it->second.auctionReachStartedMs = WorldTimer::getMSTime();
        MarkDirtyLocked(it->second);
        return record.arrivedAuctionVisits;
    }

    // The bot came within sight of an auctioneer of the city it stands in: the
    // reach deadline starts here, not where the leg was entered.
    void NoteAuctionZoneArrival(uint32_t botGuidLow, uint32_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || it->second.record.state != OrderState::Working ||
            it->second.record.supplyStep != SupplyStep::AuctionHouse)
            return;
        if (!it->second.auctionReachStartedMs)
            it->second.auctionReachStartedMs = nowMs;
    }

    // Either auction clock running out hands the leg back to the ladder: the
    // reach deadline once the bot arrived in the city, the travel cap while it
    // is still walking. Neither clock starts at leg entry, so a bot in the
    // field keeps its whole walk.
    bool AuctionReentryAllowed(uint32_t botGuidLow, uint32_t itemId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || !itemId)
            return true;
        auto found = it->second.auctionCycles.find(itemId);
        return found == it->second.auctionCycles.end() ||
            TortoiseBots::GuildOrders::AuctionReentryAllowed(found->second, itemId);
    }

    void NoteAuctionLegEnded(uint32_t botGuidLow, OrderReason reason)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || it->second.record.supplyStep != SupplyStep::AuctionHouse)
            return;
        OrderRecord const& record = it->second.record;
        NoteOrderStep(it->second.steps, OrderStepEntry{ static_cast<uint64_t>(std::time(nullptr)),
            record.state, reason, record.supplyStep, record.supplyItemId });
    }

    bool AuctionLegUnreachable(uint32_t botGuidLow, uint32_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || it->second.record.state != OrderState::Working ||
            it->second.record.supplyStep != SupplyStep::AuctionHouse)
            return false;
        return AuctionUnreachableElapsed(it->second.auctionReachStartedMs, nowMs) ||
            AuctionLegTravelCapped(it->second.auctionLegStartedMs, nowMs);
    }

    void NoteAuctioneerInReach(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return;
        // The walk is over: neither clock has anything left to time.
        it->second.auctionReachStartedMs = 0;
        it->second.auctionLegStartedMs = 0;
    }

    void NoteRelistSurplus(uint32_t botGuidLow, uint32_t itemId, uint32_t count, uint32_t unitPrice)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return;
        it->second.relistItemId = itemId;
        it->second.relistCount = count;
        it->second.relistUnitPrice = unitPrice;
    }

    bool PendingRelist(uint32_t botGuidLow, uint32_t& itemId, uint32_t& count, uint32_t& unitPrice)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        itemId = 0;
        count = 0;
        unitPrice = 0;
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || it->second.record.state != OrderState::Working ||
            !it->second.relistItemId || !it->second.relistCount)
            return false;
        itemId = it->second.relistItemId;
        count = it->second.relistCount;
        unitPrice = it->second.relistUnitPrice;
        return true;
    }

    void ClearRelistSurplus(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return;
        it->second.relistItemId = 0;
        it->second.relistCount = 0;
        it->second.relistUnitPrice = 0;
    }

    // One relist was posted: its sale gold or its returned stack comes back
    // by mail later. The listing terms are the identity the notification
    // carries back, so one auction of the item cannot settle another's row.
    // Map thread only (the buy action); the row write is synchronous and checked.
    void NoteRelistPosted(uint32_t botGuidLow, uint32_t guildId, uint32_t itemId, uint32_t count,
        uint32_t unitPrice, uint32_t bid, uint32_t buyout, uint32_t deposit, uint32_t durationSec)
    {
        if (!botGuidLow || !guildId || !itemId || !count)
            return;
        RelistLedgerEntry entry;
        entry.botGuidLow = botGuidLow;
        entry.guildId = guildId;
        entry.itemId = itemId;
        entry.count = count;
        entry.unitPrice = unitPrice;
        entry.bid = bid;
        entry.buyout = buyout;
        entry.deposit = deposit;
        entry.postedAtSec = static_cast<uint64_t>(std::time(nullptr));
        entry.expiresAtSec = entry.postedAtSec + durationSec;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            entry.seq = 1;
            for (RelistLedgerEntry const& row : m_relist[botGuidLow])
                if (row.seq >= entry.seq)
                    entry.seq = row.seq + 1;
        }
        // The checked write lands before the row exists in memory: a failed
        // insert must not leave a debt only this process knows about.
        if (!CharacterDatabase.DirectPExecute(
                "REPLACE INTO `tortoise_bots_guild_relist` (`bot_guid`, `seq`, `guild_id`, `item_id`, "
                "`count`, `unit_price`, `bid`, `buyout`, `deposit`, `posted_at`, `expires_at`, "
                "`state`, `claim_mail`, `held_gold`, `held_count`) "
                "VALUES ('%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '0', '0', '0', '0')",
                entry.botGuidLow, entry.seq, entry.guildId, entry.itemId, entry.count, entry.unitPrice,
                entry.bid, entry.buyout, entry.deposit, static_cast<uint32_t>(entry.postedAtSec),
                static_cast<uint32_t>(entry.expiresAtSec)))
        {
            TB_LOG_BASIC("TortoiseBots: guild relist for bot %u (item %u x%u) was posted but its "
                "ledger row could not be written; the value is not tracked.", botGuidLow, itemId, count);
            return;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<RelistLedgerEntry>& rows = m_relist[botGuidLow];
        for (RelistLedgerEntry const& row : rows)
            if (row.seq == entry.seq)
                return;
        rows.push_back(entry);
    }

    // The bot's ledger rows, oldest first. Any thread.
    std::vector<RelistLedgerEntry> RelistEntries(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_relist.find(botGuidLow);
        if (it == m_relist.end())
            return {};
        return it->second;
    }

    // One state change of a ledger row. The checked write lands before memory
    // changes, so a restart never sees a state the table lost. Map thread only
    // (the mail read).
    bool WriteRelistState(uint32_t botGuidLow, uint32_t seq, RelistState state, uint32_t mailId,
        uint32_t gold, uint32_t items)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_relist.find(botGuidLow);
            if (it == m_relist.end())
                return false;
            bool found = false;
            for (RelistLedgerEntry const& entry : it->second)
            {
                if (entry.seq == seq)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                return false;
        }
        if (!CharacterDatabase.DirectPExecute(
                "UPDATE `tortoise_bots_guild_relist` SET `state` = '%u', `claim_mail` = '%u', "
                "`held_gold` = '%u', `held_count` = '%u' WHERE `bot_guid` = '%u' AND `seq` = '%u'",
                static_cast<uint32_t>(state), mailId, gold, items, botGuidLow, seq))
            return false;
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_relist.find(botGuidLow);
        if (it == m_relist.end())
            return true;
        for (RelistLedgerEntry& entry : it->second)
        {
            if (entry.seq != seq)
                continue;
            entry.state = state;
            entry.claimMailId = mailId;
            entry.heldGold = gold;
            entry.heldCount = items;
            break;
        }
        return true;
    }

    // A matched mail is about to be taken: the row names it and the value it
    // promises. Map thread only (the mail read).
    bool NoteRelistClaimed(uint32_t botGuidLow, uint32_t seq, uint32_t mailId, uint32_t gold,
        uint32_t items)
    {
        if (!mailId)
            return false;
        return WriteRelistState(botGuidLow, seq, RelistState::Claimed, mailId, gold, items);
    }

    // A claimed mail was collected: the value is in the bot's hands. Map
    // thread only (the mail read).
    bool NoteRelistReceived(uint32_t botGuidLow, uint32_t seq, uint32_t gold, uint32_t items)
    {
        return WriteRelistState(botGuidLow, seq, RelistState::Received, 0, gold, items);
    }

    // Writes a ledger row (insert or replace by seq). The table write is
    // checked and synchronous: memory changes only once it landed. World
    // thread only (the relist drain).
    bool SaveRelist(RelistLedgerEntry const& row)
    {
        if (!CharacterDatabase.DirectPExecute(
                "REPLACE INTO `tortoise_bots_guild_relist` (`bot_guid`, `seq`, `guild_id`, `item_id`, "
                "`count`, `unit_price`, `bid`, `buyout`, `deposit`, `posted_at`, `expires_at`, "
                "`state`, `claim_mail`, `held_gold`, `held_count`) "
                "VALUES ('%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u')",
                row.botGuidLow, row.seq, row.guildId, row.itemId, row.count, row.unitPrice,
                row.bid, row.buyout, row.deposit, static_cast<uint32_t>(row.postedAtSec),
                static_cast<uint32_t>(row.expiresAtSec), static_cast<uint32_t>(row.state),
                row.claimMailId, row.heldGold, row.heldCount))
            return false;
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<RelistLedgerEntry>& rows = m_relist[row.botGuidLow];
        for (RelistLedgerEntry& existing : rows)
        {
            if (existing.seq == row.seq)
            {
                existing = row;
                return true;
            }
        }
        rows.push_back(row);
        return true;
    }

    // Drops a ledger row from the table and from memory, with the same
    // checked write as SaveRelist. World thread only (the relist drain).
    bool DeleteRelist(uint32_t botGuidLow, uint32_t seq)
    {
        if (!CharacterDatabase.DirectPExecute(
                "DELETE FROM `tortoise_bots_guild_relist` WHERE `bot_guid` = '%u' AND `seq` = '%u'",
                botGuidLow, seq))
            return false;
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_relist.find(botGuidLow);
        if (it == m_relist.end())
            return true;
        std::vector<RelistLedgerEntry>& rows = it->second;
        for (auto row = rows.begin(); row != rows.end(); ++row)
        {
            if (row->seq == seq)
            {
                rows.erase(row);
                break;
            }
        }
        if (rows.empty())
            m_relist.erase(it);
        return true;
    }

    // The received rows the bank trip has to pay out, oldest first. Any
    // thread: the bank action checks what it can pay, the drain pays it.
    std::vector<RelistLedgerEntry> RelistDeposits(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<RelistLedgerEntry> out;
        auto it = m_relist.find(botGuidLow);
        if (it == m_relist.end())
            return out;
        for (RelistLedgerEntry const& entry : it->second)
            if (RelistNeedsDeposit(entry))
                out.push_back(entry);
        return out;
    }

    // One stale sweep a minute, not one per world tick.
    bool RelistExpireDue(uint64_t nowSec)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_lastRelistExpireSec && nowSec < m_lastRelistExpireSec + 60)
            return false;
        m_lastRelistExpireSec = nowSec;
        return true;
    }

    // The bots whose ledger rows are old enough to expire (see
    // RelistEntryAged): only those need a presence lookup.
    std::vector<uint32_t> RelistAgedBots(uint64_t nowSec)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<uint32_t> out;
        for (auto const& [guid, rows] : m_relist)
        {
            for (RelistLedgerEntry const& row : rows)
            {
                if (RelistEntryAged(row, nowSec))
                {
                    out.push_back(guid);
                    break;
                }
            }
        }
        return out;
    }

    // A row expires only when the policy says so (RelistEntryExpiry): three
    // days with nothing back AND the bot online after the auction ended, or
    // the core's 30-day mail lifetime gone. A bot offline for days keeps its
    // row, because the sale proceeds or the expired stack still wait in its
    // mail. World thread only; called at a minute's cadence, never per tick.
    void ExpireStaleRelists(uint64_t nowSec)
    {
        std::vector<uint32_t> const aged = RelistAgedBots(nowSec);
        if (aged.empty())
            return;
        std::unordered_map<uint32_t, RelistBotPresence> presence = ReadRelistPresence(aged);

        struct Dropped
        {
            RelistLedgerEntry row;
            RelistExpiry why;
        };
        std::vector<Dropped> dropped;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto it = m_relist.begin(); it != m_relist.end();)
            {
                std::vector<RelistLedgerEntry>& rows = it->second;
                auto seen = presence.find(it->first);
                for (auto row = rows.begin(); row != rows.end();)
                {
                    // No presence read for this bot: keep the row for the next sweep.
                    RelistExpiry const why = seen == presence.end() ? RelistExpiry::Keep :
                        RelistEntryExpiry(*row, seen->second, nowSec);
                    if (why != RelistExpiry::Keep)
                    {
                        dropped.push_back({ *row, why });
                        row = rows.erase(row);
                    }
                    else
                        ++row;
                }
                it = rows.empty() ? m_relist.erase(it) : std::next(it);
            }
        }
        for (Dropped const& d : dropped)
        {
            RelistLedgerEntry const& entry = d.row;
            if (d.why == RelistExpiry::MailExpired)
                TB_LOG_BASIC("TortoiseBots: guild relist for bot %u (item %u x%u, %u copper held, %u items "
                    "held) is past the %llu s auction mail lifetime (ended %llu s ago); dropping the "
                    "ledger row, the mail is gone.", entry.botGuidLow, entry.itemId, entry.count,
                    entry.heldGold, entry.heldCount,
                    static_cast<unsigned long long>(kRelistMailLifetimeSec),
                    static_cast<unsigned long long>(nowSec - RelistEndSec(entry)));
            else
                TB_LOG_BASIC("TortoiseBots: guild relist for bot %u (item %u x%u, buyout %u copper) posted "
                    "%llu s ago returned nothing and the bot has been online since the auction ended; "
                    "dropping the ledger row.", entry.botGuidLow, entry.itemId, entry.count,
                    entry.buyout, static_cast<unsigned long long>(nowSec - entry.postedAtSec));
            CharacterDatabase.PExecute(
                "DELETE FROM `tortoise_bots_guild_relist` WHERE `bot_guid` = '%u' AND `seq` = '%u'",
                entry.botGuidLow, entry.seq);
        }
    }

    // Online bots carry their session start. Every other bot is offline with
    // the default presence, so its rows can only expire by the mail lifetime.
    static std::unordered_map<uint32_t, RelistBotPresence> ReadRelistPresence(
        std::vector<uint32_t> const& bots)
    {
        std::unordered_map<uint32_t, RelistBotPresence> out;
        for (uint32_t guidLow : bots)
        {
            RelistBotPresence p;
            Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guidLow));
            if (bot && bot->IsInWorld())
            {
                p.online = true;
                p.loginSec = static_cast<uint64_t>(bot->GetLoginTime());
            }
            out[guidLow] = p;
        }
        return out;
    }

    void NoteBagsFull(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || IsFinished(it->second.record.state))
            return;
        if (it->second.record.state == OrderState::Delivering)
            return;
        it->second.record.state = OrderState::Delivering;
        it->second.record.reason = OrderReason::BagsFull;
        it->second.record.supplyStep = SupplyStep::Deliver;
        it->second.record.supplyItemId = it->second.record.itemId;
        it->second.record.supplyCount = 0;
        it->second.record.supplyGoldCount = 0;
        MarkDirtyLocked(it->second);
    }

    void AddDelivered(uint32_t botGuidLow, uint32_t count)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return;
        it->second.record.delivered += count;
        MarkDirtyLocked(it->second);
    }

    std::vector<uint32_t> BotsForGuild(uint32_t guildId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<uint32_t> out;
        for (auto const& [guid, stored] : m_orders)
            if (stored.record.guildId == guildId && !stored.record.cancelled)
                out.push_back(guid);
        return out;
    }

    std::vector<uint32_t> BranchBots(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<uint32_t> out;
        if (m_orders.find(botGuidLow) == m_orders.end())
            return out;
        out.push_back(botGuidLow);
        // Walk through parked refund stubs: a cancelled row in the middle
        // must not orphan live grandchildren below it on cancel.
        for (size_t i = 0; i < out.size(); ++i)
            for (auto const& [guid, stored] : m_orders)
                if (stored.record.parentBotGuidLow == out[i])
                    out.push_back(guid);
        return out;
    }
    // One parent, several sub-orders at once: every item with a running row
    // or a queued (still unplaced) request directly below the parent is
    // covered, so the ladder asks for the next uncovered one. Handed-over
    // finished rows stop covering: the delivery is already collected, and a
    // later shortage runs the ladder again instead of delegating twice.
    std::vector<uint32_t> CoveredSubOrderItems(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<uint32_t> out;
        if (m_orders.find(botGuidLow) == m_orders.end())
            return out;
        for (auto const& [guid, stored] : m_orders)
        {
            OrderRecord const& row = stored.record;
            if (row.parentBotGuidLow != botGuidLow || row.cancelled || !row.itemId)
                continue;
            if (IsFinished(row.state))
            {
                // A failed sub-order lands on the parent's need list and a
                // cancelled one is gone: only finished rows whose delivery
                // still waits for the hand-over block a new request.
                if ((row.state == OrderState::Done || row.state == OrderState::Stocked) && !row.handedOver)
                    out.push_back(row.itemId);
                continue;
            }
            out.push_back(row.itemId);
        }
        // A queued request covers its item only inside the same staleness window
        // as the clock gate (UnfinishedSubOrderCount): one that never found a
        // worker must not keep the ladder skipping the item while the clocks run.
        uint32_t now = WorldTimer::getMSTime();
        for (auto const& [key, queued] : m_queued)
            if (key.parentBotGuidLow == botGuidLow && key.itemId &&
                now - queued.queuedAtMs < kQueuedSubOrderStaleMs)
                out.push_back(key.itemId);
        return out;
    }
    // Direct children that are work in progress, for the parent's clock gate:
    // a running row (not finished, not cancelled), plus a queued request still
    // inside its staleness window. A finished row's pending delivery is not
    // work, and a queued request older than the window never became a running
    // row, so neither may keep the parent's clocks paused.
    uint32_t UnfinishedSubOrderCount(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_orders.find(botGuidLow) == m_orders.end())
            return 0;
        uint32_t out = 0;
        for (auto const& [guid, stored] : m_orders)
        {
            OrderRecord const& row = stored.record;
            if (row.parentBotGuidLow != botGuidLow || row.cancelled || !row.itemId)
                continue;
            if (!IsFinished(row.state))
                ++out;
        }
        uint32_t now = WorldTimer::getMSTime();
        for (auto const& [key, queued] : m_queued)
            if (key.parentBotGuidLow == botGuidLow && key.itemId &&
                now - queued.queuedAtMs < kQueuedSubOrderStaleMs)
                ++out;
        return out;
    }
    // Finished direct sub-orders whose delivery still waits for the
    // hand-over: the ladder fetches these through the bank leg first so a
    // delivered sibling is collected instead of skipped as covered.
    std::vector<uint32_t> CollectableSubOrderItems(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<uint32_t> out;
        if (m_orders.find(botGuidLow) == m_orders.end())
            return out;
        for (auto const& [guid, stored] : m_orders)
        {
            OrderRecord const& row = stored.record;
            if (row.parentBotGuidLow != botGuidLow || row.cancelled || !row.itemId || row.handedOver)
                continue;
            if ((row.state == OrderState::Done || row.state == OrderState::Stocked))
                out.push_back(row.itemId);
        }
        return out;
    }

    void NoteSubOrderFailed(uint32_t parentBotGuidLow, uint32_t itemId, uint32_t count)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto parent = m_orders.find(parentBotGuidLow);
        if (parent == m_orders.end() || IsFinished(parent->second.record.state) || !itemId || !count)
            return;
        // The failed sub-order leaves its item on the parent's need list:
        // merge with the items already waiting so parallel sub-orders that
        // fail together each keep their row.
        OrderRecord& record = parent->second.record;
        MergeNeedItem(record.needItems, itemId, count);
        if (record.type == OrderType::Craft)
        {
            if (record.state == OrderState::Working)
            {
                // A running sub-order is not the player's item: the row keeps
                // working, and dropping the supply leg makes the ladder
                // re-plan the rest of the tree instead of retrying the failed
                // item. A leg on some other item survives, and a dead-end
                // reason belongs to the leg that produced it, so both go
                // together.
                if (!KeepLegOnNeedClear(record.supplyItemId, record.needItems))
                {
                    record.reason = ResetDeadEndReasonOnWorking(record.reason);
                    record.supplyStep = SupplyStep::None;
                    record.supplyItemId = 0;
                    record.supplyCount = 0;
                    record.supplyGoldCount = 0;
                    record.supplyReturnStep = SupplyStep::None;
                }
            }
        }
        else
        {
            // Partial need is a craft rule: a farm or buy order parks on the
            // failed item like any other dead end.
            record.state = OrderState::Waiting;
            record.reason = OrderReason::NeedItems;
            record.supplyStep = SupplyStep::Need;
            record.supplyReturnStep = SupplyStep::None;
            record.supplyGoldCount = 0;
            MarkAuctionLadderEndLocked(parent->second, itemId);
        }
        record.needWhispered = !NeedListNeedsWhisper(record.lastNeedWhisper, record.needItems);
        MarkDirtyLocked(parent->second);
    }

    bool ReassignSubOrder(uint32_t subOrderBotGuidLow, uint32_t newBotGuidLow, OrderType type)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto sub = m_orders.find(subOrderBotGuidLow);
        if (sub == m_orders.end())
            return false;
        // Same busy rule as placement: only running rows and stocked keep
        // orders block; finished rows and parked stubs do not.
        auto target = m_orders.find(newBotGuidLow);
        if (target != m_orders.end() && !target->second.record.cancelled &&
            (!IsFinished(target->second.record.state) || target->second.record.state == OrderState::Stocked))
            return false;
        OrderRecord moved = sub->second.record;
        // The debt stays with the bot that holds the gold: the moved row
        // starts with the new bot's own unrepaid gold, never the old bot's.
        moved.guildGoldHeld = target != m_orders.end() ? target->second.record.guildGoldHeld : 0;
        ClearQueuedSubOrdersLocked(subOrderBotGuidLow);
        // The old bot's unspent guild gold stays parked on the old bot
        // (same as cancel): mark the source row a refund stub instead of
        // erasing it, so the refund drain collects it at the next visit.
        if (sub->second.record.guildGoldHeld > 0)
        {
            OrderRecord stub = sub->second.record;
            stub.cancelled = true;
            stub.state = OrderState::Working;
            stub.reason = OrderReason::None;
            stub.supplyStep = SupplyStep::None;
            stub.supplyItemId = 0;
            stub.supplyCount = 0;
            stub.supplyGoldCount = 0;
            stub.supplyReturnStep = SupplyStep::None;
            stub.needItems.clear();
            stub.needWhispered = false;
            stub.needBankDenied = false;
            stub.reportedHave = 0;
            m_orders[subOrderBotGuidLow] = Stored{stub};
            MarkDirtyLocked(m_orders[subOrderBotGuidLow]);
        }
        else
            m_orders.erase(sub);
        moved.botGuidLow = newBotGuidLow;
        moved.type = type;
        moved.delivered = 0;
        moved.state = OrderState::Working;
        moved.reason = OrderReason::None;
        moved.farmZoneId = 0;
        moved.farmChance = 0;
        moved.zoneIndex = 0;
        moved.reportedHave = 0;
        moved.reportedCarry = 0;
        moved.supplyStep = SupplyStep::None;
        moved.supplyReturnStep = SupplyStep::None;
        moved.supplyItemId = 0;
        moved.supplyCount = 0;
        moved.supplyGoldCount = 0;
        moved.supplyGains.clear();
        moved.farmRest = false;
        moved.needItems.clear();
        moved.needWhispered = false;
        moved.needBankDenied = false;
        moved.farmItemId = 0;
        moved.workedSinceNewItemSec = 0;
        moved.workedTotalSec = 0;
        moved.workedNoGainSec = 0;
        moved.deathsInZone = 0;
        moved.searchFailsInZone = 0;
        moved.searchZoneMoves = 0;
        moved.bankEmptyItemId = 0;
        moved.bankEmptyMs = 0;
        moved.mailEmptyItemId = 0;
        moved.mailEmptyMs = 0;
        moved.guildGoldWithdrawn = 0;
        m_orders[newBotGuidLow] = Stored{moved};
        MarkDirtyLocked(m_orders[newBotGuidLow]);
        // Grandchildren follow the moved row: re-point their parent link to
        // the new bot so completion and failure still resolve a parent.
        for (auto& [guid, stored] : m_orders)
            if (stored.record.parentBotGuidLow == subOrderBotGuidLow)
            {
                stored.record.parentBotGuidLow = newBotGuidLow;
                MarkDirtyLocked(stored);
            }
        return true;
    }

    // One locked read-modify-write per Update pass: take the row, fold the
    // world-thread facts in, and hand back what to persist and what to send.
    // Everything else stays under the caller's single lock hold.
    struct TickIn
    {
        bool botOnline = false;
        bool botAlive = false;
        bool grouped = false;
        bool scanned = false;      // bag/mailbox walks ran this pass
        uint32_t bagCount = 0;       // order item in bags (delivery gate, reporting)
        uint32_t mailboxCount = 0;   // order item in the mailbox
        uint32_t farmItemId = 0;     // item actually farmed (0 = the order item)
        uint32_t farmBagCount = 0;   // farm-leg item in bags (no-progress clock)
        uint32_t bankCount = 0;      // guild bank stock, valid when bankChecked
        bool bankChecked = false;    // a fresh bank look came with this tick
        bool needItemAvailable = false;
        bool needItemInBank = false;
        bool needVendorReady = false; // a parked need item is vendor-unlimited and affordable now
        bool needAhReady = false;     // a parked need item is newly listed on the AH
        bool needRetryLadder = false; // a long-parked need row re-runs the whole ladder
        uint32_t unfinishedSubOrders = 0; // direct children still work in progress
        uint32_t workedMs = 0;
        uint32_t nowMs = 0;
    };
    struct TickOut
    {
        bool haveRow = false;
        OrderRecord record;
        uint32_t reportedHave = 0;
        uint32_t reportedCarry = 0;
        bool sendLine = false;
        bool finishedEdge = false;
        bool legChanged = false;
        bool needWhisper = false;
        bool persist = false;       // a row write waits outside the lock
        OrderRecord persistRecord;  // row snapshot to write (valid when persist)
    };

    TickOut Tick(uint32_t botGuidLow, TickIn const& in)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        TickOut out;
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return out;
        Stored& stored = it->second;
        OrderRecord& record = stored.record;
        out.haveRow = true;
        // A cancelled stub parks the guild-gold debt only: no travel, no
        // timers, no delivery. The refund drain pays it; FinishRepaid clears
        // the row once the gold is back.
        if (record.cancelled)
        {
            out.persist = TakeDirtyLocked(stored, out.persistRecord);
            out.record = record;
            return out;
        }

        bool wasFinished = IsFinished(record.state);
        bool wasStocked = record.state == OrderState::Stocked;

        if (record.state == OrderState::Waiting && record.reason == OrderReason::NeedItems &&
            (in.needItemAvailable || in.needVendorReady || in.needAhReady || in.needRetryLadder))
        {
            // A parked need row re-runs the ladder when its facts changed: a
            // vendor-unlimited item the new rule buys, a fresh AH listing, or
            // a restocked bank the denial flag had excluded. Bags stays the
            // player-delivered lane; the map thread re-plans the leg below.
            NeedResumeLeg leg = NeedResumeLegFor(in.needVendorReady, in.needItemInBank, in.needAhReady,
                in.needRetryLadder);
            record.state = OrderState::Working;
            record.reason = OrderReason::None;
            record.supplyStep = leg == NeedResumeLeg::Vendor ? SupplyStep::Farm :
                (leg == NeedResumeLeg::Bank ? SupplyStep::Bank :
                (leg == NeedResumeLeg::AuctionHouse ? SupplyStep::AuctionHouse :
                (leg == NeedResumeLeg::Ladder ? SupplyStep::None : SupplyStep::Bags)));
            stored.needLadderChecks = 0;
            record.needItems.clear();
            record.needWhispered = false;
            record.needBankDenied = false;
            // The player delivered: the stalled time below does not count as
            // no progress. Keep the worked total (the hours budget) intact.
            record.workedNoGainSec = 0;
            record.workedSinceNewItemSec = 0;
            MarkDirtyLocked(stored);
            out.legChanged = true;
        }

        if (record.state == OrderState::Working && !record.cancelled && !record.needItems.empty() &&
            (in.needItemInBank || in.needVendorReady || in.needAhReady || in.needRetryLadder))
        {
            // A lane hit on a working row clears the whole list and re-plans:
            // the ladder walks every item again, and one that is still a dead
            // end comes straight back without a new whisper (the token
            // already names it). The leg on some other item survives, unless
            // it sources a listed item.
            if (!KeepLegOnNeedClear(record.supplyItemId, record.needItems))
            {
                record.supplyStep = SupplyStep::None;
                record.supplyItemId = 0;
                record.supplyCount = 0;
                record.supplyReturnStep = SupplyStep::None;
            }
            record.needItems.clear();
            record.needBankDenied = false;
            stored.needLadderChecks = 0;
            if (in.needItemInBank)
                record.bankEmptyItemId = 0;
            MarkDirtyLocked(stored);
            out.legChanged = true;
        }

        // Keep watch: a fresh bank look below the stock target re-arms the
        // round (new quota, reset clocks, fresh plan) whether the bot is on
        // or off; the pool logs it back in like any unfinished order.
        if (wasStocked && record.keep && in.bankChecked)
        {
            record.lastBankCheckMs = in.nowMs;
            uint32_t missing = KeepMissing(record.stockTarget, in.bankCount);
            if (missing > 0)
            {
                record.state = OrderState::Working;
                record.reason = OrderReason::None;
                // A new round is a new conversation: the finished round's need
                // list is gone, and its whisper state (list, token, bank
                // denial) must not silence or gate this round's first list.
                record.needItems.clear();
                record.needWhispered = false;
                record.lastNeedWhisper.clear();
                record.needBankDenied = false;
                record.amount = missing;
                record.delivered = 0;
                record.farmZoneId = 0;
                record.farmChance = 0;
                record.zoneIndex = 0;
                record.farmItemId = 0;
                record.farmRest = false;
                it->second.auctionCycles.clear();
                it->second.auctionGoldReturnItemId = 0;
                record.deathsInZone = 0;
                record.searchFailsInZone = 0;
                record.searchZoneMoves = 0;
                record.workedSinceNewItemSec = 0;
                record.workedTotalSec = 0;
                record.workedNoGainSec = 0;
                record.reportedHave = 0;
                record.reportedCarry = 0;
                stored.lastOwnedCount = 0;
                stored.lastFarmCount = 0;
                stored.lastFarmItemId = 0;
                out.legChanged = true;
            }
            MarkDirtyLocked(stored);
        }
        // A deaths-driven zone move happened off the world thread: expire the
        // old travel target so the bot re-plans instead of walking on.
        if (stored.zoneMovePending)
        {
            stored.zoneMovePending = false;
            out.legChanged = true;
            MarkDirtyLocked(stored);
        }

        // Group gate: no order work while in a group with a player.
        if (!wasFinished)
        {
            if (in.grouped && record.state == OrderState::Working)
            {
                record.state = OrderState::Waiting;
                record.reason = OrderReason::Grouped;
                MarkDirtyLocked(stored);
            }
            else if (!in.grouped && record.state == OrderState::Waiting &&
                     record.reason == OrderReason::Grouped)
            {
                record.state = OrderState::Working;
                record.reason = OrderReason::None;
                MarkDirtyLocked(stored);
            }
        }

        // A parent is waiting on work, not idle, only while it has no leg of
        // its own (the ladder's WaitOnSubOrders return is step None) and a
        // child row is still unfinished: the clock stays paused then. A parent
        // working its own leg keeps every clock, so a stalled leg still ends
        // the round and hands the order back to the ladder.
        if (!wasFinished && in.botOnline && in.botAlive && !in.grouped &&
            record.state == OrderState::Working &&
            !PauseOrderClocks(record.supplyStep, static_cast<int>(in.unfinishedSubOrders)))
        {
            stored.workedCarryMs += in.workedMs;
            if (stored.workedCarryMs >= 1000)
            {
                uint32_t workedSec = stored.workedCarryMs / 1000;
                stored.workedCarryMs %= 1000;
                // Saturate: an unlimited order runs for hundreds of days
                // without wrapping its clocks back into a fresh hour.
                if (record.workedSinceNewItemSec > UINT32_MAX - workedSec)
                    record.workedSinceNewItemSec = UINT32_MAX;
                else
                    record.workedSinceNewItemSec += workedSec;
                if (record.workedTotalSec > UINT32_MAX - workedSec)
                    record.workedTotalSec = UINT32_MAX;
                else
                    record.workedTotalSec += workedSec;
                if (record.workedNoGainSec > UINT32_MAX - workedSec)
                    record.workedNoGainSec = UINT32_MAX;
                else
                    record.workedNoGainSec += workedSec;
                if (IsOrderTimeout(record.workedTotalSec, record.hours))
                {
                    // The farm time ran out: what the bot holds still goes to
                    // the guild (bags now, mailbox via the mail leg), and the
                    // order ends once the delivery drain reports back. With
                    // nothing held at all it ends right away, and a keep round
                    // ends the keep with it.
                    if (record.delivered + in.bagCount + in.mailboxCount > 0)
                    {
                        record.state = OrderState::Delivering;
                        record.reason = OrderReason::Timeout;
                        record.supplyStep = SupplyStep::Deliver;
                        record.supplyItemId = record.itemId;
                        record.supplyCount = 0;
                        record.supplyGoldCount = 0;
                    }
                    else
                    {
                        if (record.keep)
                        {
                            record.keep = false;
                            record.stockTarget = 0;
                        }
                        record.state = OrderState::Cant;
                        record.reason = OrderReason::Timeout;
                    }
                    MarkDirtyLocked(stored);
                    out.legChanged = true;
                }
                else if (SupplyFarmGivesUp(record.type == OrderType::Craft &&
                    record.supplyStep == SupplyStep::Farm && record.supplyItemId &&
                    record.reason != OrderReason::NoSource, record.workedNoGainSec,
                    true))
                {
                    // The reagent farm gives up; the ladder takes over with a
                    // fresh watchdog for the sub-order or need list.
                    record.reason = OrderReason::NoSource;
                    record.workedNoGainSec = 0;
                    MarkDirtyLocked(stored);
                    out.legChanged = true;
                }
                else if (IsOrderNoProgress(record.workedNoGainSec))
                {
                    // Same deliver-first end as the timeout: the bot brings
                    // what it holds, then the order ends cant/noprogress.
                    if (record.delivered + in.bagCount + in.mailboxCount > 0)
                    {
                        record.state = OrderState::Delivering;
                        record.reason = OrderReason::NoProgress;
                        record.supplyStep = SupplyStep::Deliver;
                        record.supplyItemId = record.itemId;
                        record.supplyCount = 0;
                        record.supplyGoldCount = 0;
                    }
                    else
                    {
                        if (record.keep)
                        {
                            record.keep = false;
                            record.stockTarget = 0;
                        }
                        record.state = OrderState::Cant;
                        record.reason = OrderReason::NoProgress;
                    }
                    MarkDirtyLocked(stored);
                    out.legChanged = true;
                }
                else if (MoveZoneLocked(stored))
                    out.legChanged = true;
                MaybeFlushWorkedLocked(stored, in.nowMs);
            }
        }
        else
            stored.workedCarryMs = 0;
        // A real rise in the order item (bags plus mailbox) or in the
        // farm-leg item (bags: reagents never arrive by mail) seen while
        // online resets the zone-move clock and the progress watchdog. A
        // login after an offline gap never counts as new items. The
        // farm-leg tracker re-snaps when the leg changes so a fresh reagent
        // starts from its own count.
        uint32_t ownedNow = in.bagCount + in.mailboxCount;
        // Off-cadence passes carry no scan: the zeros above are not item
        // loss, so they never touch the rise trackers below.
        if (!wasFinished && in.botOnline && in.scanned)
        {
            if (in.farmItemId != stored.lastFarmItemId)
            {
                stored.lastFarmItemId = in.farmItemId;
                stored.lastFarmCount = in.farmBagCount;
            }
            bool orderRise = IsNewItemRise(in.botOnline, stored.sawOnline, ownedNow, stored.lastOwnedCount);
            bool farmRise = IsNewItemRise(in.botOnline, stored.sawOnline, in.farmBagCount, stored.lastFarmCount);
            // A rise in the bags is order inflow (farm loot, a craft batch):
            // the leftover cap covers it, and the watchdog already treats it
            // as progress below. A login after an offline gap never counts.
            auto noteGain = [&](uint32_t itemId, uint32_t now, uint32_t last)
            {
                if (!itemId || now <= last)
                    return;
                uint32_t rise = now - last;
                for (NeedItem& gain : record.supplyGains)
                    if (gain.itemId == itemId)
                    {
                        gain.count = rise > UINT32_MAX - gain.count ? UINT32_MAX : gain.count + rise;
                        MarkDirtyLocked(stored);
                        return;
                    }
                record.supplyGains.push_back(NeedItem{ itemId, rise });
                MarkDirtyLocked(stored);
            };
            if (orderRise && record.itemId)
                noteGain(record.itemId, ownedNow, stored.lastOwnedCount);
            if (farmRise && in.farmItemId)
                noteGain(in.farmItemId, in.farmBagCount, stored.lastFarmCount);
            if ((orderRise || farmRise) && record.workedSinceNewItemSec != 0)
            {
                record.workedSinceNewItemSec = 0;
                MarkDirtyLocked(stored);
            }
            if ((orderRise || farmRise) && record.workedNoGainSec != 0)
            {
                record.workedNoGainSec = 0;
                MarkDirtyLocked(stored);
            }
            stored.lastOwnedCount = ownedNow;
            stored.lastFarmCount = in.farmBagCount;
            stored.sawOnline = true;
        }
        else if (!in.botOnline)
            stored.sawOnline = false;
        else if (in.botOnline && !in.scanned)
            stored.sawOnline = true;
        // Amount reached in bags: the bot heads for the guild bank (the
        // delivery action walks it there once package 4 lands; until then
        // the state alone marks it delivering). Mailbox items count for
        // progress and the give-up clock above, but delivery needs the
        // items in the bags - the bank deposit cannot take them from mail.
        // An oversized AH purchase inflates the bags with the order item
        // past the amount; delivery waits until the surplus is back on the
        // auction house (or the relist gave up), or the bank would take the
        // whole stack before it could be posted.
        if (!wasFinished && in.scanned && record.state == OrderState::Working && record.amount > 0 &&
            !stored.relistItemId && record.delivered + in.bagCount >= record.amount)
        {
            record.state = OrderState::Delivering;
            record.reason = OrderReason::None;
            record.supplyStep = SupplyStep::Deliver;
            record.supplyItemId = record.itemId;
            record.supplyCount = 0;
            record.supplyGoldCount = 0;
            out.legChanged = true;
            MarkDirtyLocked(stored);
        }
        if (!wasFinished && in.scanned && in.botOnline &&
            DeliveryLostItsStock(record.state, record.reason, record.delivered, record.amount, ownedNow))
        {
            record.state = OrderState::Working;
            record.reason = OrderReason::None;
            record.supplyStep = SupplyStep::None;
            record.supplyItemId = 0;
            record.supplyCount = 0;
            record.supplyGoldCount = 0;
            out.legChanged = true;
            MarkDirtyLocked(stored);
        }

        out.record = record;
        out.finishedEdge = !wasFinished && IsFinished(record.state);
        // The whisper goes out only when the giver is online to hear it: the
        // flag sets only on a sent line, so an offline giver is retried on a
        // later tick. The giver lookup rides the live-update pass that
        // already carries both players.
        if (NeedWhisperDue(record.state, record.reason, !record.needItems.empty(), record.needWhispered))
        {
            ObjectGuid giverGuid(HIGHGUID_PLAYER, record.giverGuidLow);
            Player* giver = sObjectAccessor.FindPlayer(giverGuid);
            if (giver && giver->IsInWorld())
            {
                out.needWhisper = true;
                record.needWhispered = true;
                record.lastNeedWhisper = NeedWhisperTokenUnion(record.lastNeedWhisper, record.needItems);
                MarkDirtyLocked(stored);
            }
        }

        // The row's have count is what reached the guild: delivered only,
        // never the bags. It still never drops: offline (or not yet re-seen)
        // bots keep max(delivered, last reported). A stocked order reports
        // the bank stock the last look saw: owned counting means nothing
        // there. Both counts are read off the row every pass, so a deposit
        // reports itself without waiting for the next scan.
        bool stocked = record.state == OrderState::Stocked;
        // A row whose hand-off succeeded (done after the bank deposit, sent
        // by mail) holds nothing for the order any more. A row that ended
        // cant or noskill still has the stack in the bags, so it keeps the
        // figure a scan last found. Reported before the line and the row
        // write below, so neither invents a figure.
        bool handOffSucceeded = HandOffSucceeded(record.state);
        out.reportedHave = ReportedHaveThisPass(stocked, in.bankChecked, in.bankCount,
            record.stockTarget, record.delivered, record.reportedHave);
        out.reportedCarry = ReportedCarryThisPass(record.state, in.scanned, in.botOnline, in.bagCount,
            in.mailboxCount, record.reportedCarry);
        // A fresh scan is the row's carry truth: fold it in and mark the row
        // dirty at once, so a bot that logs out before the next progress line
        // still lists what it carried. The zero after a hand-off is truth
        // too and reaches the row although no scan runs for it. Only the line
        // waits for the throttle.
        bool carryChanged = out.reportedCarry != record.reportedCarry;
        if ((in.scanned || handOffSucceeded) && carryChanged)
        {
            record.reportedCarry = out.reportedCarry;
            MarkDirtyLocked(stored);
        }
        bool carryToReport = out.reportedCarry != stored.reportCarry;

        // One timeline entry per change of state, reason, leg or leg item.
        if (OrderStepDiffers(stored.steps, record.state, record.reason, record.supplyStep, record.supplyItemId))
            NoteOrderStep(stored.steps, OrderStepEntry{ static_cast<uint64_t>(std::time(nullptr)),
                record.state, record.reason, record.supplyStep, record.supplyItemId });

        std::string needToken = NeedItemsToken(record.needItems);
        bool stateChanged = !stored.reportSeen || stored.reportState != record.state ||
                            stored.reportReason != record.reason || stored.reportStep != record.supplyStep ||
                            stored.reportNeed != needToken;
        if (!stored.reportSeen || stored.reportStep != record.supplyStep)
            out.legChanged = true;
        bool haveChanged = out.reportedHave != record.reportedHave;
        bool progressDue = (haveChanged || carryToReport) &&
                           (!stored.reportSeen || in.nowMs - stored.reportMs >= kProgressReportMs);
        out.sendLine = stateChanged || progressDue || out.finishedEdge;
        if (out.sendLine)
        {
            stored.reportSeen = true;
            stored.reportState = record.state;
            stored.reportReason = record.reason;
            stored.reportStep = record.supplyStep;
            stored.reportNeed = needToken;
            if (haveChanged || carryToReport)
                stored.reportMs = in.nowMs;
            stored.reportCarry = out.reportedCarry;
            if (haveChanged)
            {
                record.reportedHave = out.reportedHave;
                MarkDirtyLocked(stored);
            }
        }

        out.persist = TakeDirtyLocked(stored, out.persistRecord);
        out.record = record;
        return out;
    }

private:
    struct Stored
    {
        OrderRecord record;
        bool dirty = false;
        uint32_t workedCarryMs = 0;
        uint32_t lastWorkedFlushMs = 0;
        uint32_t lastOwnedCount = 0;
        uint32_t lastFarmItemId = 0;
        uint32_t lastFarmCount = 0;
        bool zoneMovePending = false; // NoteDeath moved zone off the world thread: expire the target next Tick
        std::unordered_map<uint32_t, std::vector<FarmZoneTargetFailure>> farmZoneFailures; // zone -> mesh-failed farm travel targets
        SupplySearchStreak supplySearch; // empty service-leg searches for the current leg
        OrderProtectedSet kept; // protected item ids for the row's keys (recomputed on the bot's map thread)
        bool sawOnline = false;
        bool reportSeen = false;
        OrderState reportState = OrderState::Working;
        OrderReason reportReason = OrderReason::None;
        uint32_t reportMs = 0;
        uint32_t reportCarry = 0; // carry the last line announced, not the row's stored carry
        uint32_t lastNeedBankCheckMs = 0;
        uint32_t lastNeedLadderCheckMs = 0;
        uint32_t needLadderChecks = 0; // slow checks since the row last parked
        uint32_t lastProgressScanMs = 0;
        SupplyStep reportStep = SupplyStep::None;
        std::string reportNeed;
        OrderStepHistory steps; // memory only: the addon's step timeline
        std::unordered_map<uint32_t, AuctionLegCycle> auctionCycles; // per reagent, memory only
        uint32_t auctionGoldReturnItemId = 0; // bank gold top-up returning to its auction visit
        uint32_t auctionReachStartedMs = 0; // arrived in the auctioneer's city; 0 = not yet
        uint32_t auctionLegStartedMs = 0;   // auction leg entered; bounds the travel
        uint32_t relistItemId = 0; // bought surplus waiting for its relist (memory only)
        uint32_t relistCount = 0;
        uint32_t relistUnitPrice = 0;
    };

    // Memory only, like the cycle itself: no row write for a bookkeeping
    // flag that a restart may drop.
    static void MarkAuctionLadderEndLocked(Stored& stored, uint32_t itemId)
    {
        if (!itemId)
            return;
        auto found = stored.auctionCycles.find(itemId);
        if (found != stored.auctionCycles.end())
            NoteAuctionLadderEnd(found->second, itemId);
    }

    // A zone move advances to the next zone of the plan (the wrap-around is
    // at pick time in the travel code) and resets the per-zone clocks.
    // Deaths never end the order; the hours budget in Tick does. Returns true
    // when the zone actually moved so the caller can expire the old target.
    static bool MoveZoneLocked(Stored& stored)
    {
        OrderRecord& record = stored.record;
        if (record.state != OrderState::Working)
            return false;
        if (ClassifyZoneMove(record.workedSinceNewItemSec, record.deathsInZone) ==
            ZoneMoveTrigger::None)
            return false;
        ++record.zoneIndex;
        record.farmZoneId = 0;
        record.farmChance = 0;
        record.workedSinceNewItemSec = 0;
        record.deathsInZone = 0;
        record.searchFailsInZone = 0;
        MarkDirtyLocked(stored);
        return true;
    }
    // A zone move after repeated failed searches: same advance-and-clear as
    // the death/no-progress move, without touching those clocks.
    static bool MoveZoneForSearchLocked(Stored& stored)
    {
        OrderRecord& record = stored.record;
        if (record.state != OrderState::Working)
            return false;
        ++record.zoneIndex;
        ++record.searchZoneMoves;
        record.farmZoneId = 0;
        record.farmChance = 0;
        record.searchFailsInZone = 0;
        MarkDirtyLocked(stored);
        return true;
    }

    static void MarkDirtyLocked(Stored& stored) { stored.dirty = true; }

    static void MaybeFlushWorkedLocked(Stored& stored, uint32_t nowMs)
    {
        if (stored.dirty || nowMs - stored.lastWorkedFlushMs < kWorkedFlushMs)
            return;
        stored.dirty = true;
        stored.lastWorkedFlushMs = nowMs;
    }

    static bool TakeDirtyLocked(Stored& stored, OrderRecord& snapshot)
    {
        if (!stored.dirty)
            return false;
        stored.dirty = false;
        snapshot = stored.record;
        return true;
    }

    // The UPDATE itself runs outside the store mutex: Tick and FinishRepaid
    // snapshot under the lock and write after, so a slow characters-database
    // stall never serializes the map-thread mutators. A failed write re-marks
    // the live row dirty so the next pass retries it.
    static bool TryWriteRecord(OrderRecord record)
    {
        record.updatedAt = static_cast<uint64_t>(std::time(nullptr));
        if (!CharacterDatabase.PExecute(
                "UPDATE `tortoise_bots_guild_order` SET `giver_guid` = '%u', `parent_bot` = '%u', `root_giver` = '%u', `amount` = '%u', `hours` = '%u', `delivered` = '%u', "
                "`state` = '%s', `reason` = '%s', "
                "`farm_zone` = '%u', `farm_chance` = '%u', `farm_item` = '%u', `zone_index` = '%u', "
                "`deaths_in_zone` = '%u', `worked_sec` = '%u', `worked_total` = '%u', `worked_nogain` = '%u', `reported` = '%u', "
                "`farm_rest` = '%u', `keep` = '%u', `gold_cap` = '%u', `gold_cap_set` = '%u', "
                "`guild_gold_withdrawn` = '%u', `guild_gold_held` = '%u', `cancelled` = '%u', `supply_step` = '%s', "
                "`supply_return_step` = '%s', `supply_item` = '%u', `supply_count` = '%u', `supply_gold_count` = '%u', "
                "`need_items` = '%s', `need_whispered` = '%u', `last_need_whisper` = '%s', `need_bank_denied` = '%u', `stock_target` = '%u', `last_bank_check` = '%u', `updated_at` = FROM_UNIXTIME('%u'), `carried` = '%u' "
                "WHERE `bot_guid` = '%u'",
                record.giverGuidLow, record.parentBotGuidLow, record.rootGiverGuidLow,
                record.amount, record.hours, record.delivered, StateToken(record.state), ReasonToken(record.reason),
                record.farmZoneId, record.farmChance, record.farmItemId, record.zoneIndex, record.deathsInZone,
                record.workedSinceNewItemSec, record.workedTotalSec, record.workedNoGainSec, record.reportedHave,
                record.farmRest ? 1u : 0u, record.keep ? 1u : 0u, record.goldCap, record.hasGoldCap ? 1u : 0u,
                record.guildGoldWithdrawn, record.guildGoldHeld, record.cancelled ? 1u : 0u,
                SupplyStepToken(record.supplyStep),
                SupplyStepToken(record.supplyReturnStep), record.supplyItemId, record.supplyCount, record.supplyGoldCount,
                NeedItemsToken(record.needItems).c_str(), record.needWhispered ? 1u : 0u,
                record.lastNeedWhisper.c_str(), record.needBankDenied ? 1u : 0u,
                record.stockTarget, record.lastBankCheckMs,
                static_cast<uint32_t>(record.updatedAt), record.reportedCarry, record.botGuidLow))
        {
            sLog.outError("TortoiseBots: failed to persist guild order for bot %u", record.botGuidLow);
            return false;
        }
        return true;
    }

    void MarkDirty(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it != m_orders.end())
            MarkDirtyLocked(it->second);
    }

    std::mutex m_mutex;
    std::unordered_map<uint32_t, Stored> m_orders;
    // Relisted auction surplus per bot, oldest first: the guild-funded value
    // that is still on the auction house, in the mail or in the bot's hands.
    std::unordered_map<uint32_t, std::vector<RelistLedgerEntry>> m_relist;
    uint64_t m_lastRelistExpireSec = 0;
    std::unordered_map<QueuedSubOrderKey, QueuedSubOrder, QueuedSubOrderKeyHash> m_queued;
    // Last request the drain gave planner time to: the next drain starts
    // after it, so one request cannot spend every tick's evaluation budget.
    uint32_t m_drainedParentBotGuidLow = 0;
    uint32_t m_drainedItemId = 0;
    bool m_initialized = false;
};

void SendOrderLine(OrderRecord const& order, uint32_t have, uint32_t carry);

std::string ZoneName(uint32_t zoneId)
{
    if (!zoneId)
        return "-";
    AreaTableEntry const* entry = GetAreaEntryByAreaID(zoneId);
    if (!entry)
        return "-";
    AreaTableEntry const* zone = entry->ZoneId ? GetAreaEntryByAreaID(entry->ZoneId) : entry;
    if (!zone || !zone->Name)
        return "-";
    return zone->Name;
}

std::string BotName(uint32_t botGuidLow)
{
    ObjectGuid guid(HIGHGUID_PLAYER, botGuidLow);
    if (Player* bot = sObjectAccessor.FindPlayer(guid))
        return bot->GetName();
    std::string name;
    if (sObjectMgr.GetPlayerNameByGUID(guid, name))
        return name;
    return "?";
}

// Bought AH items arrive by mail: progress counts delivered plus bags plus
// the order items waiting in the mailbox. World thread only, called from
// Update and Progress where the live Player is already at hand.
uint32_t MailboxItems(Player* bot, uint32_t itemId)
{
    if (!bot || !bot->GetSession() || !itemId)
        return 0;
    MasterPlayer* master = bot->GetSession()->GetMasterPlayer();
    if (!master)
        return 0;
    uint32_t count = 0;
    time_t curTime = time(nullptr);
    for (PlayerMails::iterator itr = master->GetMailBegin(); itr != master->GetMailEnd(); ++itr)
    {
        Mail* mail = *itr;
        if (!mail || mail->state == MAIL_STATE_DELETED || curTime < mail->deliver_time)
            continue;
        if (!mail->has_items)
            continue;
        for (MailItemInfoVec::const_iterator itemItr = mail->items.begin(); itemItr != mail->items.end(); ++itemItr)
            if (itemItr->item_template == itemId)
            {
                Item* mailItem = master->GetMItem(itemItr->item_guid);
                count += mailItem ? mailItem->GetCount() : 1;
            }
    }
    return count;
}

// Guild bank stock of one item for a keep watch: a single summed read. The
// bank lives only in memory, so the count goes through its table (saved on
// every change) while the guild object itself is the existence check.
uint32_t BankItemCount(uint32_t guildId, uint32_t itemId)
{
    if (!guildId || !itemId || !sGuildMgr.GetGuildById(guildId))
        return 0;
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
        "SELECT SUM(`count`) FROM `guild_bank` WHERE `guildid` = '%u' AND `isInferno` = '0' AND "
        "`item_template` = '%u'",
        guildId, itemId));
    if (!result)
        return 0;
    Field* fields = result->Fetch();
    return fields ? fields[0].GetUInt32() : 0;
}

// A parked need item the vendor leg can buy now: a vendor sells it without
// limit (template rows included, through the populated vendor sets) and the
// bot holds one bundle's price. Offline bots never qualify: no purse to
// read, and the vendor trip needs them online anyway.
bool NeedVendorAffordable(Player* bot, bool online, NeedItem const& need)
{
    if (!online || !bot || !need.itemId || !need.count)
        return false;
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(need.itemId);
    if (!proto || !::ai::ItemUsageValue::IsItemSoldByAnyVendorWithoutLimit(proto))
        return false;
    uint32_t buyCount = proto->BuyCount ? proto->BuyCount : 1;
    uint32_t bundles = (need.count + buyCount - 1) / buyCount;
    if (!bundles)
        return false;
    uint64_t price = uint64_t(proto->BuyPrice) * bundles;
    return price > 0 && bot->GetMoney() >= price;
}

// A parked need item with a fresh AH listing: any buyout listing the mirror
// holds. Price and affordability stay with the buy action; this only wakes
// the row so the ladder re-tries the leg.
bool NeedAhListed(uint32_t itemId)
{
    if (!itemId)
        return false;
    return !sRandomBotFacade.GetAhPrices(itemId).empty();
}

// Depth of the bot's branch: top-level orders count 1, each parent link adds
// one. Queued requests are not rows yet, so the branch is only placed rows.
size_t BranchDepth(uint32_t botGuidLow)
{
    size_t depth = 0;
    uint32_t current = botGuidLow;
    for (size_t guard = 0; guard < 64 && current; ++guard)
    {
        std::optional<OrderRecord> row = Find(current);
        if (!row)
            break;
        ++depth;
        current = row->parentBotGuidLow;
    }
    return depth;
}

// Items already on the branch from the root to the parent, for the loop
// guard: the same item never appears twice in one branch.
std::vector<uint32_t> BranchItems(uint32_t parentBotGuidLow)
{
    std::vector<uint32_t> items;
    uint32_t current = parentBotGuidLow;
    for (size_t guard = 0; guard < 64 && current; ++guard)
    {
        std::optional<OrderRecord> row = Find(current);
        if (!row)
            break;
        items.push_back(row->itemId);
        current = row->parentBotGuidLow;
    }
    return items;
}
// Busy-only requests retry on the shared policy timer instead of every
// tick: one member query per queued request per retry interval.
// One drained queue entry: pick a durable guild bot that can make the item,
// or farm it when no maker is free. One guild_member query per guild per
// tick shared across due requests; planner evaluations share a per-tick
// budget (the rest requeue on the retry timer); depth and loop checks read
// one All snapshot instead of per-request Find chains.
bool PlaceSubOrders(uint32_t nowMs)
{
    if (!sPlayerbotAIConfig.guildOrderSubOrders)
        return false;
    uint32_t maxDepth = sPlayerbotAIConfig.guildOrderSubOrderDepth;
    if (maxDepth < 1 || maxDepth > 16)
        maxDepth = kSubOrderDefaultDepth;
    std::vector<OrderStore::QueuedSubOrder> queued = OrderStore::Instance().TakeDueQueuedSubOrders(nowMs);
    if (queued.empty())
        return false;
    std::vector<OrderRecord> rows;
    std::unordered_map<uint32_t, OrderRecord const*> byBot;
    auto refreshBranches = [&]()
    {
        rows = OrderStore::Instance().All();
        byBot.clear();
        byBot.reserve(rows.size());
        for (OrderRecord const& row : rows)
            byBot.emplace(row.botGuidLow, &row);
    };
    refreshBranches();
    auto depthOf = [&](uint32_t botGuidLow)
    {
        size_t depth = 0;
        uint32_t current = botGuidLow;
        for (size_t guard = 0; guard < 64 && current; ++guard)
        {
            auto found = byBot.find(current);
            if (found == byBot.end())
                break;
            ++depth;
            current = found->second->parentBotGuidLow;
        }
        return depth;
    };
    auto branchHas = [&](uint32_t parentBotGuidLow, uint32_t itemId)
    {
        uint32_t current = parentBotGuidLow;
        for (size_t guard = 0; guard < 64 && current; ++guard)
        {
            auto found = byBot.find(current);
            if (found == byBot.end())
                break;
            if (found->second->itemId == itemId)
                return true;
            current = found->second->parentBotGuidLow;
        }
        return false;
    };
    std::unordered_map<uint32_t, std::vector<uint32_t>> membersByGuild;
    size_t plannerEvals = 0;
    auto evalUsable = [&](Player* bot, uint32_t itemId, bool& canMake, bool& canFarm, bool& gathers)
    {
        if (plannerEvals >= kSubOrderPlannerEvalsPerTick)
            return false;
        ++plannerEvals;
        return SubOrderCandidateUsable(bot, itemId, canMake, canFarm, gathers);
    };
    // The placement's busy rule, shared by the scan and the pre-Give
    // re-check: only a running row or a stocked keep watch blocks a bot.
    auto busyOrder = [](std::optional<OrderRecord> const& existing)
    {
        return existing.has_value() && !existing->cancelled &&
            (!IsFinished(existing->state) || existing->state == OrderState::Stocked);
    };
    bool placedAny = false;
    // The request the drain gave planner time to last: the next drain starts
    // after it, so one request cannot take every tick's evaluations.
    uint32_t drainedParent = 0;
    uint32_t drainedItem = 0;
    for (OrderStore::QueuedSubOrder const& request : queued)
    {
        if (!request.itemId || !request.count)
            continue;
        std::optional<OrderRecord> parent = Find(request.parentBotGuidLow);
        if (!parent || IsFinished(parent->state))
            continue;
        // Every terminal path below reports through the failure entry: the
        // parent never waits on a request that cannot become a running row.
        // An exhausted planner budget requeues instead of failing: the
        // request is still placeable, just not this tick.
        if (!SubOrderDepthAllows(depthOf(parent->botGuidLow), maxDepth))
        {
            NoteSubOrderFailed(parent->botGuidLow, request.itemId, request.count);
            continue;
        }
        if (branchHas(parent->botGuidLow, request.itemId))
        {
            NoteSubOrderFailed(parent->botGuidLow, request.itemId, request.count);
            continue;
        }
        auto cached = membersByGuild.find(parent->guildId);
        if (cached == membersByGuild.end())
        {
            std::vector<uint32_t> members;
            // Ordered: the scan resumes at a guid, so an unordered result
            // would move the resume point between attempts and skip members.
            std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
                "SELECT `guid` FROM `guild_member` WHERE `guildid` = '%u' ORDER BY `guid`",
                parent->guildId));
            if (result)
            {
                do
                {
                    Field* fields = result->Fetch();
                    uint32_t guidLow = fields[0].GetUInt32();
                    if (guidLow && guidLow != parent->botGuidLow)
                        members.push_back(guidLow);
                } while (result->NextRow());
            }
            cached = membersByGuild.emplace(parent->guildId, std::move(members)).first;
        }
        std::vector<uint32_t> const& roster = cached->second;
        size_t const rosterSize = roster.size();
        // The scan resumes after the last member it looked at, so a guild
        // bigger than one tick's evaluations is covered over retries instead
        // of re-scanning the same head, and a member leaving or joining
        // between attempts cannot shift the resume point onto another one.
        SubOrderScanPlan const plan = PlanSubOrderScan(roster.data(), rosterSize,
            request.scan.lastMember, request.scan.scanStart);
        // Best free candidate and busy verdict found in the earlier slices; a
        // request without a running scan starts a new full scan and forgets
        // them.
        SubOrderScanBest best;
        if (request.scan.scanStart)
        {
            best.guid = request.scan.bestGuid;
            best.rank = request.scan.bestRank;
            best.canMake = request.scan.bestCanMake;
            best.sawBusyUsable = request.scan.sawBusyUsable;
        }
        size_t const evalsBefore = plannerEvals;
        size_t looked = 0;
        for (; looked < plan.remaining; ++looked)
        {
            if (plannerEvals >= kSubOrderPlannerEvalsPerTick)
                break;
            uint32_t guidLow = roster[(plan.begin + looked) % rosterSize];
            ObjectGuid guid(HIGHGUID_PLAYER, guidLow);
            Player* bot = sObjectAccessor.FindPlayer(guid);
            if (!bot || !bot->IsInWorld() || bot->GetGuildId() != parent->guildId)
                continue;
            if (!IsDurableOrderBot(guidLow))
                continue;
            SubOrderCandidate candidate;
            candidate.botGuidLow = guidLow;
            candidate.online = true;
            std::optional<OrderRecord> existing = Find(guidLow);
            // A finished order only lingers in the list; a keep order still
            // watches the bank, so only that one keeps the bot busy.
            candidate.hasOrder = busyOrder(existing);
            candidate.keepHolder = candidate.hasOrder &&
                existing->state == OrderState::Stocked && existing->keep;
            if (!evalUsable(bot, request.itemId, candidate.canMake, candidate.canFarm,
                candidate.gathers))
                continue;
            // A busy candidate keeps the verdicts: only a busy bot that can
            // make or farm the item makes the request worth retrying.
            candidate.queuedBusy = candidate.hasOrder;
            NoteSubOrderCandidate(best, candidate);
        }
        if (plannerEvals > evalsBefore)
        {
            drainedParent = request.parentBotGuidLow;
            drainedItem = request.itemId;
        }
        SubOrderScanCursor const scan = AdvanceSubOrderScan(roster.data(), rosterSize, plan,
            request.scan.lastMember, looked);
        // The scan is decided once it has covered the roster: an attempt that
        // stopped early carries what it found into the next slice instead of
        // placing a candidate a later slice would outrank.
        if (!scan.complete)
        {
            OrderStore::Instance().RetryQueuedSubOrder(parent->botGuidLow, request.itemId,
                request.count, nowMs + kSubOrderSliceRetryMs, request.queuedAtMs,
                OrderStore::QueuedSubOrderScan{ scan.lastGuid, scan.startGuid, best.guid,
                    best.rank, best.canMake, best.sawBusyUsable, request.scan.busyScans });
            continue;
        }
        SubOrderPick const pick = ClassifySubOrderScan(best, request.scan.busyScans + 1);
        if (pick == SubOrderPick::Queued)
        {
            // Only busy members could take it: keep the request queued and
            // scan the roster again on the next retry, until the streak of
            // busy-only scans runs out and the item joins the need list.
            OrderStore::Instance().RetryQueuedSubOrder(parent->botGuidLow, request.itemId,
                request.count, nowMs + kSubOrderRetryMs, request.queuedAtMs,
                OrderStore::QueuedSubOrderScan{ 0, 0, 0, -1, false, false,
                    request.scan.busyScans + 1 });
            continue;
        }
        if (pick != SubOrderPick::Take)
        {
            NoteSubOrderFailed(parent->botGuidLow, request.itemId, request.count);
            continue;
        }
        // The scan may have carried this candidate over several ticks, and
        // Give REPLACEs whatever order the bot holds now: re-run the member
        // checks and restart the walk when it went stale in between.
        ObjectGuid bestGuid(HIGHGUID_PLAYER, best.guid);
        Player* bestBot = sObjectAccessor.FindPlayer(bestGuid);
        SubOrderRecheck recheck;
        recheck.found = bestBot != nullptr;
        recheck.inWorld = bestBot && bestBot->IsInWorld();
        recheck.sameGuild = bestBot && bestBot->GetGuildId() == parent->guildId;
        recheck.durable = IsDurableOrderBot(best.guid);
        recheck.hasOrder = busyOrder(Find(best.guid));
        if (!SubOrderRecheckPasses(recheck))
        {
            OrderStore::Instance().RetryQueuedSubOrder(parent->botGuidLow, request.itemId,
                request.count, nowMs + kSubOrderSliceRetryMs, request.queuedAtMs,
                OrderStore::QueuedSubOrderScan{});
            continue;
        }
        OrderRecord record;
        record.botGuidLow = best.guid;
        record.guildId = parent->guildId;
        record.giverGuidLow = parent->botGuidLow;
        record.parentBotGuidLow = parent->botGuidLow;
        record.rootGiverGuidLow = parent->rootGiverGuidLow ? parent->rootGiverGuidLow : parent->giverGuidLow;
        record.type = best.canMake ? OrderType::Craft : OrderType::Farm;
        record.itemId = request.itemId;
        record.amount = request.count;
        record.goldCap = parent->goldCap;
        record.hasGoldCap = parent->hasGoldCap;
        if (!Give(record))
        {
            // A fresh scan re-evaluates the roster on the next retry.
            OrderStore::Instance().RetryQueuedSubOrder(parent->botGuidLow, request.itemId,
                request.count, nowMs + kSubOrderRetryMs, request.queuedAtMs,
                OrderStore::QueuedSubOrderScan{});
            continue;
        }
        placedAny = true;
        // A new row joins the branch: later same-tick requests see its
        // depth and item instead of the pre-tick snapshot.
        refreshBranches();
        if (std::optional<OrderRecord> placed = Find(record.botGuidLow))
        {
            Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, placed->botGuidLow));
            SendOrderLine(*placed, 0, Carry(bot, *placed));
        }
    }
    if (drainedParent)
        OrderStore::Instance().NoteSubOrderDrained(drainedParent, drainedItem);
    return placedAny;
}

void SendOrderLine(OrderRecord const& order, uint32_t have, uint32_t carry)
{
    uint32_t giverLow = order.rootGiverGuidLow ? order.rootGiverGuidLow : order.giverGuidLow;
    Player* giver = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, giverLow));
    if (!giver || !giver->IsInWorld())
        return;
    giver->SendAddonMessage("TBM", FormatOrderLine(order.guildId, order, have, carry));
}

void SendNeedWhisper(OrderRecord const& order)
{
    Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, order.botGuidLow));
    Player* giver = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, order.giverGuidLow));
    if (!bot || !bot->IsInWorld() || !giver || !giver->IsInWorld())
        return;
    PlayerbotAI* botAi = ::PlayerbotAIStorage::Instance().GetAI(bot);
    if (!botAi)
        return;

    ItemPrototype const* output = sObjectMgr.GetItemPrototype(order.itemId);
    std::string message = "To make " + (output ? output->Name1 : std::to_string(order.itemId)) + " I still need ";
    for (NeedItem const& need : order.needItems)
    {
        if (message.back() != ' ')
            message += ", ";
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(need.itemId);
        message += std::to_string(need.count) + " " + (proto ? proto->Name1 : std::to_string(need.itemId));
    }
    message += "; trade it, mail it or put it in the guild bank";
    botAi->TellPlayerNoFacing(giver, message, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false);
}

} // namespace

// Durable module bots only: claimed into a guild, or on a random-pool
// account. Never autonomous alts, never hired bots (they belong to a
// player), never real players' own characters. Same-guild membership is a
// separate live check at each call site (member rows plus GetGuildId()).
bool IsDurableOrderBot(uint32_t guidLow)
{
    if (!guidLow)
        return false;
    ObjectGuid guid(HIGHGUID_PLAYER, guidLow);
    if (!BotManager::Instance().IsBot(guid))
        return false;
    if (AltBotService::Instance().IsAutonomous(guidLow))
        return false;
    if (HireLifecycle::Instance().IsHired(guid))
        return false;
    if (ClaimLifecycle::Instance().IsClaimed(guidLow))
        return true;
    uint32_t accountId = sObjectMgr.GetPlayerAccountIdByGUID(guid);
    return accountId && RandomBotAccountRegistry::Instance().IsRegistered(accountId);
}

bool SubOrderCandidateUsable(Player* bot, uint32_t itemId, bool& canMake, bool& canFarm, bool& gathers)
{
    canMake = false;
    canFarm = false;
    gathers = false;
    if (!bot || !bot->IsInWorld() || !itemId)
        return false;
    bool const maker = FindCraftSpellFor(bot, itemId) != 0;
    // The planner walks every spawn slice: a maker without a gathering skill
    // skips it, since only a gatherer may outrank its recipe.
    if (maker && !bot->HasSkill(SKILL_SKINNING) && !bot->HasSkill(SKILL_HERBALISM) &&
        !bot->HasSkill(SKILL_MINING) && !bot->HasSkill(SKILL_FISHING))
    {
        canMake = true;
        return true;
    }
    SourcePlan plan = ai::PlanGuildOrderSources(bot, itemId, OrderType::Farm);
    canFarm = plan.verdict == OrderReason::None && !plan.zones.empty();
    // A raw material the bot gathers itself is farmed, not crafted: the
    // recipe chain under it multiplies into hundreds of reagents.
    gathers = canFarm && (plan.sources & (SourceSkin | SourceHerb | SourceOre | SourceFish)) != 0;
    if (maker && !gathers)
    {
        canMake = true;
        canFarm = false;
        return true;
    }
    return canFarm;
}

bool IsAvailable()
{
    return OrderStore::Instance().IsAvailable();
}

std::optional<OrderRecord> Find(uint32_t botGuidLow)
{
    return OrderStore::Instance().Find(botGuidLow);
}

std::optional<OrderStepHistory> FindStepHistory(uint32_t botGuidLow)
{
    return OrderStore::Instance().FindStepHistory(botGuidLow);
}

bool IsOrderableItem(ItemPrototype const* proto)
{
    return proto && !proto->Name1.empty() && proto->Bonding != BIND_WHEN_PICKED_UP &&
           proto->Bonding != BIND_QUEST_ITEM && proto->Class != ITEM_CLASS_QUEST && proto->Duration == 0;
}

std::vector<OrderRecord> ListForGuild(uint32_t guildId)
{
    return OrderStore::Instance().ListForGuild(guildId);
}

std::vector<OrderRecord> All()
{
    return OrderStore::Instance().All();
}

bool IsOrderProtectedItem(Player* bot, uint32_t itemId)
{
    return OrderStore::Instance().IsProtectedItem(bot, itemId);
}

void RefreshOrderProtectedSet(Player* bot)
{
    OrderStore::Instance().RefreshProtectedSet(bot);
}

bool Give(OrderRecord record)
{
    return OrderStore::Instance().Give(record);
}

bool Cancel(uint32_t botGuidLow)
{
    std::optional<OrderRecord> doomed = Find(botGuidLow);
    uint32_t parent = doomed ? doomed->parentBotGuidLow : 0;
    uint32_t itemId = doomed ? doomed->itemId : 0;
    uint32_t amount = doomed ? doomed->amount : 0;
    std::vector<uint32_t> branch = OrderStore::Instance().BranchBots(botGuidLow);
    bool gone = OrderStore::Instance().Cancel(botGuidLow);
    if (gone)
    {
        // A row that detached into a final top-level delivery keeps its
        // travel target and queue state: the delivery path still runs.
        if (std::optional<OrderRecord> live = Find(botGuidLow))
        {
            OrderStore::Instance().WriteAndRescue(*live);
            Player* liveBot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, botGuidLow));
            SendOrderLine(*live, live->reportedHave, Carry(liveBot, *live));
        }
        else
        {
            OnDeliveryOrderGone(botGuidLow);
            OnSupplyOrderGone(botGuidLow);
            ExpireGuildOrderTarget(botGuidLow);
        }
        for (uint32_t child : branch)
        {
            if (child == botGuidLow)
                continue;
            OrderStore::Instance().Cancel(child);
            // A child that detached into a final top-level delivery keeps
            // its row (and its target): only reaped rows drop theirs.
            if (std::optional<OrderRecord> live = Find(child))
            {
                OrderStore::Instance().WriteAndRescue(*live);
                Player* liveBot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, child));
                SendOrderLine(*live, live->reportedHave, Carry(liveBot, *live));
                continue;
            }
            OnDeliveryOrderGone(child);
            OnSupplyOrderGone(child);
            ExpireGuildOrderTarget(child);
        }
        if (parent)
            NoteSubOrderFailed(parent, itemId, amount);
    }
    return gone;
}

bool SubOrderDelivered(uint32_t parentBotGuidLow, uint32_t itemId)
{
    return OrderStore::Instance().TakeDeliveredSubOrder(parentBotGuidLow, itemId);
}

bool RequestSupply(uint32_t parentBotGuidLow, uint32_t itemId, uint32_t count)
{
    if (!sPlayerbotAIConfig.guildOrderSubOrders || !itemId || !count)
        return false;
    // A reagent any vendor sells without limit is bought by the crafter,
    // never delegated: the placer reports failure so the ladder falls back
    // to the need list. npc_vendor rows only (per-entry stock); a template
    // row would need the creature's vendor_id, which the store never reads.
    if (VendorSellsUnlimited(itemId))
        return false;
    std::optional<OrderRecord> parent = Find(parentBotGuidLow);
    if (!parent)
        return false;
    std::optional<OrderRecord> existing;
    // A parked refund stub is not a running row: it never counts here, so
    // a dead stub cannot stall the parent waiting on it.
    for (uint32_t child : OrderStore::Instance().BranchBots(parentBotGuidLow))
    {
        if (child == parentBotGuidLow)
            continue;
        std::optional<OrderRecord> row = Find(child);
        if (row && !row->cancelled && row->itemId == itemId && !IsFinished(row->state))
            existing = row;
    }
    if (existing)
        return true;
    return OrderStore::Instance().QueueSubOrder(parentBotGuidLow, itemId, count);
}
uint32_t BranchGoldCapRemaining(uint32_t botGuidLow)
{
    uint32_t remaining = UINT32_MAX;
    bool capped = false;
    uint32_t current = botGuidLow;
    for (size_t guard = 0; current && guard < 64; ++guard)
    {
        std::optional<OrderRecord> row = Find(current);
        if (!row)
            break;
        if (row->hasGoldCap)
        {
            capped = true;
            uint32_t left = row->guildGoldWithdrawn >= row->goldCap ? 0 : row->goldCap - row->guildGoldWithdrawn;
            if (left < remaining)
                remaining = left;
        }
        current = row->parentBotGuidLow;
    }
    return capped ? remaining : UINT32_MAX;
}

void NoteSubOrderFailed(uint32_t parentBotGuidLow, uint32_t itemId, uint32_t count)
{
    OrderStore::Instance().NoteSubOrderFailed(parentBotGuidLow, itemId, count);
}

bool ReassignSubOrder(uint32_t subOrderBotGuidLow, uint32_t newBotGuidLow, OrderType type)
{
    bool moved = OrderStore::Instance().ReassignSubOrder(subOrderBotGuidLow, newBotGuidLow, type);
    if (!moved)
        return false;
    // A parked refund stub keeps its row; only a fully moved row is deleted.
    if (!Find(subOrderBotGuidLow))
        CharacterDatabase.DirectPExecute(
            "DELETE FROM `tortoise_bots_guild_order` WHERE `bot_guid` = '%u'", subOrderBotGuidLow);
    std::optional<OrderRecord> row = Find(newBotGuidLow);
    if (!row)
        return false;
    // The old bot's unspent guild gold stays a parked stub on the old bot
    // (same as cancel): the refund drain collects it at the next banker
    // visit instead of the debt dying with the row.
    OrderRecord record = *row;
    if (!OrderStore::Instance().Give(record))
    {
        NoteSubOrderFailed(record.parentBotGuidLow, record.itemId, record.amount);
        OrderStore::Instance().Cancel(newBotGuidLow);
        return false;
    }
    Player* newBot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, record.botGuidLow));
    SendOrderLine(record, row->reportedHave, Carry(newBot, record));
    return true;
}

std::vector<uint32_t> BranchBots(uint32_t botGuidLow)
{
    return OrderStore::Instance().BranchBots(botGuidLow);
}

std::vector<uint32_t> CoveredSubOrderItems(uint32_t botGuidLow)
{
    return OrderStore::Instance().CoveredSubOrderItems(botGuidLow);
}

std::vector<uint32_t> CollectableSubOrderItems(uint32_t botGuidLow)
{
    return OrderStore::Instance().CollectableSubOrderItems(botGuidLow);
}

void SetFarmZone(uint32_t botGuidLow, uint32_t zoneId, uint8_t chance, uint32_t zoneIndex, uint32_t farmItemId)
{
    OrderStore::Instance().SetFarmZone(botGuidLow, zoneId, chance, zoneIndex, farmItemId);
}

void SetState(uint32_t botGuidLow, OrderState state, OrderReason reason)
{
    OrderStore::Instance().SetState(botGuidLow, state, reason);
}

void SetSupply(uint32_t botGuidLow, SupplyStep step, uint32_t itemId, uint32_t count,
    SupplyStep returnStep, OrderReason reason, uint32_t goldCount)
{
    OrderStore::Instance().SetSupply(botGuidLow, step, itemId, count, returnStep, reason, goldCount);
}

void SetNeedItems(uint32_t botGuidLow, std::vector<NeedItem> const& items, bool bankDenied)
{
    OrderStore::Instance().SetNeedItems(botGuidLow, items, bankDenied);
}

void AddNeedItem(uint32_t botGuidLow, uint32_t itemId, uint32_t count, bool bankDenied)
{
    OrderStore::Instance().AddNeedItem(botGuidLow, itemId, count, bankDenied);
}

bool ReplaceNeedItems(uint32_t botGuidLow, std::vector<NeedItem> const& expected,
    std::vector<NeedItem> const& items)
{
    return OrderStore::Instance().ReplaceNeedItems(botGuidLow, expected, items);
}

void RecordGuildGoldWithdrawal(uint32_t botGuidLow, uint32_t amount)
{
    OrderStore::Instance().RecordGuildGoldWithdrawal(botGuidLow, amount);
}

void ConsumeGuildGoldHeld(uint32_t botGuidLow, uint32_t amount)
{
    OrderStore::Instance().ConsumeGuildGoldHeld(botGuidLow, amount);
}

void RefundGuildGold(uint32_t botGuidLow, uint32_t deposited)
{
    OrderStore::Instance().RefundGuildGold(botGuidLow, deposited);
}

void NoteSupplyGain(uint32_t botGuidLow, uint32_t itemId, uint32_t count)
{
    OrderStore::Instance().NoteSupplyGain(botGuidLow, itemId, count);
}

void NoteSupplySpent(uint32_t botGuidLow, uint32_t itemId, uint32_t count)
{
    OrderStore::Instance().NoteSupplySpent(botGuidLow, itemId, count);
}

void NoteSupplyProgress(uint32_t botGuidLow)
{
    OrderStore::Instance().NoteSupplyProgress(botGuidLow);
}

void NoteBankEmpty(uint32_t botGuidLow, uint32_t itemId)
{
    OrderStore::Instance().NoteBankEmpty(botGuidLow, itemId);
}

void ClearBankEmpty(uint32_t botGuidLow)
{
    OrderStore::Instance().ClearBankEmpty(botGuidLow);
}
void NoteMailEmpty(uint32_t botGuidLow, uint32_t itemId)
{
    OrderStore::Instance().NoteMailEmpty(botGuidLow, itemId);
}

void ClearMailEmpty(uint32_t botGuidLow)
{
    OrderStore::Instance().ClearMailEmpty(botGuidLow);
}

uint32_t NoteSupplySearchFailed(uint32_t botGuidLow, SupplyStep step, uint32_t itemId)
{
    return OrderStore::Instance().NoteSupplySearchFailed(botGuidLow, step, itemId);
}

void StartKeepWatch(uint32_t botGuidLow)
{
    OrderStore::Instance().StartKeepWatch(botGuidLow, WorldTimer::getMSTime());
}

void DropKeep(uint32_t botGuidLow)
{
    OrderStore::Instance().DropKeep(botGuidLow);
}

void NoteFarmRest(uint32_t botGuidLow)
{
    OrderStore::Instance().NoteFarmRest(botGuidLow);
}

void NoteDeath(uint32_t botGuidLow)
{
    OrderStore::Instance().NoteDeath(botGuidLow);
}

void NoteFarmZoneTargetFailed(uint32_t botGuidLow, uint32_t zoneId, uint32_t mapId,
    float x, float y, float z)
{
    OrderStore::Instance().NoteFarmZoneTargetFailed(botGuidLow, zoneId, mapId, x, y, z);
}

std::vector<FarmZoneTargetFailure> FarmZoneFailures(uint32_t botGuidLow, uint32_t zoneId)
{
    return OrderStore::Instance().GetFarmZoneFailures(botGuidLow, zoneId);
}

void NoteSearchFailed(uint32_t botGuidLow, uint32_t zoneId)
{
    OrderStore::Instance().NoteSearchFailed(botGuidLow, zoneId);
}

void NoteSearchLanded(uint32_t botGuidLow)
{
    OrderStore::Instance().NoteSearchLanded(botGuidLow);
}

void CommitAuctionZone(uint32_t botGuidLow, uint32_t zoneId)
{
    OrderStore::Instance().CommitAuctionZone(botGuidLow, zoneId);
}

uint32_t NoteArrivedAuctionVisit(uint32_t botGuidLow, bool auctioneerInReach)
{
    return OrderStore::Instance().NoteArrivedAuctionVisit(botGuidLow, auctioneerInReach);
}

bool AuctionReentryAllowed(uint32_t botGuidLow, uint32_t itemId)
{
    return OrderStore::Instance().AuctionReentryAllowed(botGuidLow, itemId);
}

void NoteAuctionLegEnded(uint32_t botGuidLow, OrderReason reason)
{
    OrderStore::Instance().NoteAuctionLegEnded(botGuidLow, reason);
}

bool AuctionLegUnreachable(uint32_t botGuidLow, uint32_t nowMs)
{
    return OrderStore::Instance().AuctionLegUnreachable(botGuidLow, nowMs);
}

void NoteAuctioneerInReach(uint32_t botGuidLow)
{
    OrderStore::Instance().NoteAuctioneerInReach(botGuidLow);
}

void NoteAuctionZoneArrival(uint32_t botGuidLow, uint32_t nowMs)
{
    OrderStore::Instance().NoteAuctionZoneArrival(botGuidLow, nowMs);
}

void NoteRelistSurplus(uint32_t botGuidLow, uint32_t itemId, uint32_t count, uint32_t unitPrice)
{
    OrderStore::Instance().NoteRelistSurplus(botGuidLow, itemId, count, unitPrice);
}

bool PendingRelist(uint32_t botGuidLow, uint32_t& itemId, uint32_t& count, uint32_t& unitPrice)
{
    return OrderStore::Instance().PendingRelist(botGuidLow, itemId, count, unitPrice);
}

void ClearRelistSurplus(uint32_t botGuidLow)
{
    OrderStore::Instance().ClearRelistSurplus(botGuidLow);
}

void NoteRelistPosted(uint32_t botGuidLow, uint32_t guildId, uint32_t itemId, uint32_t count,
    uint32_t unitPrice, uint32_t bid, uint32_t buyout, uint32_t deposit, uint32_t durationSec)
{
    OrderStore::Instance().NoteRelistPosted(botGuidLow, guildId, itemId, count, unitPrice, bid, buyout,
        deposit, durationSec);
}

std::vector<RelistLedgerEntry> RelistEntries(uint32_t botGuidLow)
{
    return OrderStore::Instance().RelistEntries(botGuidLow);
}

bool NoteRelistClaimed(uint32_t botGuidLow, uint32_t seq, uint32_t mailId, uint32_t gold,
    uint32_t items)
{
    return OrderStore::Instance().NoteRelistClaimed(botGuidLow, seq, mailId, gold, items);
}

bool NoteRelistReceived(uint32_t botGuidLow, uint32_t seq, uint32_t gold, uint32_t items)
{
    return OrderStore::Instance().NoteRelistReceived(botGuidLow, seq, gold, items);
}

bool SaveRelistRow(RelistLedgerEntry const& row)
{
    return OrderStore::Instance().SaveRelist(row);
}

bool DeleteRelistRow(uint32_t botGuidLow, uint32_t seq)
{
    return OrderStore::Instance().DeleteRelist(botGuidLow, seq);
}

std::vector<RelistLedgerEntry> RelistDeposits(uint32_t botGuidLow)
{
    return OrderStore::Instance().RelistDeposits(botGuidLow);
}

void NoteBagsFull(uint32_t botGuidLow)
{
    OrderStore::Instance().NoteBagsFull(botGuidLow);
}

void AddDelivered(uint32_t botGuidLow, uint32_t count)
{
    OrderStore::Instance().AddDelivered(botGuidLow, count);
}

uint32_t Carry(Player* bot, OrderRecord const& order)
{
    if (!order.itemId)
        return 0;
    bool terminal = IsTerminal(order.state);
    bool botOnline = bot && bot->IsInWorld();
    uint32_t liveCarry = botOnline && !terminal ?
        bot->GetItemCount(order.itemId) + MailboxItems(bot, order.itemId) : 0;
    return ListedCarry(terminal, botOnline, liveCarry, order.reportedCarry);
}

std::string FormatOrderLine(uint32_t guildId, OrderRecord const& order, uint32_t have, uint32_t carry)
{
    (void)guildId;
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(order.itemId);
    std::string itemName = proto ? proto->Name1 : "?";
    std::string zone = ZoneName(order.farmZoneId);
    if (order.type == OrderType::Buy && zone == "-")
        zone = "Auction House";
    Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, order.botGuidLow));
    std::string difficulty = "-";
    if (bot && bot->IsInWorld())
        difficulty = ai::PlanOrderDifficulty(bot, order.itemId, order.type, order.amount);
    else if (order.type == OrderType::Farm && order.farmChance > 0)
        difficulty = DifficultyToken(
            EstimateDifficulty(ExpectedKills(order.amount, float(order.farmChance))));
    return "TBM:ORDER|" + BotCommands::ProtocolSafe(BotName(order.botGuidLow)) + "|" +
           TypeToken(order.type) + "|" + std::to_string(order.itemId) + "|" +
           BotCommands::ProtocolSafe(itemName) + "|" + std::to_string(have) + "|" +
           std::to_string(order.amount) + "|" + StateToken(order.state) + "|" +
           ReasonToken(order.reason) + "|" + BotCommands::ProtocolSafe(zone) + "|" + difficulty +
           "|" + HoursToken(order.hours) + "|" + KeepToken(order.keep) + "|" +
           BotCommands::ProtocolSafe(order.parentBotGuidLow ? BotName(order.parentBotGuidLow) : "-") + "|" +
           (order.hasGoldCap ? std::to_string(order.goldCap) : "-") + "|" +
           SupplyStepToken(order.supplyStep) + "|" + NeedItemsToken(order.needItems) + "|" +
           std::to_string(carry) + "|" + BringItemsToken(order.bringItems);
}

void Initialize()
{
    OrderStore::Instance().Initialize();
}

// A finished child deposited into the guild bank: clear the parent's
// empty-bank memory for that item so the parent's next ladder pass retries
// the bank step instead of skipping it for the cooldown. Cheap: one scan
// over finished child rows per Update, no bank reads here.
void WakeParentsOfDeliveredChildren()
{
    for (OrderRecord const& snapshot : OrderStore::Instance().All())
    {
        if (!snapshot.parentBotGuidLow || snapshot.cancelled)
            continue;
        if ((snapshot.state != OrderState::Done && snapshot.state != OrderState::Stocked) ||
            snapshot.handedOver)
            continue;
        std::optional<OrderRecord> parent = Find(snapshot.parentBotGuidLow);
        if (!parent || parent->cancelled || parent->state == OrderState::Waiting)
            continue;
        if (parent->bankEmptyItemId == snapshot.itemId && snapshot.itemId)
            OrderStore::Instance().ClearBankEmpty(parent->botGuidLow);
    }
}

void Update(uint32_t diff)
{
    // Give-up and progress ticks run per order, not per bot tick: orders are
    // few, bots are many. The only place that looks up players and sends
    // live updates; map-thread mutators only mark rows dirty.
    uint32_t now = WorldTimer::getMSTime();
    // Once a minute, not per tick: an entry whose posting never reported back
    // stops being a debt to the guild only once its bot has been online after
    // the auction ended, or the core's 30-day mail lifetime ran out (one log
    // line each).
    uint64_t nowSec = static_cast<uint64_t>(std::time(nullptr));
    if (OrderStore::Instance().RelistExpireDue(nowSec))
        OrderStore::Instance().ExpireStaleRelists(nowSec);
    PlaceSubOrders(now);
    for (OrderRecord const& snapshot : OrderStore::Instance().All())
    {
        uint32_t guidLow = snapshot.botGuidLow;
        ObjectGuid guid(HIGHGUID_PLAYER, guidLow);
        Player* bot = sObjectAccessor.FindPlayer(guid);
        bool online = bot && bot->IsInWorld();

        // Bag and mailbox walks (per item, per reagent) run on the progress
        // cadence, not every world tick: the worked clocks below still tick
        // every pass, and the first scan after a state change runs at once.
        // Finished and parked rows report nothing, so they never scan.
        bool active = !IsFinished(snapshot.state) && !snapshot.cancelled;
        bool stateEdge = snapshot.state == OrderState::Working && snapshot.supplyStep == SupplyStep::Sub;
        bool scanDue = active && (stateEdge ||
            OrderStore::Instance().ShouldScanProgress(guidLow, now));
        // A deliver-first verdict below ends the order on stale zeros when
        // the cadence scan is not due this pass: the worked clocks kept
        // ticking below while bags and mailbox were last read up to one
        // cadence ago, so a sale or mailbox take in between reads as
        // nothing held. Force one fresh scan at the moment the timeout or
        // no-progress clock trips, before Tick decides deliver-first or
        // cant, so partial stacks reach the bank instead of stranding.
        if (active && !scanDue && snapshot.state == OrderState::Working &&
            (IsOrderTimeout(snapshot.workedTotalSec, snapshot.hours) ||
                IsOrderNoProgress(snapshot.workedNoGainSec)))
        {
            OrderStore::Instance().MarkProgressScanned(guidLow, now);
            scanDue = true;
        }
        if (scanDue)
            OrderStore::Instance().MarkProgressScanned(guidLow, now);
        OrderStore::TickIn in;
        in.botOnline = online;
        in.botAlive = online && bot->IsAlive();
        in.scanned = scanDue;
        in.bagCount = scanDue && online && snapshot.itemId ? bot->GetItemCount(snapshot.itemId) : 0;
        in.mailboxCount = scanDue && online && snapshot.itemId ? MailboxItems(bot, snapshot.itemId) : 0;
        // The no-progress clock watches what the bot actually farms: the
        // reagent of the current craft leg, else the order item itself.
        // Delivery, reporting and mailbox progress stay on the order item.
        in.farmItemId = (snapshot.type == OrderType::Craft && snapshot.farmItemId) ? snapshot.farmItemId : 0;
        in.farmBagCount = scanDue && online && in.farmItemId ? bot->GetItemCount(in.farmItemId) :
            (in.farmItemId ? 0 : in.bagCount);
        in.workedMs = diff;
        in.nowMs = now;
        // Direct children still in progress (a running row, or a queued
        // request inside its staleness window): only a row with no leg of its
        // own pauses its clocks for them.
        in.unfinishedSubOrders = active ? OrderStore::Instance().UnfinishedSubOrderCount(guidLow) : 0;
        if (scanDue && NeedLadderLaneEligible(snapshot.state, snapshot.reason, !snapshot.needItems.empty()) &&
            !snapshot.cancelled)
        {
            // Bags and mailbox are the player's delivery lane, so only a
            // parked row reads them; the bank, vendor and AH lanes below
            // serve a working row's need list the same way.
            if (snapshot.state == OrderState::Waiting)
            {
                for (NeedItem const& need : snapshot.needItems)
                {
                    if (online && bot->GetItemCount(need.itemId) + MailboxItems(bot, need.itemId) >= need.count)
                    {
                        in.needItemAvailable = true;
                        break;
                    }
                }
            }
            if (OrderStore::Instance().ShouldCheckNeedBank(guidLow, now))
            {
                OrderStore::Instance().MarkNeedBankChecked(guidLow, now);
                // Withdrawing disabled, or a bank that already refused this
                // order: a stock the bot cannot take must not look like a
                // lane hit, or the row clears and replans every ten seconds.
                if (sPlayerbotAIConfig.guildOrderBankWithdraw)
                    for (NeedItem const& need : snapshot.needItems)
                        if (!snapshot.needBankDenied &&
                            BankItemCount(snapshot.guildId, need.itemId) >= need.count)
                        {
                            in.needItemAvailable = true;
                            in.needItemInBank = true;
                            break;
                        }
            }
            // A parked need row re-runs the ladder when its facts changed: a
            // vendor-unlimited item the new rule buys, or a fresh AH listing.
            // Same ten-minute cadence as the bank lane above (plus the first
            // scan after the row parked), so the player hears no new whisper:
            // Tick only clears the already-sent list on a resume.
            if (!in.needItemAvailable && OrderStore::Instance().ShouldCheckNeedLadder(guidLow, now))
            {
                OrderStore::Instance().MarkNeedLadderChecked(guidLow, now);
                for (NeedItem const& need : snapshot.needItems)
                {
                    if (!need.itemId || !need.count)
                        continue;
                    // The giver brings a ticked material: no shop or auction resume for it.
                    if (BringTickedBlocksLeg(snapshot.bringItems, need.itemId))
                        continue;
                    if (NeedVendorAffordable(bot, online, need))
                    {
                        in.needVendorReady = true;
                        break;
                    }
                    if (!in.needVendorReady && NeedAhListed(need.itemId))
                    {
                        in.needAhReady = true;
                        break;
                    }
                }
                if (!in.needVendorReady && !in.needAhReady &&
                    NeedFullRetryDue(OrderStore::Instance().NeedLadderChecks(guidLow)))
                    in.needRetryLadder = true;
            }
        }
        else if (snapshot.state == OrderState::Working && snapshot.supplyStep == SupplyStep::Sub &&
            sPlayerbotAIConfig.guildOrderBankWithdraw &&
            OrderStore::Instance().ShouldCheckNeedBank(guidLow, now))
        {
            OrderStore::Instance().MarkNeedBankChecked(guidLow, now);
            if (snapshot.supplyItemId && BankItemCount(snapshot.guildId, snapshot.supplyItemId) > 0)
                SetSupply(guidLow, SupplyStep::Bank, snapshot.supplyItemId, snapshot.supplyCount,
                    SupplyStep::AuctionHouse);
        }
        // Keep watch, due at most every 10 min per stocked order: one summed
        // bank read on the world thread, never per bot tick, bot online or not.
        if (snapshot.state == OrderState::Stocked && snapshot.keep &&
            KeepCheckDue(now, snapshot.lastBankCheckMs))
        {
            in.bankCount = BankItemCount(snapshot.guildId, snapshot.itemId);
            in.bankChecked = true;
        }

        OrderStore::TickOut out = OrderStore::Instance().Tick(guidLow, in);
        if (!out.haveRow)
            continue;
        // The row write runs without the store mutex (Tick only snapshotted
        // it); message sends never held it either, so the order here needs
        // no lock discipline beyond finishing the write first.
        if (out.persist)
            OrderStore::Instance().WriteAndRescue(out.persistRecord);
        if (out.sendLine)
            SendOrderLine(out.record, out.reportedHave, out.reportedCarry);
        if (out.needWhisper)
            SendNeedWhisper(out.record);
        if (out.legChanged)
            ExpireGuildOrderTarget(guidLow);
    }
    for (OrderRecord const& snapshot : OrderStore::Instance().All())
    {
        // A finished sub-order that did not deliver fails the parent: the
        // full cancel path reaps its branch (queues, travel target) and
        // puts the item on the parent's need list. A child holding its
        // quota detaches into a final top-level delivery instead, through
        // the same cancel path.
        if (!snapshot.parentBotGuidLow || snapshot.state == OrderState::Done)
            continue;
        if (!IsFinished(snapshot.state))
        {
            // A running sub-order whose parent is gone or finished has no
            // one to supply: it stops instead of farming for nobody.
            std::optional<OrderRecord> parent = Find(snapshot.parentBotGuidLow);
            if (!snapshot.cancelled && (!parent || IsFinished(parent->state)))
                Cancel(snapshot.botGuidLow);
            continue;
        }
        Cancel(snapshot.botGuidLow);
    }
    WakeParentsOfDeliveredChildren();
    UpdateDeliveries();
    UpdateSupplyWithdrawals();
    UpdateGoldRefunds();
    UpdateRelistDeposits();
    OrderStore::Instance().FinishRepaid();
}

void OnGuildDelMember(uint32_t botGuidLow)
{
    Cancel(botGuidLow);
}

void OnGuildDisband(uint32_t guildId)
{
    for (uint32_t guidLow : OrderStore::Instance().BotsForGuild(guildId))
        Cancel(guidLow);
}

} // namespace GuildOrders
} // namespace TortoiseBots
