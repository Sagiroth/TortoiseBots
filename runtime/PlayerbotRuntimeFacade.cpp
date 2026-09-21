// Small adapters for mature Vanilla/Tortoise strategy code.
//
// These functions translate behavior-facing queries to the native owners. They
// do not own sessions, players, AI instances, or random-bot population.

#include "../ai/playerbot/RandomBotFacade.h"
#include "runtime/BotActivityLease.h"

#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/PlayerbotFactory.h"
#include "../ai/playerbot/BotState.h"
#include "../ai/playerbot/TravelMgr.h"
#include "../runtime/BotManager.h"
#include "../runtime/PlayerbotAIStorage.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "AuctionHouse/AuctionHouseMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Database/DBCStores.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "World.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Item.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Objects/Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Database/DatabaseEnv.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Timer.h"
#include "../host/ModuleLog.h"

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
    // Phase 1: only rebuild the compatibility map when membership changed
    // (dirty flag) or every 5s as a self-healing fallback. The per-tick
    // rebuild did GetAllBots + FindPlayer per bot plus a full map clear on
    // every world tick with zero membership change in the common case.
    constexpr uint32 kResyncIntervalMs = 5000;
    uint32 nowMs = WorldTimer::getMSTime();
    bool fallbackDue = (nowMs - m_nativePlayersLastSyncMs) >= kResyncIntervalMs;
    if (!m_nativePlayersDirty && !fallbackDue)
        return;
    m_nativePlayersDirty = false;
    m_nativePlayersLastSyncMs = nowMs;
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
    if (!bot || !IsRandomBot(bot) || !PlayerbotAIStorage::Instance().GetAI(bot))
        return;

    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.MakeComplete();
}

bool RandomBotFacade::ProcessBot(Player* player)
{
    if (!player || !IsRandomBot(player) || !player->IsInWorld() || player->IsBeingTeleported())
        return false;
    // A dungeon crew (another module holds the Dungeon activity lease) is left alone:
    // no graveyard repop, no expired-value sweep. This runs from the bot's own AI
    // ("random bot update") as well as from the service loop, so the guard sits here.
    if (TortoiseBots::BotActivityLeaseManager::Instance().GetActivity(player->GetGUIDLow()) == TortoiseBots::BotActivity::Dungeon)
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

    // Iterate all DBC auction house entries, deduplicating by object pointer
    // (cross-faction mode collapses all entries to one object).
    std::vector<AuctionHouseObject*> visited;
    for (uint32 i = 0; i < sAuctionHouseStore.GetNumRows(); ++i)
    {
        AuctionHouseEntry const* houseEntry = sAuctionHouseStore.LookupEntry(i);
        if (!houseEntry)
            continue;

        AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(houseEntry);
        if (!auctionHouse)
            continue;
        if (std::find(visited.begin(), visited.end(), auctionHouse) != visited.end())
            continue;
        visited.push_back(auctionHouse);

        AuctionHouseObject::AuctionEntryMap const* auctions = auctionHouse->GetAuctions();
        if (!auctions)
            continue;

        for (auto const& pair : *auctions)
        {
            AuctionEntry const* entry = pair.second;
            if (!entry)
                continue;

            // Only consider buyout listings for unit-price appraisal
            if (!entry->buyout)
                continue;

            Item const* item = sAuctionMgr.GetAItem(entry->itemGuidLow);
            if (!item || !item->GetCount())
                continue;

            // Bounded per-item listings: keep up to 64 lowest-unit-price entries
            // per item template so memory and appraisal sorting stay bounded.
            constexpr size_t kMaxAuctionsPerItem = 64;
            auto& listings = ahMirror[entry->itemTemplate];
            if (listings.size() < kMaxAuctionsPerItem)
            {
                listings.push_back(*entry);
            }
            else
            {
                float currentUnitPrice = float(entry->buyout) / float(item->GetCount());
                size_t maxIdx = 0;
                float maxUnitPrice = 0.0f;
                for (size_t idx = 0; idx < listings.size(); ++idx)
                {
                    Item const* existingItem = sAuctionMgr.GetAItem(listings[idx].itemGuidLow);
                    uint32 existingCount = existingItem ? existingItem->GetCount() : 1;
                    float existingUnitPrice = float(listings[idx].buyout) / float(existingCount);
                    if (existingUnitPrice > maxUnitPrice)
                    {
                        maxUnitPrice = existingUnitPrice;
                        maxIdx = idx;
                    }
                }
                if (currentUnitPrice < maxUnitPrice)
                {
                    listings[maxIdx] = *entry;
                }
            }
        }
    }
}

void RandomBotFacade::RefreshAuctionPrices(uint32 diff)
{
    static uint32 elapsed = 0;
    elapsed += diff;
    uint32 intervalMs = sPlayerbotAIConfig.auctionPriceRefreshInterval * 1000;
    if (intervalMs < 5000)
        intervalMs = 5000;
    if (elapsed >= intervalMs)
    {
        elapsed = 0;
        LoadAuctionPrices();
    }
}

const std::vector<AuctionEntry>& RandomBotFacade::GetAhPrices(uint32 itemId) const
{
    static const std::vector<AuctionEntry> empty;
    auto it = ahMirror.find(itemId);
    return it == ahMirror.end() ? empty : it->second;
}

std::vector<AuctionEntry> RandomBotFacade::GetAhPrices(uint32 itemId, uint32 houseFaction) const
{
    std::vector<AuctionEntry> result;
    auto it = ahMirror.find(itemId);
    if (it == ahMirror.end())
        return result;

    bool twoSide = sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_AUCTION);
    for (auto const& entry : it->second)
    {
        if (twoSide)
        {
            result.push_back(entry);
            continue;
        }

        uint32 team = entry.auctionHouseEntry ? AuctionHouseMgr::GetAuctionHouseTeam(entry.auctionHouseEntry) : 0;
        // team == 0 is neutral AH (accessible to all factions)
        if (team == 0 || team == houseFaction || houseFaction == 0)
        {
            result.push_back(entry);
        }
    }

    return result;
}

std::vector<AuctionEntry> RandomBotFacade::GetAhPrices(uint32 itemId, Player* bot) const
{
    if (!bot)
        return GetAhPrices(itemId, (uint32)0);

    return GetAhPrices(itemId, bot->GetTeam());
}

InventoryResult RandomBotFacade::CanEquipUnseenItem(Player* player, uint8 slot, uint16& dest, uint32 item)
{
    if (!player)
        return EQUIP_ERR_ITEM_NOT_FOUND;

    ItemPrototype const* prototype = sObjectMgr.GetItemPrototype(item);
    // swap=true: the caller compares old-vs-new itself and replaces in place; with
    // swap=false CanEquipItem rejects an occupied slot (e.g. starter MH weapon)
    // and every upgrade candidate dies there.
    return prototype ? player->CanEquipItem(slot, dest, prototype, nullptr, true) : EQUIP_ERR_ITEM_NOT_FOUND;
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
    TB_LOG_BASIC("TortoiseBots: native travel points; named-location rows: %u", namedLocations);
}
