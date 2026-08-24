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
#include "../runtime/PlayerbotAIStorage.h"
#include "BotManager.h"

#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Objects/Player.h"
#include "Log.h"
#include "Database/DatabaseEnv.h"

#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

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

void ReportUnsupportedRandomFeature(char const* feature)
{
    static std::mutex reportMutex;
    static std::set<std::string> reported;
    std::lock_guard<std::mutex> lock(reportMutex);
    if (reported.insert(feature).second)
        sLog.outInfo("TortoiseBots: legacy random-bot feature '%s' is not available under native BotManager ownership", feature);
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
    ReportUnsupportedRandomFeature("PlayerbotHolder::UpdateSessions");
}

void PlayerbotHolder::LogoutAllBots()
{
    for (Player* player : TortoiseBots::BotManager::Instance().GetAllBots())
        if (player)
            TortoiseBots::BotManager::Instance().RemoveBot(player->GetObjectGuid(), true);
}

void PlayerbotHolder::JoinChatChannels(Player* /*bot*/)
{
    ReportUnsupportedRandomFeature("PlayerbotHolder::JoinChatChannels");
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

void RandomPlayerbotMgr::UpdateAIInternal(uint32 /*elapsed*/, bool /*minimal*/)
{
    ReportUnsupportedRandomFeature("RandomPlayerbotMgr::UpdateAIInternal");
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

void RandomPlayerbotMgr::LoadNamedLocations()
{
    // The native TravelMgr reads the same table on demand. Keep this donor
    // lifecycle hook intentionally quiet instead of claiming that named
    // locations are unavailable.
}

void RandomPlayerbotMgr::LoadBattleMastersCache()
{
    // Vanilla battleground entries are loaded by the core BattleGroundMgr;
    // the donor random manager's separate cache is not needed by the native
    // action path.
}

InventoryResult RandomPlayerbotMgr::CanEquipUnseenItem(Player* player, uint8 slot, uint16& dest, uint32 item)
{
    if (!player)
        return EQUIP_ERR_ITEM_NOT_FOUND;

    ItemPrototype const* prototype = sObjectMgr.GetItemPrototype(item);
    return prototype ? player->CanEquipItem(slot, dest, prototype, nullptr, false) : EQUIP_ERR_ITEM_NOT_FOUND;
}

const CreatureDataPair* RandomPlayerbotMgr::GetCreatureDataByEntry(uint32 /*entry*/)
{
    ReportUnsupportedRandomFeature("RandomPlayerbotMgr::GetCreatureDataByEntry");
    return nullptr;
}

uint32 RandomPlayerbotMgr::GetCreatureGuidByEntry(uint32 /*entry*/)
{
    ReportUnsupportedRandomFeature("RandomPlayerbotMgr::GetCreatureGuidByEntry");
    return 0;
}

uint32 RandomPlayerbotMgr::GetBattleMasterEntry(Player* /*bot*/, BattleGroundTypeId /*bgTypeId*/, bool /*fake*/)
{
    ReportUnsupportedRandomFeature("RandomPlayerbotMgr::GetBattleMasterEntry");
    return 0;
}

bool RandomPlayerbotMgr::IsPinnedBot(uint32 /*guidLow*/)
{
    ReportUnsupportedRandomFeature("RandomPlayerbotMgr::IsPinnedBot");
    return false;
}

bool RandomPlayerbotMgr::AddRandomBot(uint32 guid)
{
    ObjectGuid const objectGuid(HIGHGUID_PLAYER, guid);
    uint32 const accountId = sObjectMgr.GetPlayerAccountIdByGUID(objectGuid);
    return accountId && TortoiseBots::BotManager::Instance().AddRandomBot(accountId, objectGuid);
}

void RandomPlayerbotMgr::RandomTeleportForLevel(Player* /*bot*/, bool /*activeOnly*/)
{
    ReportUnsupportedRandomFeature("RandomPlayerbotMgr::RandomTeleportForLevel");
}

void RandomPlayerbotMgr::RandomTeleportForRpg(Player* /*bot*/, bool /*activeOnly*/)
{
    ReportUnsupportedRandomFeature("RandomPlayerbotMgr::RandomTeleportForRpg");
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
    ReportUnsupportedRandomFeature("RandomPlayerbotMgr::PrintTeleportCache");
}

void RandomPlayerbotMgr::PrintStats(uint32 /*requesterGuid*/)
{
    ReportUnsupportedRandomFeature("RandomPlayerbotMgr::PrintStats");
}
