// Module-local compatibility implementation for behavior that was written
// against CMaNGOS's RandomPlayerbotMgr/PlayerbotHolder interfaces.
//
// The donor manager source is intentionally not compiled. This file preserves
// the behavior-facing parts that the Vanilla strategy families call while all
// character/session ownership remains in BotManager and BotSessionAdapter.

#include "../ai/playerbot/RandomPlayerbotMgr.h"
#include "../ai/playerbot/PlayerbotMgr.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/PlayerbotFactory.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/BotState.h"
#include "../ai/playerbot/TravelMgr.h"
#include "../runtime/PlayerbotAIStorage.h"
#include "BotManager.h"

#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Objects/Player.h"
#include "Log.h"
#include "Database/DatabaseEnv.h"

#include <mutex>
#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace
{
struct StoredValue
{
    uint32 value = 0;
    std::string data;
    int32 validIn = -1;
    time_t expiresAt = 0;
};

std::mutex s_valuesMutex;
std::unordered_map<std::string, StoredValue> s_values;
std::unordered_map<std::string, uint32> s_tradeDiscounts;

bool RandomTravelTeleport(Player* bot, uint32 purpose, bool activeOnly)
{
    if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported() || bot->InBattleGround())
        return false;

    if (activeOnly)
    {
        if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
        {
            if (!ai->AllowActivity())
                return false;
        }
    }

    ai::PlayerTravelInfo travelInfo(bot);
    ai::WorldPosition center(bot);
    std::vector<uint32> partitions = { 500, 2000, 5000, 10000, 50000 };
    ai::PartitionedTravelList destinations = sTravelMgr.GetPartitions(
        center, partitions, travelInfo, purpose, {}, false, 1000000.0f);

    std::vector<ai::WorldPosition*> points;
    for (auto const& partition : destinations)
        for (auto const& point : partition.second)
            if (std::get<1>(point))
                points.push_back(std::get<1>(point));

    if (points.empty())
        return bot->TeleportToHomebind();

    ai::WorldPosition* destination = points[urand(0, static_cast<uint32>(points.size() - 1))];
    if (!destination || !bot->TeleportTo(*destination))
        return false;

    sLog.outDetail("TortoiseBots: native random travel moved %s to %s",
        bot->GetName(), destination->print().c_str());
    return true;
}

std::string ValueKey(uint32 guid, std::string const& name)
{
    return std::to_string(guid) + "\n" + name;
}

std::string TradeKey(ObjectGuid bot, ObjectGuid master)
{
    return bot.GetString() + "\n" + master.GetString();
}
}

PlayerbotHolder::PlayerbotHolder() : PlayerbotAIBase()
{
}

PlayerbotHolder::~PlayerbotHolder() = default;

void PlayerbotHolder::UpdateAIInternal(uint32 elapsed, bool /*minimal*/)
{
    // Keep legacy holder callers on the same module-owned update path. The
    // native WorldScript is the normal caller; this only matters for old
    // holder-facing code that still invokes the virtual.
    TortoiseBots::BotManager::Instance().OnWorldUpdate(elapsed);
}

void PlayerbotHolder::OnBotDeleted(uint32 botGuid, uint32 /*accountId*/)
{
    ObjectGuid const guid(HIGHGUID_PLAYER, botGuid);
    if (TortoiseBots::BotManager::Instance().IsBot(guid))
        TortoiseBots::BotManager::Instance().RemoveBot(guid, false);
}

uint32 PlayerbotHolder::GetOrCreateAccount(Player* master, std::string& error)
{
    if (master && master->GetSession())
        return master->GetSession()->GetAccountId();

    error = "Native TortoiseBots requires an existing owner account";
    return 0;
}

botPID::botPID(double /*dt*/, double /*max*/, double /*min*/, double /*Kp*/, double /*Ki*/, double /*Kd*/)
    : pimpl(nullptr)
{
}

void botPID::adjust(double /*Kp*/, double /*Ki*/, double /*Kd*/)
{
}

void botPID::reset()
{
}

double botPID::calculate(double setpoint, double /*pv*/)
{
    return setpoint;
}

botPID::~botPID() = default;

void PlayerbotHolder::LogoutPlayerBot(uint32 guid, bool /*allowInstant*/, bool /*forDelete*/)
{
    TortoiseBots::BotManager::Instance().RemoveBot(ObjectGuid(HIGHGUID_PLAYER, guid), true);
}

Player* PlayerbotHolder::GetPlayerBot(uint32 guid) const
{
    ObjectGuid const objectGuid(HIGHGUID_PLAYER, guid);
    return sObjectAccessor.FindPlayer(objectGuid);
}

void PlayerbotHolder::ForEachPlayerbot(std::function<void(Player*)> callback) const
{
    if (!callback)
        return;

    for (Player* player : TortoiseBots::BotManager::Instance().GetAllBots())
        callback(player);
}

void PlayerbotHolder::UpdateSessions(uint32 /*elapsed*/)
{
    // Penqle's World owns generic network/headless session processing. The
    // native BotManager only owns lifecycle records; it must not recreate the
    // donor manager's second session loop here.
}

void PlayerbotHolder::LogoutAllBots()
{
    for (Player* player : TortoiseBots::BotManager::Instance().GetAllBots())
        if (player)
            TortoiseBots::BotManager::Instance().RemoveBot(player->GetObjectGuid(), true);
}

void PlayerbotHolder::JoinChatChannels(Player* /*bot*/)
{
    // Channel membership is optional social behaviour. Native PlayerbotAI
    // handles party/master traffic through the generic packet seam; there is
    // no donor channel-manager loop in the native random service.
}

void PlayerbotHolder::OnBotLogin(Player* bot)
{
    if (bot)
        TortoiseBots::BotManager::Instance().OnPlayerLogin(bot);
}

void PlayerbotHolder::MovePlayerBot(uint32 guid, PlayerbotHolder* /*newHolder*/)
{
    LogoutPlayerBot(guid);
}

uint32 PlayerbotHolder::GetPlayerbotsAmount() const
{
    return TortoiseBots::BotManager::Instance().GetBotCount();
}

RandomPlayerbotMgr::RandomPlayerbotMgr() = default;
RandomPlayerbotMgr::~RandomPlayerbotMgr() = default;

void RandomPlayerbotMgr::SyncNativePlayers()
{
    players.clear();
    for (Player* player : TortoiseBots::BotManager::Instance().GetAllBots())
    {
        if (!player || !player->GetSession() || !player->GetSession()->IsHeadless())
            continue;

        TortoiseBots::BotRecord* record =
            TortoiseBots::BotManager::Instance().FindBot(player->GetObjectGuid());
        if (record && record->random && player->IsInWorld())
            players[player->GetObjectGuid().GetCounter()] = player;
    }
}

void RandomPlayerbotMgr::UpdateAIInternal(uint32 /*elapsed*/, bool /*minimal*/)
{
    // RandomBotService and BotManager are the only native update owners.
}

void RandomPlayerbotMgr::OnBotLoginInternal(Player* bot)
{
    if (bot)
        TortoiseBots::BotManager::Instance().OnPlayerLogin(bot);
}

void RandomPlayerbotMgr::MovePlayerBot(uint32 guid, PlayerbotHolder* newHolder)
{
    PlayerbotHolder::MovePlayerBot(guid, newHolder);
}

uint32 RandomPlayerbotMgr::GetOrCreateAccount(Player* master, std::string& error)
{
    return PlayerbotHolder::GetOrCreateAccount(master, error);
}

void RandomPlayerbotMgr::OnBotDeleted(uint32 botGuid, uint32 accountId)
{
    PlayerbotHolder::OnBotDeleted(botGuid, accountId);
}

bool RandomPlayerbotMgr::IsRandomBot(Player* bot)
{
    return bot && IsRandomBot(bot->GetObjectGuid());
}

bool RandomPlayerbotMgr::IsRandomBot(uint32 guid)
{
    return TortoiseBots::BotManager::Instance().IsRandomBot(ObjectGuid(HIGHGUID_PLAYER, guid));
}

uint32 RandomPlayerbotMgr::GetValue(Player* bot, std::string type)
{
    return bot ? GetValue(bot->GetObjectGuid().GetCounter(), std::move(type)) : 0;
}

uint32 RandomPlayerbotMgr::GetValue(uint32 guid, std::string type)
{
    std::lock_guard<std::mutex> lock(s_valuesMutex);
    auto it = s_values.find(ValueKey(guid, type));
    if (it == s_values.end())
        return 0;
    if (it->second.expiresAt && time(nullptr) >= it->second.expiresAt)
        return 0;
    return it->second.value;
}

int32 RandomPlayerbotMgr::GetValueValidTime(uint32 guid, std::string event)
{
    std::lock_guard<std::mutex> lock(s_valuesMutex);
    auto it = s_values.find(ValueKey(guid, event));
    if (it == s_values.end() || !it->second.expiresAt)
        return it == s_values.end() ? 0 : it->second.validIn;
    time_t remaining = it->second.expiresAt - time(nullptr);
    return remaining > 0 ? static_cast<int32>(remaining) : 0;
}

std::string RandomPlayerbotMgr::GetData(uint32 guid, std::string type)
{
    std::lock_guard<std::mutex> lock(s_valuesMutex);
    auto it = s_values.find(ValueKey(guid, type));
    if (it == s_values.end() || (it->second.expiresAt && time(nullptr) >= it->second.expiresAt))
        return std::string();
    return it->second.data;
}

void RandomPlayerbotMgr::SetValue(uint32 guid, std::string type, uint32 value, std::string data, int32 validIn)
{
    std::lock_guard<std::mutex> lock(s_valuesMutex);
    time_t expiresAt = validIn > 0 ? time(nullptr) + validIn : 0;
    s_values[ValueKey(guid, type)] = StoredValue{value, std::move(data), validIn, expiresAt};
}

void RandomPlayerbotMgr::SetValue(Player* bot, std::string type, uint32 value, std::string data, int32 validIn)
{
    if (bot)
        SetValue(bot->GetObjectGuid().GetCounter(), std::move(type), value, std::move(data), validIn);
}

double RandomPlayerbotMgr::GetBuyMultiplier(Player* bot)
{
    if (!bot)
        return 1.0;

    uint32 value = GetValue(bot, "buymultiplier");
    if (!value)
    {
        value = urand(50, 120);
        SetValue(bot, "buymultiplier", value, {},
            static_cast<int32>(sPlayerbotAIConfig.maxRandomBotsPriceChangeInterval));
    }

    return static_cast<double>(value) / 100.0;
}

double RandomPlayerbotMgr::GetSellMultiplier(Player* bot)
{
    if (!bot)
        return 1.0;

    uint32 value = GetValue(bot, "sellmultiplier");
    if (!value)
    {
        value = urand(80, 250);
        SetValue(bot, "sellmultiplier", value, {},
            static_cast<int32>(sPlayerbotAIConfig.maxRandomBotsPriceChangeInterval));
    }

    return static_cast<double>(value) / 100.0;
}

uint32 RandomPlayerbotMgr::GetTradeDiscount(Player* bot, Player* master)
{
    if (!bot || !master)
        return 0;

    std::lock_guard<std::mutex> lock(s_valuesMutex);
    auto it = s_tradeDiscounts.find(TradeKey(bot->GetObjectGuid(), master->GetObjectGuid()));
    return it == s_tradeDiscounts.end() ? 0 : it->second;
}

void RandomPlayerbotMgr::SetTradeDiscount(Player* bot, Player* master, uint32 value)
{
    if (!bot || !master)
        return;

    std::lock_guard<std::mutex> lock(s_valuesMutex);
    s_tradeDiscounts[TradeKey(bot->GetObjectGuid(), master->GetObjectGuid())] = value;
}

void RandomPlayerbotMgr::AddTradeDiscount(Player* bot, Player* master, int32 value)
{
    uint32 current = GetTradeDiscount(bot, master);
    SetTradeDiscount(bot, master, value < 0 && current < static_cast<uint32>(-value)
        ? 0
        : static_cast<uint32>(static_cast<int64>(current) + value));
}

void RandomPlayerbotMgr::Remove(Player* bot)
{
    if (bot)
        TortoiseBots::BotManager::Instance().RemoveBot(bot->GetObjectGuid(), true);
}

void RandomPlayerbotMgr::Refresh(Player* bot)
{
    if (!bot || !IsRandomBot(bot))
        return;

    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.Refresh();
}

void RandomPlayerbotMgr::UpdateGearSpells(Player* bot)
{
    if (!bot || !IsRandomBot(bot))
        return;

    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.UpgradeGearBest();
}

bool RandomPlayerbotMgr::ProcessBot(Player* player)
{
    if (!player || !IsRandomBot(player) || !player->IsInWorld() || player->IsBeingTeleported())
        return false;

    if (!player->IsAlive())
    {
        Revive(player);
        return true;
    }

    if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(player))
        ai->GetAiObjectContext()->ClearExpiredValues();
    return true;
}

bool RandomPlayerbotMgr::GetNamedLocation(std::string const& name, WorldLocation& location)
{
    std::string escaped = name;
    WorldDatabase.escape_string(escaped);
    auto result = WorldDatabase.PQuery(
        "SELECT map_id, position_x, position_y, position_z, orientation "
        "FROM ai_playerbot_named_location WHERE name = '%s' LIMIT 1", escaped.c_str());
    if (!result)
        return false;

    Field* fields = result->Fetch();
    location = WorldLocation(fields[0].GetUInt32(), fields[1].GetFloat(), fields[2].GetFloat(),
        fields[3].GetFloat(), fields[4].GetFloat());
    return true;
}

void RandomPlayerbotMgr::LoadBattleMastersCache()
{
    BattleMastersCache.clear();

    // Keep the mature value path's compatibility view backed by the same
    // native table used by BattleGroundMgr. The cache is loaded once during
    // AI initialization; it is not a second battleground owner and is never
    // consulted by the random service's world-tick loop.
    auto result = WorldDatabase.Query(
        "SELECT entry, bg_template FROM battlemaster_entry");
    if (!result)
    {
        sLog.outString("TortoiseBots: battlemaster_entry is empty; battleground target discovery unavailable");
        return;
    }

    uint32 loaded = 0;
    do
    {
        Field* fields = result->Fetch();
        uint32 entry = fields[0].GetUInt32();
        uint32 bgTemplate = fields[1].GetUInt32();
        if (!entry || !sObjectMgr.GetCreatureTemplate(entry))
            continue;

        // Faction/area filtering remains in BgMasterValue. Keeping the
        // native entry in the neutral bucket avoids recreating the donor's
        // faction-template cache while preserving valid battlemaster
        // candidates.
        BattleMastersCache[TEAM_BOTH_ALLOWED][BattleGroundTypeId(bgTemplate)].push_back(entry);
        ++loaded;
    } while (result->NextRow());

    sLog.outString("TortoiseBots: loaded %u native battlemaster entries for mature AI values", loaded);
}

InventoryResult RandomPlayerbotMgr::CanEquipUnseenItem(Player* player, uint8 slot, uint16& dest, uint32 item)
{
    if (!player)
        return EQUIP_ERR_ITEM_NOT_FOUND;

    ItemPrototype const* prototype = sObjectMgr.GetItemPrototype(item);
    return prototype ? player->CanEquipItem(slot, dest, prototype, nullptr, false) : EQUIP_ERR_ITEM_NOT_FOUND;
}

const CreatureDataPair* RandomPlayerbotMgr::GetCreatureDataByEntry(uint32 entry)
{
    if (!entry || !sObjectMgr.GetCreatureTemplate(entry))
        return nullptr;

    FindCreatureData worker(entry, nullptr);
    sObjectMgr.DoCreatureData(worker);
    return worker.GetResult();
}

uint32 RandomPlayerbotMgr::GetCreatureGuidByEntry(uint32 entry)
{
    CreatureDataPair const* data = GetCreatureDataByEntry(entry);
    return data ? data->first : 0;
}

uint32 RandomPlayerbotMgr::GetBattleMasterEntry(Player* /*bot*/, BattleGroundTypeId bgTypeId, bool /*fake*/)
{
    // Penqle keeps the inverse entry -> battleground map private to its
    // BattleGroundMgr. This is an on-demand lookup used by the mature command
    // path, not a tick query, and uses the same native table as that manager.
    auto result = WorldDatabase.PQuery(
        "SELECT entry FROM battlemaster_entry WHERE bg_template = '%u' LIMIT 1",
        static_cast<uint32>(bgTypeId));
    return result ? result->Fetch()[0].GetUInt32() : 0;
}

bool RandomPlayerbotMgr::IsPinnedBot(uint32 guidLow)
{
    PlayerCacheData* data = sObjectMgr.GetPlayerDataByGUID(guidLow);
    if (!data)
        return false;

    for (std::string pinned : sPlayerbotAIConfig.pinnedBotNames)
    {
        std::string cached = data->sName;
        if (normalizePlayerName(pinned) && normalizePlayerName(cached) && pinned == cached)
            return true;
    }
    return false;
}

bool RandomPlayerbotMgr::AddRandomBot(uint32 guid)
{
    ObjectGuid const objectGuid(HIGHGUID_PLAYER, guid);
    uint32 const accountId = sObjectMgr.GetPlayerAccountIdByGUID(objectGuid);
    return accountId && TortoiseBots::BotManager::Instance().AddRandomBot(accountId, objectGuid);
}

void RandomPlayerbotMgr::RandomTeleportForLevel(Player* bot, bool activeOnly)
{
    if (!bot || !IsRandomBot(bot) || !sPlayerbotAIConfig.enableRandomTeleports)
        return;

    uint32 purpose = static_cast<uint32>(ai::TravelDestinationPurpose::Grind) |
        static_cast<uint32>(ai::TravelDestinationPurpose::Explore) |
        static_cast<uint32>(ai::TravelDestinationPurpose::GenericRpg);
    if (RandomTravelTeleport(bot, purpose, activeOnly))
        Refresh(bot);
}

void RandomPlayerbotMgr::RandomTeleportForRpg(Player* bot, bool activeOnly)
{
    if (!bot || !IsRandomBot(bot) || !sPlayerbotAIConfig.enableRandomTeleports)
        return;

    uint32 purpose = static_cast<uint32>(ai::TravelDestinationPurpose::GenericRpg) |
        static_cast<uint32>(ai::TravelDestinationPurpose::Explore);
    if (RandomTravelTeleport(bot, purpose, activeOnly))
        Refresh(bot);
}

void RandomPlayerbotMgr::ChangeStrategy(Player* player)
{
    if (!player)
        return;

    if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(player))
    {
        ai->ChangeStrategy(sPlayerbotAIConfig.randomBotCombatStrategies, BotState::BOT_STATE_COMBAT);
        ai->ChangeStrategy(sPlayerbotAIConfig.randomBotNonCombatStrategies, BotState::BOT_STATE_NON_COMBAT);
    }
}

void RandomPlayerbotMgr::Revive(Player* player)
{
    if (player && player->IsInWorld() && !player->IsAlive())
        player->RepopAtGraveyard();
}

void RandomPlayerbotMgr::PrintTeleportCache()
{
    auto locations = WorldDatabase.Query("SELECT COUNT(*) FROM ai_playerbot_named_location");
    uint32 namedLocations = locations ? locations->Fetch()[0].GetUInt32() : 0;
    sLog.outString("TortoiseBots: native TravelMgr owns random travel points; named-location rows: %u", namedLocations);
}

void RandomPlayerbotMgr::PrintStats(uint32 requesterGuid)
{
    uint32 total = 0;
    uint32 random = 0;
    uint32 inWorld = 0;
    uint32 withAi = 0;
    std::map<uint32, uint32> classCounts;

    for (Player* bot : TortoiseBots::BotManager::Instance().GetAllBots())
    {
        TortoiseBots::BotRecord* record = bot ? TortoiseBots::BotManager::Instance().FindBot(bot->GetObjectGuid()) : nullptr;
        if (!bot || !record)
            continue;
        ++total;
        random += record->random ? 1 : 0;
        inWorld += bot->IsInWorld() ? 1 : 0;
        withAi += PlayerbotAIStorage::Instance().GetAI(bot) ? 1 : 0;
        ++classCounts[bot->GetClass()];
    }

    std::ostringstream out;
    out << "Native random-bot stats: total=" << total
        << " random=" << random << " inWorld=" << inWorld << " matureAI=" << withAi;
    for (auto const& entry : classCounts)
        out << " class" << entry.first << "=" << entry.second;
    sLog.outString("%s", out.str().c_str());

    if (Player* requester = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, requesterGuid)))
        requester->SendMessageToPlayer(out.str());
}
