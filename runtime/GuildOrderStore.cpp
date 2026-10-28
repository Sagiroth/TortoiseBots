#include "GuildOrderStore.h"

#include "GuildOrderGiveUpPolicy.h"
#include "../commands/CommandShared.h"
#include "../host/ModuleLog.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "PlayerbotAIStorage.h"

// pi-lens-ignore: clang:pp_file_not_found
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
            "SELECT `bot_guid`, `guild_id`, `giver_guid`, `type`, `item_id`, `amount`, `delivered`, "
            "`state`, `reason`, `farm_zone`, `farm_chance`, `moved_zone`, `deaths`, `worked_sec`, "
            "`created_at`, `updated_at` FROM `tortoise_bots_guild_order`"));

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
                record.delivered = fields[6].GetUInt32();
                ParseState(fields[7].GetCppString(), record.state);
                ParseReason(fields[8].GetCppString(), record.reason);
                record.farmZoneId = fields[9].GetUInt32();
                record.farmChance = static_cast<uint8_t>(fields[10].GetUInt32());
                record.movedZone = fields[11].GetUInt32() != 0;
                record.deaths = fields[12].GetUInt32();
                record.workedSinceNewItemSec = fields[13].GetUInt32();
                record.createdAt = fields[14].GetUInt64();
                record.updatedAt = fields[15].GetUInt64();

                if (record.botGuidLow && record.itemId && record.amount)
                    m_orders[record.botGuidLow] = record;
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
        return it->second;
    }

    std::vector<OrderRecord> ListForGuild(uint32_t guildId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<OrderRecord> out;
        for (auto const& [guid, record] : m_orders)
            if (record.guildId == guildId)
                out.push_back(record);
        return out;
    }
    std::vector<OrderRecord> All()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<OrderRecord> out;
        out.reserve(m_orders.size());
        for (auto const& [guid, record] : m_orders)
            out.push_back(record);
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
                "(`bot_guid`, `guild_id`, `giver_guid`, `type`, `item_id`, `amount`, `delivered`, "
                "`state`, `reason`, `farm_zone`, `farm_chance`, `moved_zone`, `deaths`, `worked_sec`, "
                "`created_at`, `updated_at`) "
                "VALUES ('%u', '%u', '%u', '%s', '%u', '%u', '%u', '%s', '%s', '%u', '%u', '%u', '%u', '%u', "
                "FROM_UNIXTIME('%u'), FROM_UNIXTIME('%u'))",
                record.botGuidLow, record.guildId, record.giverGuidLow, TypeToken(record.type),
                record.itemId, record.amount, record.delivered, StateToken(record.state),
                ReasonToken(record.reason), record.farmZoneId, record.farmChance,
                record.movedZone ? 1u : 0u, record.deaths, record.workedSinceNewItemSec,
                static_cast<uint32_t>(record.createdAt), static_cast<uint32_t>(record.updatedAt)))
        {
            sLog.outError("TortoiseBots: failed to persist guild order for bot %u", record.botGuidLow);
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_orders[record.botGuidLow] = record;
            m_reported[record.botGuidLow] = ReportState{};
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
            m_reported.erase(botGuidLow);
        }
        CharacterDatabase.DirectPExecute(
            "DELETE FROM `tortoise_bots_guild_order` WHERE `bot_guid` = '%u'", botGuidLow);
        return true;
    }

    void SetFarmZone(uint32_t botGuidLow, uint32_t zoneId, uint8_t chance)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return;
        it->second.farmZoneId = zoneId;
        it->second.farmChance = chance;
        SaveLocked(it->second);
    }

    // Returns the record when the state actually changed.
    std::optional<OrderRecord> SetState(uint32_t botGuidLow, OrderState state, OrderReason reason)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return std::nullopt;
        if (it->second.state == state && it->second.reason == reason)
            return std::nullopt;
        it->second.state = state;
        it->second.reason = reason;
        SaveLocked(it->second);
        return it->second;
    }

    std::optional<OrderRecord> NoteNewItems(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || IsFinished(it->second.state))
            return std::nullopt;
        bool changed = it->second.workedSinceNewItemSec != 0;
        it->second.workedSinceNewItemSec = 0;
        if (changed)
            SaveLocked(it->second);
        return it->second;
    }

    std::optional<OrderRecord> NoteDeath(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || IsFinished(it->second.state))
            return std::nullopt;
        ++it->second.deaths;
        if (!ApplyGiveUpLocked(it->second))
            SaveLocked(it->second);
        return it->second;
    }

    std::optional<OrderRecord> AddWorkedTime(uint32_t botGuidLow, uint32_t seconds)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || IsFinished(it->second.state))
            return std::nullopt;
        it->second.workedSinceNewItemSec += seconds;
        if (!ApplyGiveUpLocked(it->second))
            SaveLocked(it->second);
        return it->second;
    }

    std::optional<OrderRecord> NoteBagsFull(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end() || IsFinished(it->second.state))
            return std::nullopt;
        if (it->second.state == OrderState::Delivering)
            return std::nullopt;
        it->second.state = OrderState::Delivering;
        it->second.reason = OrderReason::BagsFull;
        SaveLocked(it->second);
        return it->second;
    }

    std::optional<OrderRecord> AddDelivered(uint32_t botGuidLow, uint32_t count)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_orders.find(botGuidLow);
        if (it == m_orders.end())
            return std::nullopt;
        it->second.delivered += count;
        SaveLocked(it->second);
        return it->second;
    }

    struct ReportState
    {
        OrderState state = OrderState::Working;
        OrderReason reason = OrderReason::None;
        uint32_t have = 0;
        uint32_t lastProgressMs = 0;
        bool seen = false;
    };

    ReportState GetReport(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_reported[botGuidLow];
    }

    void PutReport(uint32_t botGuidLow, ReportState report)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_reported[botGuidLow] = report;
    }

    std::vector<uint32_t> BotsForGuild(uint32_t guildId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<uint32_t> out;
        for (auto const& [guid, record] : m_orders)
            if (record.guildId == guildId)
                out.push_back(guid);
        return out;
    }

private:
    // First threshold hit moves zone once, the second ends the order as stuck.
    // Returns true when the state changed through the give-up rule.
    bool ApplyGiveUpLocked(OrderRecord& record)
    {
        if (record.state != OrderState::Working && record.state != OrderState::Waiting)
            return false;
        GiveUpAction action =
            ClassifyGiveUp(record.workedSinceNewItemSec, record.deaths, record.movedZone);
        if (action == GiveUpAction::KeepWorking)
            return false;
        if (action == GiveUpAction::MoveZone)
        {
            record.movedZone = true;
            record.farmZoneId = 0;
            record.farmChance = 0;
            record.workedSinceNewItemSec = 0;
            record.deaths = 0;
        }
        else
        {
            record.state = OrderState::Cant;
            record.reason = OrderReason::Stuck;
        }
        SaveLocked(record);
        return true;
    }

    void SaveLocked(OrderRecord& record)
    {
        record.updatedAt = static_cast<uint64_t>(std::time(nullptr));
        CharacterDatabase.PExecute(
            "UPDATE `tortoise_bots_guild_order` SET `delivered` = '%u', `state` = '%s', `reason` = '%s', "
            "`farm_zone` = '%u', `farm_chance` = '%u', `moved_zone` = '%u', `deaths` = '%u', "
            "`worked_sec` = '%u', `updated_at` = FROM_UNIXTIME('%u') WHERE `bot_guid` = '%u'",
            record.delivered, StateToken(record.state), ReasonToken(record.reason), record.farmZoneId,
            record.farmChance, record.movedZone ? 1u : 0u, record.deaths,
            record.workedSinceNewItemSec, static_cast<uint32_t>(record.updatedAt), record.botGuidLow);
    }

    std::mutex m_mutex;
    std::unordered_map<uint32_t, OrderRecord> m_orders;
    std::unordered_map<uint32_t, ReportState> m_reported;
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

} // namespace

std::optional<OrderRecord> Find(uint32_t botGuidLow)
{
    return OrderStore::Instance().Find(botGuidLow);
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
    return OrderStore::Instance().Cancel(botGuidLow);
}

void SetFarmZone(uint32_t botGuidLow, uint32_t zoneId, uint8_t chance)
{
    OrderStore::Instance().SetFarmZone(botGuidLow, zoneId, chance);
}

void SetState(uint32_t botGuidLow, OrderState state, OrderReason reason)
{
    std::optional<OrderRecord> changed = OrderStore::Instance().SetState(botGuidLow, state, reason);
    if (!changed)
        return;
    ObjectGuid guid(HIGHGUID_PLAYER, botGuidLow);
    Player* bot = sObjectAccessor.FindPlayer(guid);
    SendLiveUpdate(*changed, Progress(bot, *changed));
}

bool NoteNewItems(uint32_t botGuidLow, uint32_t /*have*/)
{
    std::optional<OrderRecord> record = OrderStore::Instance().NoteNewItems(botGuidLow);
    if (!record)
        return false;
    return record->movedZone || IsFinished(record->state);
}

void NoteDeath(uint32_t botGuidLow)
{
    std::optional<OrderRecord> record = OrderStore::Instance().NoteDeath(botGuidLow);
    if (!record)
        return;
    ObjectGuid guid(HIGHGUID_PLAYER, botGuidLow);
    Player* bot = sObjectAccessor.FindPlayer(guid);
    SendLiveUpdate(*record, Progress(bot, *record));
}

void AddWorkedTime(uint32_t botGuidLow, uint32_t seconds)
{
    std::optional<OrderRecord> record = OrderStore::Instance().AddWorkedTime(botGuidLow, seconds);
    if (!record)
        return;
    ObjectGuid guid(HIGHGUID_PLAYER, botGuidLow);
    Player* bot = sObjectAccessor.FindPlayer(guid);
    SendLiveUpdate(*record, Progress(bot, *record));
}

void NoteBagsFull(uint32_t botGuidLow)
{
    std::optional<OrderRecord> record = OrderStore::Instance().NoteBagsFull(botGuidLow);
    if (!record)
        return;
    ObjectGuid guid(HIGHGUID_PLAYER, botGuidLow);
    Player* bot = sObjectAccessor.FindPlayer(guid);
    SendLiveUpdate(*record, Progress(bot, *record));
}

void AddDelivered(uint32_t botGuidLow, uint32_t count)
{
    std::optional<OrderRecord> record = OrderStore::Instance().AddDelivered(botGuidLow, count);
    if (!record)
        return;
    ObjectGuid guid(HIGHGUID_PLAYER, botGuidLow);
    Player* bot = sObjectAccessor.FindPlayer(guid);
    SendLiveUpdate(*record, Progress(bot, *record));
}

uint32_t Progress(Player* bot, OrderRecord const& order)
{
    uint32_t have = order.delivered;
    if (bot && bot->IsInWorld() && order.itemId)
        have += bot->GetItemCount(order.itemId);
    return have;
}

std::string FormatOrderLine(uint32_t guildId, OrderRecord const& order)
{
    uint32_t have;
    {
        ObjectGuid guid(HIGHGUID_PLAYER, order.botGuidLow);
        have = Progress(sObjectAccessor.FindPlayer(guid), order);
    }
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(order.itemId);
    std::string itemName = proto ? proto->Name1 : "?";
    std::string zone = ZoneName(order.farmZoneId);
    if (order.type == OrderType::Buy && zone == "-")
        zone = "Auction House";
    (void)guildId;
    return "TBM:ORDER|" + BotCommands::ProtocolSafe(BotName(order.botGuidLow)) + "|" +
           TypeToken(order.type) + "|" + std::to_string(order.itemId) + "|" +
           BotCommands::ProtocolSafe(itemName) + "|" + std::to_string(have) + "|" +
           std::to_string(order.amount) + "|" + StateToken(order.state) + "|" +
           ReasonToken(order.reason) + "|" + BotCommands::ProtocolSafe(zone);
}

void SendLiveUpdate(OrderRecord const& order, uint32_t have)
{
    OrderStore& store = OrderStore::Instance();
    OrderStore::ReportState report = store.GetReport(order.botGuidLow);
    uint32_t now = WorldTimer::getMSTime();

    bool stateChanged =
        !report.seen || report.state != order.state || report.reason != order.reason;
    bool progressDue =
        have != report.have && (!report.seen || now - report.lastProgressMs >= kProgressReportMs);
    if (!stateChanged && !progressDue)
        return;

    report.seen = true;
    report.state = order.state;
    report.reason = order.reason;
    if (have != report.have)
    {
        report.have = have;
        report.lastProgressMs = now;
    }
    store.PutReport(order.botGuidLow, report);

    Player* giver = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, order.giverGuidLow));
    if (!giver || !giver->IsInWorld())
        return;
    giver->SendAddonMessage("TBM", FormatOrderLine(order.guildId, order));
}

void Initialize()
{
    OrderStore::Instance().Initialize();
}

void Update(uint32_t diff)
{
    // Give-up and progress ticks run per order, not per bot tick: orders are
    // few, bots are many.
    static std::unordered_map<uint32_t, uint32_t> carryMs;
    static std::unordered_map<uint32_t, uint32_t> lastHave;

    // Snapshot the rows once; per-order calls re-lock individually.
    std::vector<OrderRecord> orders = OrderStore::Instance().All();

    for (OrderRecord const& snapshot : orders)
    {
        uint32_t guidLow = snapshot.botGuidLow;
        std::optional<OrderRecord> current = OrderStore::Instance().Find(guidLow);
        if (!current || IsFinished(current->state))
        {
            carryMs.erase(guidLow);
            lastHave.erase(guidLow);
            continue;
        }

        ObjectGuid guid(HIGHGUID_PLAYER, guidLow);
        Player* bot = sObjectAccessor.FindPlayer(guid);
        uint32_t have = Progress(bot, *current);

        auto lastIt = lastHave.find(guidLow);
        if (lastIt != lastHave.end() && have > lastIt->second &&
            (current->state == OrderState::Working || current->state == OrderState::Waiting))
        {
            OrderStore::Instance().NoteNewItems(guidLow);
            current = OrderStore::Instance().Find(guidLow);
            if (!current)
            {
                carryMs.erase(guidLow);
                lastHave.erase(guidLow);
                continue;
            }
        }
        lastHave[guidLow] = have;

        ::PlayerbotAI* ai = (bot && bot->IsInWorld())
            ? ::PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
        bool grouped = ai && ai->HasRealPlayerMaster();

        // Group gate: no order work while in a group with a player.
        if (grouped && current->state == OrderState::Working)
        {
            OrderStore::Instance().SetState(guidLow, OrderState::Waiting, OrderReason::Grouped);
            current = OrderStore::Instance().Find(guidLow);
        }
        else if (!grouped && current->state == OrderState::Waiting && current->reason == OrderReason::Grouped)
        {
            OrderStore::Instance().SetState(guidLow, OrderState::Working, OrderReason::None);
            current = OrderStore::Instance().Find(guidLow);
        }
        if (!current)
        {
            carryMs.erase(guidLow);
            lastHave.erase(guidLow);
            continue;
        }

        bool working = bot && bot->IsInWorld() && bot->IsAlive() && !grouped &&
                       current->state == OrderState::Working;
        if (working)
        {
            carryMs[guidLow] += diff;
            if (carryMs[guidLow] >= 1000)
            {
                uint32_t seconds = carryMs[guidLow] / 1000;
                carryMs[guidLow] %= 1000;
                OrderStore::Instance().AddWorkedTime(guidLow, seconds);
                current = OrderStore::Instance().Find(guidLow);
                if (!current)
                {
                    carryMs.erase(guidLow);
                    lastHave.erase(guidLow);
                    continue;
                }
            }
        }
        else
            carryMs[guidLow] = 0;

        // Amount reached: the bot heads for the guild bank (package 4 walks
        // it there; until then the state alone marks it delivering).
        if (have >= current->amount && current->state == OrderState::Working &&
            current->amount > 0)
        {
            OrderStore::Instance().SetState(guidLow, OrderState::Delivering, OrderReason::None);
            current = OrderStore::Instance().Find(guidLow);
            if (!current)
                continue;
        }

        SendLiveUpdate(*current, have);
    }
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
