#include "GuildOrderStore.h"

#include "GuildOrderDelivery.h"
#include "GuildOrderGiveUpPolicy.h"
#include "GuildOrderKeepPolicy.h"
#include "GuildOrderSourcesPolicy.h"
#include "GuildOrderSupplyPolicy.h"
#include "GuildOrderSubOrderPolicy.h"
#include "../commands/CommandShared.h"
#include "../host/ModuleLog.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/GuildOrderSources.h"
#include "../ai/playerbot/GuildOrderCraft.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../runtime/BotManager.h"
#include "../runtime/AltBotService.h"
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

#include <cstdint>
#include <ctime>
#include <mutex>
#include <sstream>
#include <unordered_map>

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
// Worked seconds live in memory; the database sees them at most this often.
inline constexpr uint32_t kWorkedFlushMs = 60000;
inline constexpr uint32_t kNeedBankCheckMs = 10000;

std::vector<NeedItem> ParseNeedItems(std::string const& token)
{
    std::vector<NeedItem> out;
    if (token.empty() || token == "-")
        return out;
    std::istringstream entries(token);
    std::string entry;
    while (std::getline(entries, entry, ';'))
    {
        size_t colon = entry.find(':');
        if (colon == std::string::npos)
            continue;
        try
        {
            unsigned long itemId = std::stoul(entry.substr(0, colon));
            unsigned long count = std::stoul(entry.substr(colon + 1));
            if (itemId && count && itemId <= UINT32_MAX && count <= UINT32_MAX)
                out.push_back(NeedItem{ static_cast<uint32_t>(itemId), static_cast<uint32_t>(count) });
        }
        catch (...)
        {
        }
    }
    return out;
}

class OrderStore
{
public:
    static OrderStore& Instance()
    {
        static OrderStore instance;
        return instance;
    }

    void Initialize()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_initialized)
            return;

        m_orders.clear();

        std::unique_ptr<QueryResult> result(CharacterDatabase.Query(
            "SELECT `bot_guid`, `guild_id`, `giver_guid`, `parent_bot`, `root_giver`, `type`, `item_id`, `amount`, `hours`, `delivered`, "
            "`state`, `reason`, `farm_zone`, `farm_chance`, `farm_item`, `zone_index`, `deaths_in_zone`, "
            "`worked_sec`, `worked_total`, `worked_nogain`, `reported`, `farm_rest`, `keep`, `gold_cap`, `gold_cap_set`, "
            "`guild_gold_withdrawn`, `guild_gold_held`, `cancelled`, `supply_step`, `supply_return_step`, `supply_item`, `supply_count`, `supply_gold_count`, "
            "`need_items`, `need_whispered`, `last_need_whisper`, `need_bank_denied`, `stock_target`, `last_bank_check`, "
            "`created_at`, `updated_at` "
            "FROM `tortoise_bots_guild_order`"));
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                OrderRecord record;
                record.botGuidLow = fields[0].GetUInt32();
                record.guildId = fields[1].GetUInt32();
                record.giverGuidLow = fields[2].GetUInt32();
                record.parentBotGuidLow = fields[3].GetUInt32();
                record.rootGiverGuidLow = fields[4].GetUInt32();
                ParseType(fields[5].GetCppString(), record.type);
                record.itemId = fields[6].GetUInt32();
                record.amount = fields[7].GetUInt32();
                record.hours = fields[8].GetUInt32();
                record.delivered = fields[9].GetUInt32();
                ParseState(fields[10].GetCppString(), record.state);
                ParseReason(fields[11].GetCppString(), record.reason);
                record.farmZoneId = fields[12].GetUInt32();
                record.farmChance = static_cast<uint8_t>(fields[13].GetUInt32());
                record.farmItemId = fields[14].GetUInt32();
                record.zoneIndex = fields[15].GetUInt32();
                record.deathsInZone = fields[16].GetUInt32();
                record.workedSinceNewItemSec = fields[17].GetUInt32();
                record.workedTotalSec = fields[18].GetUInt32();
                record.workedNoGainSec = fields[19].GetUInt32();
                record.reportedHave = fields[20].GetUInt32();
                record.farmRest = fields[21].GetUInt32() != 0;
                record.keep = fields[22].GetUInt32() != 0;
                record.goldCap = fields[23].GetUInt32();
                record.hasGoldCap = fields[24].GetUInt32() != 0;
                record.guildGoldWithdrawn = fields[25].GetUInt32();
                record.guildGoldHeld = fields[26].GetUInt32();
                record.cancelled = fields[27].GetUInt32() != 0;
                ParseSupplyStep(fields[28].GetCppString(), record.supplyStep);
                ParseSupplyStep(fields[29].GetCppString(), record.supplyReturnStep);
                record.supplyItemId = fields[30].GetUInt32();
                record.supplyCount = fields[31].GetUInt32();
                record.supplyGoldCount = fields[32].GetUInt32();
                record.needItems = ParseNeedItems(fields[33].GetCppString());
                record.needWhispered = fields[34].GetUInt32() != 0;
                record.lastNeedWhisper = fields[35].GetCppString();
                record.needBankDenied = fields[36].GetUInt32() != 0;
                record.stockTarget = fields[37].GetUInt32();
                record.lastBankCheckMs = fields[38].GetUInt32();
                record.createdAt = fields[39].GetUInt64();
                record.updatedAt = fields[40].GetUInt64();
                if (record.hours != kUnlimitedOrderHours &&
                    (record.hours < kMinOrderHours || record.hours > kMaxOrderHours))
                    record.hours = kDefaultOrderHours;
                if (!record.rootGiverGuidLow && !record.parentBotGuidLow)
                    record.rootGiverGuidLow = record.giverGuidLow;
                if (record.botGuidLow && record.itemId && record.amount)
                    m_orders[record.botGuidLow] = Stored{record};
            } while (result->NextRow());
        }

        m_initialized = true;
        TB_LOG_BASIC("TortoiseBots: loaded %zu guild order(s) from database.", m_orders.size());
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
                "`created_at`, `updated_at`) "
                "VALUES ('%u', '%u', '%u', '%u', '%u', '%s', '%u', '%u', '%u', '%u', '%s', '%s', '%u', '%u', '%u', '%u', '%u', "
                "'%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%s', '%s', '%u', '%u', '%u', '%s', '%u', '%s', '%u', '%u', '%u', "
                "FROM_UNIXTIME('%u'), FROM_UNIXTIME('%u'))",
                record.botGuidLow, record.guildId, record.giverGuidLow, record.parentBotGuidLow,
                record.rootGiverGuidLow, TypeToken(record.type),
                record.itemId, record.amount, record.hours, record.delivered, StateToken(record.state),
                ReasonToken(record.reason), record.farmZoneId, record.farmChance, record.farmItemId,
                record.zoneIndex, record.deathsInZone, record.workedSinceNewItemSec,
                record.workedTotalSec, record.workedNoGainSec, record.reportedHave, record.farmRest ? 1u : 0u,
                record.keep ? 1u : 0u, record.goldCap, record.hasGoldCap ? 1u : 0u,
                record.guildGoldWithdrawn, record.guildGoldHeld, record.cancelled ? 1u : 0u,
                SupplyStepToken(record.supplyStep),
                SupplyStepToken(record.supplyReturnStep), record.supplyItemId, record.supplyCount, record.supplyGoldCount,
                NeedItemsToken(record.needItems).c_str(), record.needWhispered ? 1u : 0u,
                record.lastNeedWhisper.c_str(), record.needBankDenied ? 1u : 0u,
                record.stockTarget, record.lastBankCheckMs,
                static_cast<uint32_t>(record.createdAt), static_cast<uint32_t>(record.updatedAt)))
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
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_orders.begin(); it != m_orders.end();)
        {
            // The refund drain paid the last copper: the parked row goes, in
            // memory and in the table. A still-owed row survives restarts.
            if (it->second.record.cancelled && it->second.record.guildGoldHeld == 0)
            {
                uint32_t guidLow = it->first;
                it = m_orders.erase(it);
                CharacterDatabase.DirectPExecute(
                    "DELETE FROM `tortoise_bots_guild_order` WHERE `bot_guid` = '%u'", guidLow);
            }
            else
                ++it;
        }
    }
    bool Cancel(uint32_t botGuidLow)
    {
        if (!botGuidLow)
            return false;
        bool park = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_orders.find(botGuidLow);
            if (it == m_orders.end())
                return false;
            // Unspent guild gold must reach the bank first: the row parks as
            // a stub so the refund drain can still find the debt after a
            // restart. Nothing else runs on it: travel, give-up and progress
            // all check the flag first.
            if (it->second.record.guildGoldHeld > 0)
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
                it->second.record.reportedHave = 0;
                MarkDirtyLocked(it->second);
                park = true;
            }
            else
                m_orders.erase(it);
            ClearQueuedSubOrdersLocked(botGuidLow);
        }
        if (park)
            return true;
        CharacterDatabase.DirectPExecute(
            "DELETE FROM `tortoise_bots_guild_order` WHERE `bot_guid` = '%u'", botGuidLow);
        return true;
    }

    // Map-thread requests keyed by parent and item: one parent can hold
    // several sub-order requests in parallel (different items, on different
    // bots). RequestSupply only records them under the lock; Update drains
    // and places them with live players and the planner.
    struct QueuedSubOrder
    {
        uint32_t parentBotGuidLow = 0;
        uint32_t itemId = 0;
        uint32_t count = 0;
        uint32_t nextTryMs = 0; // world uptime ms before this request retries
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
        m_queued.emplace(key, QueuedSubOrder{ parentBotGuidLow, itemId, count });
        return true;
    }

    std::vector<QueuedSubOrder> TakeDueQueuedSubOrders(uint32_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<QueuedSubOrder> out;
        for (auto const& [key, queued] : m_queued)
        {
            int32_t until = int32_t(queued.nextTryMs - nowMs);
            if (until <= 0)
                out.push_back(queued);
        }
        for (QueuedSubOrder const& due : out)
            m_queued.erase(QueuedSubOrderKey{ due.parentBotGuidLow, due.itemId });
        return out;
    }

    void RetryQueuedSubOrder(uint32_t parentBotGuidLow, uint32_t itemId, uint32_t count, uint32_t nextTryMs)
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
        // denial while the bot farms or buys.
        if (reason == OrderReason::None && step == record.supplyStep && record.supplyItemId == itemId &&
            (record.reason == OrderReason::BankDenied || record.reason == OrderReason::NoSource))
            reason = record.reason;
        if (record.state == OrderState::Waiting && record.reason == OrderReason::NeedItems && step != SupplyStep::Need)
        {
            record.state = OrderState::Working;
            record.reason = OrderReason::None;
            record.needItems.clear();
            record.needWhispered = false;
        }
        if (record.supplyStep == step && record.supplyItemId == itemId &&
            record.supplyCount == count && record.supplyGoldCount == goldCount &&
            record.supplyReturnStep == returnStep && record.reason == reason)
            return;
        record.supplyStep = step;
        record.supplyItemId = itemId;
        record.supplyCount = count;
        record.supplyReturnStep = returnStep;
        record.supplyGoldCount = goldCount;
        record.reason = reason;
        if (step != SupplyStep::Need)
        {
            record.needItems.clear();
            record.needWhispered = false;
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
        bool same = record.needItems.size() == items.size();
        for (size_t i = 0; same && i < items.size(); ++i)
            same = record.needItems[i].itemId == items[i].itemId && record.needItems[i].count == items[i].count;
        if (same && record.state == OrderState::Waiting && record.reason == OrderReason::NeedItems)
            return;
        record.needItems = items;
        record.state = OrderState::Waiting;
        record.reason = OrderReason::NeedItems;
        record.supplyStep = SupplyStep::Need;
        record.supplyReturnStep = SupplyStep::None;
        record.supplyGoldCount = 0;
        record.needWhispered = record.lastNeedWhisper == NeedItemsToken(items);
        record.needBankDenied = bankDenied;
        MarkDirtyLocked(it->second);
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
            !((it->second.record.state == OrderState::Waiting && it->second.record.reason == OrderReason::NeedItems) ||
              (it->second.record.state == OrderState::Working && it->second.record.supplyStep == SupplyStep::Sub)))
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
        if (it->second.record.searchFailsInZone == 0 && it->second.supplySearch.fails == 0)
            return;
        it->second.record.searchFailsInZone = 0;
        it->second.supplySearch = SupplySearchStreak{};
        MarkDirtyLocked(it->second);
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

    void NoteSubOrderFailed(uint32_t parentBotGuidLow, uint32_t itemId, uint32_t count)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto parent = m_orders.find(parentBotGuidLow);
        if (parent == m_orders.end() || IsFinished(parent->second.record.state) || !itemId || !count)
            return;
        // The failed sub-order leaves its item on the parent's need list:
        // merge with the items already waiting so parallel sub-orders that
        // fail together each keep their row. A return through the need state
        // replaces whatever wait the parent entered first, so every dead
        // end funnels through this one entry.
        OrderRecord& record = parent->second.record;
        bool merged = false;
        for (NeedItem& need : record.needItems)
        {
            if (need.itemId != itemId)
                continue;
            if (count > need.count)
                need.count = count;
            merged = true;
        }
        if (!merged)
            record.needItems.push_back(NeedItem{ itemId, count });
        record.state = OrderState::Waiting;
        record.reason = OrderReason::NeedItems;
        record.supplyStep = SupplyStep::Need;
        record.supplyReturnStep = SupplyStep::None;
        record.supplyGoldCount = 0;
        record.needWhispered = record.lastNeedWhisper == NeedItemsToken(record.needItems);
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
        uint32_t bagCount = 0;       // order item in bags (delivery gate, reporting)
        uint32_t mailboxCount = 0;   // order item in the mailbox
        uint32_t farmItemId = 0;     // item actually farmed (0 = the order item)
        uint32_t farmBagCount = 0;   // farm-leg item in bags (no-progress clock)
        uint32_t bankCount = 0;      // guild bank stock, valid when bankChecked
        bool bankChecked = false;    // a fresh bank look came with this tick
        bool needItemAvailable = false;
        bool needItemInBank = false;
        uint32_t workedMs = 0;
        uint32_t nowMs = 0;
    };
    struct TickOut
    {
        bool haveRow = false;
        OrderRecord record;
        uint32_t reportedHave = 0;
        bool sendLine = false;
        bool finishedEdge = false;
        bool legChanged = false;
        bool needWhisper = false;
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
            return out;

        bool wasFinished = IsFinished(record.state);
        bool wasStocked = record.state == OrderState::Stocked;

        if (record.state == OrderState::Waiting && record.reason == OrderReason::NeedItems &&
            in.needItemAvailable)
        {
            record.state = OrderState::Working;
            record.reason = OrderReason::None;
            record.supplyStep = in.needItemInBank ? SupplyStep::Bank : SupplyStep::Bags;
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
                record.amount = missing;
                record.delivered = 0;
                record.farmZoneId = 0;
                record.farmChance = 0;
                record.zoneIndex = 0;
                record.farmItemId = 0;
                record.farmRest = false;
                record.deathsInZone = 0;
                record.searchFailsInZone = 0;
                record.workedSinceNewItemSec = 0;
                record.workedTotalSec = 0;
                record.workedNoGainSec = 0;
                record.reportedHave = 0;
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

        // A parent parked on a sub-order is waiting, even while its row stays working.
        if (!wasFinished && in.botOnline && in.botAlive && !in.grouped &&
            record.state == OrderState::Working && record.supplyStep != SupplyStep::Sub)
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
        if (!wasFinished && in.botOnline)
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
        // Amount reached in bags: the bot heads for the guild bank (the
        // delivery action walks it there once package 4 lands; until then
        // the state alone marks it delivering). Mailbox items count for
        // progress and the give-up clock above, but delivery needs the
        // items in the bags - the bank deposit cannot take them from mail.
        if (!wasFinished && record.state == OrderState::Working && record.amount > 0 &&
            record.delivered + in.bagCount >= record.amount)
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

        out.record = record;
        out.finishedEdge = !wasFinished && IsFinished(record.state);
        // The whisper goes out only when the giver is online to hear it: the
        // flag sets only on a sent line, so an offline giver is retried on a
        // later tick. The giver lookup rides the live-update pass that
        // already carries both players.
        if (record.state == OrderState::Waiting && record.reason == OrderReason::NeedItems &&
            !record.needWhispered)
        {
            ObjectGuid giverGuid(HIGHGUID_PLAYER, record.giverGuidLow);
            Player* giver = sObjectAccessor.FindPlayer(giverGuid);
            if (giver && giver->IsInWorld())
            {
                out.needWhisper = true;
                record.needWhispered = true;
                record.lastNeedWhisper = NeedItemsToken(record.needItems);
                MarkDirtyLocked(stored);
            }
        }

        // Reported progress never drops: offline (or not yet re-seen) bots
        // keep max(delivered, last reported). A stocked order reports the
        // bank stock the last look saw: owned counting means nothing there.
        out.reportedHave = ReportedHave(record.delivered, ownedNow, in.botOnline, record.reportedHave);
        if (record.state == OrderState::Stocked && in.bankChecked)
            out.reportedHave = in.bankCount > record.stockTarget ? record.stockTarget : in.bankCount;

        std::string needToken = NeedItemsToken(record.needItems);
        bool stateChanged = !stored.reportSeen || stored.reportState != record.state ||
                            stored.reportReason != record.reason || stored.reportStep != record.supplyStep ||
                            stored.reportNeed != needToken;
        if (!stored.reportSeen || stored.reportStep != record.supplyStep)
            out.legChanged = true;
        bool progressDue = out.reportedHave != record.reportedHave &&
                           (!stored.reportSeen || in.nowMs - stored.reportMs >= kProgressReportMs);
        out.sendLine = stateChanged || progressDue || out.finishedEdge;
        if (out.sendLine)
        {
            stored.reportSeen = true;
            stored.reportState = record.state;
            stored.reportReason = record.reason;
            stored.reportStep = record.supplyStep;
            stored.reportNeed = needToken;
            if (out.reportedHave != record.reportedHave)
            {
                record.reportedHave = out.reportedHave;
                stored.reportMs = in.nowMs;
                MarkDirtyLocked(stored);
            }
        }

        FlushLocked(stored);
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
        SupplySearchStreak supplySearch; // empty service-leg searches for the current leg
        bool sawOnline = false;
        bool reportSeen = false;
        OrderState reportState = OrderState::Working;
        OrderReason reportReason = OrderReason::None;
        uint32_t reportMs = 0;
        uint32_t lastNeedBankCheckMs = 0;
        SupplyStep reportStep = SupplyStep::None;
        std::string reportNeed;
    };

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

    static void FlushLocked(Stored& stored)
    {
        if (!stored.dirty)
            return;
        stored.dirty = false;
        OrderRecord& record = stored.record;
        record.updatedAt = static_cast<uint64_t>(std::time(nullptr));
        CharacterDatabase.PExecute(
            "UPDATE `tortoise_bots_guild_order` SET `giver_guid` = '%u', `parent_bot` = '%u', `root_giver` = '%u', `amount` = '%u', `hours` = '%u', `delivered` = '%u', "
            "`state` = '%s', `reason` = '%s', "
            "`farm_zone` = '%u', `farm_chance` = '%u', `farm_item` = '%u', `zone_index` = '%u', "
            "`deaths_in_zone` = '%u', `worked_sec` = '%u', `worked_total` = '%u', `worked_nogain` = '%u', `reported` = '%u', "
            "`farm_rest` = '%u', `keep` = '%u', `gold_cap` = '%u', `gold_cap_set` = '%u', "
            "`guild_gold_withdrawn` = '%u', `guild_gold_held` = '%u', `cancelled` = '%u', `supply_step` = '%s', "
            "`supply_return_step` = '%s', `supply_item` = '%u', `supply_count` = '%u', `supply_gold_count` = '%u', "
            "`need_items` = '%s', `need_whispered` = '%u', `last_need_whisper` = '%s', `need_bank_denied` = '%u', `stock_target` = '%u', `last_bank_check` = '%u', `updated_at` = FROM_UNIXTIME('%u') "
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
            static_cast<uint32_t>(record.updatedAt), record.botGuidLow);
    }

    std::mutex m_mutex;
    std::unordered_map<uint32_t, Stored> m_orders;
    std::unordered_map<QueuedSubOrderKey, QueuedSubOrder, QueuedSubOrderKeyHash> m_queued;
    bool m_initialized = false;
};

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
void SendOrderLine(OrderRecord const& order, uint32_t have);
// One drained queue entry: pick a durable guild bot that can make the item,
// or farm it when no maker is free. A member-list query runs once per due
// request per retry interval, never every tick: busy-only candidates
// re-queue with a 45 s timer, and depth/loop/no-candidate failures report
// through the single failure entry instead of retrying.
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
    bool placedAny = false;
    for (OrderStore::QueuedSubOrder const& request : queued)
    {
        if (!request.itemId || !request.count)
            continue;
        std::optional<OrderRecord> parent = Find(request.parentBotGuidLow);
        if (!parent || IsFinished(parent->state))
            continue;
        // Every terminal path below reports through the failure entry: the
        // parent never waits on a request that cannot become a running row.
        if (!SubOrderDepthAllows(BranchDepth(parent->botGuidLow), maxDepth))
        {
            NoteSubOrderFailed(parent->botGuidLow, request.itemId, request.count);
            continue;
        }
        std::vector<uint32_t> branch = BranchItems(parent->botGuidLow);
        if (!SubOrderLoopFree(request.itemId, branch.data(), branch.size()))
        {
            NoteSubOrderFailed(parent->botGuidLow, request.itemId, request.count);
            continue;
        }
        std::vector<SubOrderCandidate> candidates;
        std::unique_ptr<QueryResult> members(CharacterDatabase.PQuery(
            "SELECT `guid` FROM `guild_member` WHERE `guildid` = '%u'", parent->guildId));
        if (members)
        {
            do
            {
                Field* fields = members->Fetch();
                uint32_t guidLow = fields[0].GetUInt32();
                if (!guidLow || guidLow == parent->botGuidLow)
                    continue;
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
                candidate.hasOrder = existing.has_value() && !existing->cancelled &&
                    (!IsFinished(existing->state) || existing->state == OrderState::Stocked);
                if (candidate.hasOrder)
                {
                    bool make = false, farm = false;
                    if (SubOrderCandidateUsable(bot, request.itemId, make, farm))
                        candidate.queuedBusy = true;
                    else
                        continue;
                    candidates.push_back(candidate);
                }
                else if (!SubOrderCandidateUsable(bot, request.itemId, candidate.canMake, candidate.canFarm))
                    continue;
                else
                    candidates.push_back(candidate);
            } while (members->NextRow());
        }
        bool queuedOut = false;
        size_t picked = PickSubOrderCandidate(candidates.data(), candidates.size(), queuedOut);
        SubOrderPick pick = ClassifySubOrderPick(picked, candidates.size(), queuedOut);
        if (pick == SubOrderPick::Take)
        {
            OrderRecord record;
            record.botGuidLow = candidates[picked].botGuidLow;
            record.guildId = parent->guildId;
            record.giverGuidLow = parent->botGuidLow;
            record.parentBotGuidLow = parent->botGuidLow;
            record.rootGiverGuidLow = parent->rootGiverGuidLow ? parent->rootGiverGuidLow : parent->giverGuidLow;
            record.type = candidates[picked].canMake ? OrderType::Craft : OrderType::Farm;
            record.itemId = request.itemId;
            record.amount = request.count;
            record.goldCap = parent->goldCap;
            record.hasGoldCap = parent->hasGoldCap;
            if (!Give(record))
            {
                OrderStore::Instance().RetryQueuedSubOrder(parent->botGuidLow, request.itemId,
                    request.count, nowMs + kSubOrderRetryMs);
                continue;
            }
            placedAny = true;
            if (std::optional<OrderRecord> placed = Find(record.botGuidLow))
                SendOrderLine(*placed, 0);
        }
        else if (pick == SubOrderPick::Queued)
            OrderStore::Instance().RetryQueuedSubOrder(parent->botGuidLow, request.itemId,
                request.count, nowMs + kSubOrderRetryMs);
        else
            NoteSubOrderFailed(parent->botGuidLow, request.itemId, request.count);
    }
    return placedAny;
}

void SendOrderLine(OrderRecord const& order, uint32_t have)
{
    uint32_t giverLow = order.rootGiverGuidLow ? order.rootGiverGuidLow : order.giverGuidLow;
    Player* giver = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, giverLow));
    if (!giver || !giver->IsInWorld())
        return;
    giver->SendAddonMessage("TBM", FormatOrderLine(order.guildId, order, have));
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

bool SubOrderCandidateUsable(Player* bot, uint32_t itemId, bool& canMake, bool& canFarm)
{
    canMake = false;
    canFarm = false;
    if (!bot || !bot->IsInWorld() || !itemId)
        return false;
    if (FindCraftSpellFor(bot, itemId))
    {
        canMake = true;
        return true;
    }
    SourcePlan plan = ai::PlanGuildOrderSources(bot, itemId, OrderType::Farm);
    canFarm = plan.verdict == OrderReason::None && !plan.zones.empty();
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
        OnDeliveryOrderGone(botGuidLow);
        OnSupplyOrderGone(botGuidLow);
        ExpireGuildOrderTarget(botGuidLow);
        for (uint32_t child : branch)
        {
            if (child == botGuidLow)
                continue;
            OrderStore::Instance().Cancel(child);
            OnDeliveryOrderGone(child);
            OnSupplyOrderGone(child);
            ExpireGuildOrderTarget(child);
        }
        if (parent)
            NoteSubOrderFailed(parent, itemId, amount);
    }
    return gone;
}

bool RequestSupply(uint32_t parentBotGuidLow, uint32_t itemId, uint32_t count)
{
    if (!sPlayerbotAIConfig.guildOrderSubOrders || !itemId || !count)
        return false;
    std::optional<OrderRecord> parent = Find(parentBotGuidLow);
    if (!parent)
        return false;
    std::optional<OrderRecord> existing;
    // Parked refund stubs are invisible here: BranchBots skips them, and a
    // stub never counts as a running row for the busy check either.
    for (uint32_t child : OrderStore::Instance().BranchBots(parentBotGuidLow))
    {
        if (child == parentBotGuidLow)
            continue;
        std::optional<OrderRecord> row = Find(child);
        if (row && row->itemId == itemId && !IsFinished(row->state))
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
    SendOrderLine(record, row->reportedHave);
    return true;
}

std::vector<uint32_t> BranchBots(uint32_t botGuidLow)
{
    return OrderStore::Instance().BranchBots(botGuidLow);
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

void NoteSearchFailed(uint32_t botGuidLow, uint32_t zoneId)
{
    OrderStore::Instance().NoteSearchFailed(botGuidLow, zoneId);
}

void NoteSearchLanded(uint32_t botGuidLow)
{
    OrderStore::Instance().NoteSearchLanded(botGuidLow);
}

void NoteBagsFull(uint32_t botGuidLow)
{
    OrderStore::Instance().NoteBagsFull(botGuidLow);
}

void AddDelivered(uint32_t botGuidLow, uint32_t count)
{
    OrderStore::Instance().AddDelivered(botGuidLow, count);
}

uint32_t Progress(Player* bot, OrderRecord const& order)
{
    uint32_t have = order.delivered;
    if (bot && bot->IsInWorld() && order.itemId)
        have += bot->GetItemCount(order.itemId) + MailboxItems(bot, order.itemId);
    return have;
}

std::string FormatOrderLine(uint32_t guildId, OrderRecord const& order, uint32_t have)
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
           SupplyStepToken(order.supplyStep) + "|" + NeedItemsToken(order.needItems);
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
        if (snapshot.state != OrderState::Done && snapshot.state != OrderState::Stocked)
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
    PlaceSubOrders(now);
    for (OrderRecord const& snapshot : OrderStore::Instance().All())
    {
        uint32_t guidLow = snapshot.botGuidLow;
        ObjectGuid guid(HIGHGUID_PLAYER, guidLow);
        Player* bot = sObjectAccessor.FindPlayer(guid);
        bool online = bot && bot->IsInWorld();

        OrderStore::TickIn in;
        in.botOnline = online;
        in.botAlive = online && bot->IsAlive();
        in.bagCount = online && snapshot.itemId ? bot->GetItemCount(snapshot.itemId) : 0;
        in.mailboxCount = online && snapshot.itemId ? MailboxItems(bot, snapshot.itemId) : 0;
        // The no-progress clock watches what the bot actually farms: the
        // reagent of the current craft leg, else the order item itself.
        // Delivery, reporting and mailbox progress stay on the order item.
        in.farmItemId = (snapshot.type == OrderType::Craft && snapshot.farmItemId) ? snapshot.farmItemId : 0;
        in.farmBagCount = online && in.farmItemId ? bot->GetItemCount(in.farmItemId) : in.bagCount;
        in.workedMs = diff;
        in.nowMs = now;
        if (snapshot.state == OrderState::Waiting && snapshot.reason == OrderReason::NeedItems)
        {
            for (NeedItem const& need : snapshot.needItems)
            {
                if (online && bot->GetItemCount(need.itemId) + MailboxItems(bot, need.itemId) >= need.count)
                {
                    in.needItemAvailable = true;
                    break;
                }
            }
            if (OrderStore::Instance().ShouldCheckNeedBank(guidLow, now))
            {
                OrderStore::Instance().MarkNeedBankChecked(guidLow, now);
                for (NeedItem const& need : snapshot.needItems)
                    if (!snapshot.needBankDenied && BankItemCount(snapshot.guildId, need.itemId) >= need.count)
                    {
                        in.needItemAvailable = true;
                        in.needItemInBank = true;
                        break;
                    }
            }
        }
        else if (snapshot.state == OrderState::Working && snapshot.supplyStep == SupplyStep::Sub &&
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
        if (out.sendLine)
            SendOrderLine(out.record, out.reportedHave);
        if (out.needWhisper)
            SendNeedWhisper(out.record);
        if (out.legChanged)
            ExpireGuildOrderTarget(guidLow);
    }
    for (OrderRecord const& snapshot : OrderStore::Instance().All())
    {
        if (!snapshot.parentBotGuidLow || snapshot.state != OrderState::Cant)
            continue;
        std::optional<OrderRecord> parent = Find(snapshot.parentBotGuidLow);
        if (!parent || IsFinished(parent->state))
        {
            OrderStore::Instance().Cancel(snapshot.botGuidLow);
            continue;
        }
        NoteSubOrderFailed(snapshot.parentBotGuidLow, snapshot.itemId, snapshot.amount);
        OrderStore::Instance().Cancel(snapshot.botGuidLow);
    }
    WakeParentsOfDeliveredChildren();
    UpdateDeliveries();
    UpdateSupplyWithdrawals();
    UpdateGoldRefunds();
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
