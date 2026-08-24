// Small adapters for mature Vanilla/Turtle strategy code.
//
// These functions translate behavior-facing queries to the native owners. They
// do not own sessions, players, AI instances, or random-bot population.

#include "../ai/playerbot/RandomBotFacade.h"

#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/PlayerbotFactory.h"
#include "../ai/playerbot/BotState.h"
#include "../ai/playerbot/TravelMgr.h"
#include "../runtime/BotManager.h"
#include "../runtime/PlayerbotAIStorage.h"

#include "AuctionHouse/AuctionHouseMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Objects/Player.h"
#include "Log.h"
#include "Database/DatabaseEnv.h"

#include <algorithm>
#include <ctime>
#include <map>
#include <mutex>
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

std::string ValueKey(uint32 guid, std::string const& name)
{
    return std::to_string(guid) + "\n" + name;
}

std::string TradeKey(ObjectGuid bot, ObjectGuid master)
{
    return bot.GetString() + "\n" + master.GetString();
}
}

void RandomBotFacade::SyncNativePlayers()
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

bool RandomBotFacade::IsRandomBot(Player* bot)
{
    return bot && IsRandomBot(bot->GetObjectGuid().GetCounter());
}

bool RandomBotFacade::IsRandomBot(uint32 guid)
{
    return TortoiseBots::BotManager::Instance().IsRandomBot(ObjectGuid(HIGHGUID_PLAYER, guid));
}

bool RandomBotFacade::IsFreeBot(Player* bot)
{
    return bot && IsFreeBot(bot->GetObjectGuid().GetCounter());
}

bool RandomBotFacade::IsFreeBot(uint32 guid)
{
    return IsRandomBot(guid) || sPlayerbotAIConfig.IsFreeAltBot(guid);
}

uint32 RandomBotFacade::GetValue(Player* bot, std::string type)
{
    return bot ? GetValue(bot->GetObjectGuid().GetCounter(), std::move(type)) : 0;
}

uint32 RandomBotFacade::GetValue(uint32 guid, std::string type)
{
    std::lock_guard<std::mutex> lock(s_valuesMutex);
    auto it = s_values.find(ValueKey(guid, type));
    if (it == s_values.end())
        return 0;
    if (it->second.expiresAt && time(nullptr) >= it->second.expiresAt)
        return 0;
    return it->second.value;
}

int32 RandomBotFacade::GetValueValidTime(uint32 guid, std::string event)
{
    std::lock_guard<std::mutex> lock(s_valuesMutex);
    auto it = s_values.find(ValueKey(guid, event));
    if (it == s_values.end() || !it->second.expiresAt)
        return it == s_values.end() ? 0 : it->second.validIn;
    time_t remaining = it->second.expiresAt - time(nullptr);
    return remaining > 0 ? static_cast<int32>(remaining) : 0;
}

std::string RandomBotFacade::GetData(uint32 guid, std::string type)
{
    std::lock_guard<std::mutex> lock(s_valuesMutex);
    auto it = s_values.find(ValueKey(guid, type));
    if (it == s_values.end() || (it->second.expiresAt && time(nullptr) >= it->second.expiresAt))
        return {};
    return it->second.data;
}

void RandomBotFacade::SetValue(uint32 guid, std::string type, uint32 value, std::string data, int32 validIn)
{
    std::lock_guard<std::mutex> lock(s_valuesMutex);
    time_t expiresAt = validIn > 0 ? time(nullptr) + validIn : 0;
    s_values[ValueKey(guid, type)] = StoredValue{value, std::move(data), validIn, expiresAt};
}

void RandomBotFacade::SetValue(Player* bot, std::string type, uint32 value, std::string data, int32 validIn)
{
    if (bot)
        SetValue(bot->GetObjectGuid().GetCounter(), std::move(type), value, std::move(data), validIn);
}

double RandomBotFacade::GetBuyMultiplier(Player* bot)
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

double RandomBotFacade::GetSellMultiplier(Player* bot)
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

uint32 RandomBotFacade::GetTradeDiscount(Player* bot, Player* master)
{
    if (!bot || !master)
        return 0;

    std::lock_guard<std::mutex> lock(s_valuesMutex);
    auto it = s_tradeDiscounts.find(TradeKey(bot->GetObjectGuid(), master->GetObjectGuid()));
    return it == s_tradeDiscounts.end() ? 0 : it->second;
}

void RandomBotFacade::SetTradeDiscount(Player* bot, Player* master, uint32 value)
{
    if (!bot || !master)
        return;

    std::lock_guard<std::mutex> lock(s_valuesMutex);
    s_tradeDiscounts[TradeKey(bot->GetObjectGuid(), master->GetObjectGuid())] = value;
}

void RandomBotFacade::AddTradeDiscount(Player* bot, Player* master, int32 value)
{
    uint32 current = GetTradeDiscount(bot, master);
    SetTradeDiscount(bot, master, value < 0 && current < static_cast<uint32>(-value)
        ? 0
        : static_cast<uint32>(static_cast<int64>(current) + value));
}

void RandomBotFacade::Remove(Player* bot)
{
    if (bot)
        TortoiseBots::BotManager::Instance().RemoveBot(bot->GetObjectGuid(), true);
}

void RandomBotFacade::Refresh(Player* bot)
{
    if (!bot || !IsRandomBot(bot))
        return;

    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.Refresh();
}

void RandomBotFacade::UpdateGearSpells(Player* bot)
{
    if (!bot || !IsRandomBot(bot))
        return;

    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.UpgradeGearBest();
}

bool RandomBotFacade::ProcessBot(Player* player)
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

bool RandomBotFacade::GetNamedLocation(std::string const& name, WorldLocation& location)
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

void RandomBotFacade::LoadBattleMastersCache()
{
    battleMastersCache.clear();
    auto result = WorldDatabase.Query("SELECT entry, bg_template FROM battlemaster_entry");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();
        uint32 entry = fields[0].GetUInt32();
        uint32 bgTemplate = fields[1].GetUInt32();
        if (entry && sObjectMgr.GetCreatureTemplate(entry))
            battleMastersCache[TEAM_BOTH_ALLOWED][BattleGroundTypeId(bgTemplate)].push_back(entry);
    } while (result->NextRow());
}

void RandomBotFacade::LoadAuctionPrices()
{
    std::lock_guard<std::mutex> lock(m_ahActionMutex);
    ahMirror.clear();

    std::vector<AuctionHouseObject*> visited;
    for (uint32 i = 0; i < sAuctionHouseStore.GetNumRows(); ++i)
    {
        AuctionHouseEntry const* houseEntry = sAuctionHouseStore.LookupEntry(i);
        if (!houseEntry)
            continue;

        AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(houseEntry);
        if (!auctionHouse || std::find(visited.begin(), visited.end(), auctionHouse) != visited.end())
            continue;
        visited.push_back(auctionHouse);

        AuctionHouseObject::Guard guard(auctionHouse->GetLock());
        auto bounds = auctionHouse->GetAuctionsBounds_locked();
        for (auto it = bounds.first; it != bounds.second; ++it)
        {
            if (!it->second || !it->second->buyout || !it->second->itemCount)
                continue;
            ahMirror[it->second->itemTemplate].push_back(*it->second);
        }
    }
}

const std::vector<AuctionEntry>& RandomBotFacade::GetAhPrices(uint32 itemId) const
{
    static const std::vector<AuctionEntry> empty;
    auto it = ahMirror.find(itemId);
    return it == ahMirror.end() ? empty : it->second;
}

InventoryResult RandomBotFacade::CanEquipUnseenItem(Player* player, uint8 slot, uint16& dest, uint32 item)
{
    if (!player)
        return EQUIP_ERR_ITEM_NOT_FOUND;

    ItemPrototype const* prototype = sObjectMgr.GetItemPrototype(item);
    return prototype ? player->CanEquipItem(slot, dest, prototype, nullptr, false) : EQUIP_ERR_ITEM_NOT_FOUND;
}

bool RandomBotFacade::IsPinnedBot(uint32 guidLow)
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

void RandomBotFacade::ChangeStrategy(Player* player)
{
    if (!player)
        return;

    if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(player))
    {
        ai->ChangeStrategy(sPlayerbotAIConfig.randomBotCombatStrategies, BotState::BOT_STATE_COMBAT);
        ai->ChangeStrategy(sPlayerbotAIConfig.randomBotNonCombatStrategies, BotState::BOT_STATE_NON_COMBAT);
    }
}

void RandomBotFacade::Revive(Player* player)
{
    if (player && player->IsInWorld() && !player->IsAlive())
        player->RepopAtGraveyard();
}

void RandomBotFacade::PrintTeleportCache()
{
    auto locations = WorldDatabase.Query("SELECT COUNT(*) FROM ai_playerbot_named_location");
    uint32 namedLocations = locations ? locations->Fetch()[0].GetUInt32() : 0;
    sLog.outString("TortoiseBots: native travel points; named-location rows: %u", namedLocations);
}
