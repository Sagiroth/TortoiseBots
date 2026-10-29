#include "GuildOrderStore.h"

#include "GuildOrderDelivery.h"
#include "GuildOrderGiveUpPolicy.h"
#include "GuildOrderSourcesPolicy.h"
#include "../commands/CommandShared.h"
#include "../host/ModuleLog.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/GuildOrderSources.h"
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

#include <ctime>
#include <mutex>
#include <unordered_map>

namespace TortoiseBots
{
namespace GuildOrders
{
namespace
{

// Progress lines at most every 5 s per order; state changes always go out.
inline constexpr uint32_t kProgressReportMs = 5000;
// Worked seconds live in memory; the database sees them at most this often.
inline constexpr uint32_t kWorkedFlushMs = 60000;

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
            "SELECT `bot_guid`, `guild_id`, `giver_guid`, `type`, `item_id`, `amount`, `hours`, `delivered`, "
            "`state`, `reason`, `farm_zone`, `farm_chance`, `zone_index`, `deaths_in_zone`, `worked_sec`, "
            "`worked_total`, `reported`, `farm_rest`, `created_at`, `updated_at` FROM `tortoise_bots_guild_order`"));

        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                OrderRecord record;
                record.botGuidLow = fields[0].GetUInt32();
                record.guildId = fields[1].GetUInt32();
                record.giverGuidLow = fields[2].GetUInt32();
                ParseType(fields[3].GetCppString(), record.type);
                record.itemId = fields[4].GetUInt32();
                record.amount = fields[5].GetUInt32();
                record.hours = fields[6].GetUInt32();
                record.delivered = fields[7].GetUInt32();
                ParseState(fields[8].GetCppString(), record.state);
                ParseReason(fields[9].GetCppString(), record.reason);
                record.farmZoneId = fields[10].GetUInt32();
                record.farmChance = static_cast<uint8_t>(fields[11].GetUInt32());
                record.zoneIndex = fields[12].GetUInt32();
                record.deathsInZone = fields[13].GetUInt32();
                record.workedSinceNewItemSec = fields[14].GetUInt32();
                record.workedTotalSec = fields[15].GetUInt32();
                record.reportedHave = fields[16].GetUInt32();
                record.farmRest = fields[17].GetUInt32() != 0;
                record.createdAt = fields[18].GetUInt64();
                record.updatedAt = fields[19].GetUInt64();

                if (record.hours < kMinOrderHours || record.hours > kMaxOrderHours)
                    record.hours = kDefaultOrderHours;
                if (record.botGuidLow && record.itemId && record.amount)
                    m_orders[record.botGuidLow] = Stored{record};
            } while (result->NextRow());
        }

        m_initialized = true;
        TB_LOG_BASIC("TortoiseBots: loaded %zu guild order(s) from database.", m_orders.size());
    }

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
            if (stored.record.guildId == guildId)
                out.push_back(stored.record);
        return out;
    }

    std::vector<OrderRecord> All()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<OrderRecord> out;
        out.reserve(m_orders.size());
        for (auto const& [guid, stored] : m_orders)
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

        if (!CharacterDatabase.DirectPExecute(
                "REPLACE INTO `tortoise_bots_guild_order` "
                "(`bot_guid`, `guild_id`, `giver_guid`, `type`, `item_id`, `amount`, `hours`, `delivered`, "
                "`state`, `reason`, `farm_zone`, `farm_chance`, `zone_index`, `deaths_in_zone`, `worked_sec`, "
                "`worked_total`, `reported`, `farm_rest`, `created_at`, `updated_at`) "
                "VALUES ('%u', '%u', '%u', '%s', '%u', '%u', '%u', '%u', '%s', '%s', '%u', '%u', '%u', '%u', '%u', "
                "'%u', '%u', '%u', FROM_UNIXTIME('%u'), FROM_UNIXTIME('%u'))",
                record.botGuidLow, record.guildId, record.giverGuidLow, TypeToken(record.type),
                record.itemId, record.amount, record.hours, record.delivered, StateToken(record.state),
                ReasonToken(record.reason), record.farmZoneId, record.farmChance,
                record.zoneIndex, record.deathsInZone, record.workedSinceNewItemSec,
                record.workedTotalSec, record.reportedHave, record.farmRest ? 1u : 0u,
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

    bool Cancel(uint32_t botGuidLow)
    {
        if (!botGuidLow)
            return false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_orders.erase(botGuidLow) == 0)
                return false;
        }
        CharacterDatabase.DirectPExecute(
            "DELETE FROM `tortoise_bots_guild_order` WHERE `bot_guid` = '%u'", botGuidLow);
        return true;
    }

    void SetFarmZone(uint32_t botGuidLow, uint32_t zoneId, uint8_t chance, uint32_t zoneIndex)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return;
        it->second.record.farmZoneId = zoneId;
        it->second.record.farmChance = chance;
        it->second.record.zoneIndex = zoneIndex;
        MarkDirtyLocked(it->second);
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
        MoveZoneLocked(it->second);
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
            if (stored.record.guildId == guildId)
                out.push_back(guid);
        return out;
    }

    // One locked read-modify-write per Update pass: take the row, fold the
    // world-thread facts in, and hand back what to persist and what to send.
    // Everything else stays under the caller's single lock hold.
    struct TickIn
    {
        bool botOnline = false;
        bool botAlive = false;
        bool grouped = false;
        uint32_t bagCount = 0;
        uint32_t mailboxCount = 0;
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

        bool wasFinished = IsFinished(record.state);

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

        // The worked clock only runs while the bot actually works.
        if (!wasFinished && in.botOnline && in.botAlive && !in.grouped &&
            record.state == OrderState::Working)
        {
            stored.workedCarryMs += in.workedMs;
            if (stored.workedCarryMs >= 1000)
            {
                uint32_t workedSec = stored.workedCarryMs / 1000;
                stored.workedCarryMs %= 1000;
                record.workedSinceNewItemSec += workedSec;
                record.workedTotalSec += workedSec;
                if (IsOrderTimeout(record.workedTotalSec, record.hours))
                {
                    record.state = OrderState::Cant;
                    record.reason = OrderReason::Timeout;
                }
                else
                    MoveZoneLocked(stored);
                MaybeFlushWorkedLocked(stored, in.nowMs);
            }
        }
        else
            stored.workedCarryMs = 0;

        // A real rise in owned count (bags plus mailbox) seen while online
        // resets the give-up clock. A login after an offline gap never counts
        // as new items.
        uint32_t ownedNow = in.bagCount + in.mailboxCount;
        if (!wasFinished && in.botOnline)
        {
            if (IsNewItemRise(in.botOnline, stored.sawOnline, ownedNow, stored.lastOwnedCount) &&
                record.workedSinceNewItemSec != 0)
            {
                record.workedSinceNewItemSec = 0;
                MarkDirtyLocked(stored);
            }
            stored.lastOwnedCount = ownedNow;
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
            out.legChanged = true;
            MarkDirtyLocked(stored);
        }

        out.record = record;
        out.finishedEdge = !wasFinished && IsFinished(record.state);

        // Reported progress never drops: offline (or not yet re-seen) bots
        // keep max(delivered, last reported).
        out.reportedHave = ReportedHave(record.delivered, ownedNow, in.botOnline, record.reportedHave);

        bool stateChanged = !stored.reportSeen || stored.reportState != record.state ||
                            stored.reportReason != record.reason;
        bool progressDue = out.reportedHave != record.reportedHave &&
                           (!stored.reportSeen || in.nowMs - stored.reportMs >= kProgressReportMs);
        out.sendLine = stateChanged || progressDue || out.finishedEdge;
        if (out.sendLine)
        {
            stored.reportSeen = true;
            stored.reportState = record.state;
            stored.reportReason = record.reason;
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
        bool sawOnline = false;
        bool reportSeen = false;
        OrderState reportState = OrderState::Working;
        OrderReason reportReason = OrderReason::None;
        uint32_t reportMs = 0;
    };

    // A zone move clears the zone so travel re-plans to the next zone of the
    // plan (wrapping around when exhausted), and resets the per-zone clocks.
    // Deaths never end the order; the hours budget in Tick does.
    static void MoveZoneLocked(Stored& stored)
    {
        OrderRecord& record = stored.record;
        if (record.state != OrderState::Working)
            return;
        if (ClassifyZoneMove(record.workedSinceNewItemSec, record.deathsInZone) ==
            ZoneMoveTrigger::None)
            return;
        record.farmZoneId = 0;
        record.farmChance = 0;
        record.workedSinceNewItemSec = 0;
        record.deathsInZone = 0;
        MarkDirtyLocked(stored);
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
            "UPDATE `tortoise_bots_guild_order` SET `hours` = '%u', `delivered` = '%u', `state` = '%s', `reason` = '%s', "
            "`farm_zone` = '%u', `farm_chance` = '%u', `zone_index` = '%u', `deaths_in_zone` = '%u', "
            "`worked_sec` = '%u', `worked_total` = '%u', `reported` = '%u', `farm_rest` = '%u', "
            "`updated_at` = FROM_UNIXTIME('%u') "
            "WHERE `bot_guid` = '%u'",
            record.hours, record.delivered, StateToken(record.state), ReasonToken(record.reason),
            record.farmZoneId, record.farmChance, record.zoneIndex, record.deathsInZone,
            record.workedSinceNewItemSec, record.workedTotalSec, record.reportedHave,
            record.farmRest ? 1u : 0u, static_cast<uint32_t>(record.updatedAt), record.botGuidLow);
    }

    std::mutex m_mutex;
    std::unordered_map<uint32_t, Stored> m_orders;
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

void SendOrderLine(OrderRecord const& order, uint32_t have)
{
    Player* giver = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, order.giverGuidLow));
    if (!giver || !giver->IsInWorld())
        return;
    giver->SendAddonMessage("TBM", FormatOrderLine(order.guildId, order, have));
}

} // namespace

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
    bool gone = OrderStore::Instance().Cancel(botGuidLow);
    if (gone)
        OnDeliveryOrderGone(botGuidLow);
    return gone;
}

void SetFarmZone(uint32_t botGuidLow, uint32_t zoneId, uint8_t chance, uint32_t zoneIndex)
{
    OrderStore::Instance().SetFarmZone(botGuidLow, zoneId, chance, zoneIndex);
}

void SetState(uint32_t botGuidLow, OrderState state, OrderReason reason)
{
    OrderStore::Instance().SetState(botGuidLow, state, reason);
}

void NoteFarmRest(uint32_t botGuidLow)
{
    OrderStore::Instance().NoteFarmRest(botGuidLow);
}

void NoteDeath(uint32_t botGuidLow)
{
    OrderStore::Instance().NoteDeath(botGuidLow);
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
           ReasonToken(order.reason) + "|" + BotCommands::ProtocolSafe(zone) + "|" + difficulty;
}

void Initialize()
{
    OrderStore::Instance().Initialize();
}

void Update(uint32_t diff)
{
    // Give-up and progress ticks run per order, not per bot tick: orders are
    // few, bots are many. The only place that looks up players and sends
    // live updates; map-thread mutators only mark rows dirty.
    uint32_t now = WorldTimer::getMSTime();
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
        in.workedMs = diff;
        in.nowMs = now;
        if (online)
        {
            ::PlayerbotAI* ai = ::PlayerbotAIStorage::Instance().GetAI(bot);
            in.grouped = ai && ai->HasRealPlayerMaster();
        }

        OrderStore::TickOut out = OrderStore::Instance().Tick(guidLow, in);
        if (!out.haveRow)
            continue;
        if (out.sendLine)
            SendOrderLine(out.record, out.reportedHave);
        if (out.legChanged)
            ExpireGuildOrderTarget(guidLow);
    }
    UpdateDeliveries();
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
