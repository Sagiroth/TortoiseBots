#include "GuildOrderAccess.h"

#include "GuildOrderAccessPolicy.h"
#include "AltBotService.h"
#include "BotManager.h"
#include "ClaimLifecycle.h"
#include "HireLifecycle.h"
#include "RandomBotAccountRegistry.h"
#include "../commands/CommandShared.h"
#include "../host/ModuleLog.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "PlayerbotAIStorage.h"
#include "Database/DatabaseEnv.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Guild/Guild.h"
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
#include <sstream>
#include <unordered_map>

namespace TortoiseBots
{
namespace GuildOrders
{
namespace
{

// Stored bot-rank choices, guild id -> rank. Loaded once at startup; the
// rank command writes through it. Guilds without a row use the default.
class BotRankStore
{
public:
    static BotRankStore& Instance()
    {
        static BotRankStore instance;
        return instance;
    }

    void Initialize()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_initialized)
            return;
        m_ranks.clear();
        std::unique_ptr<QueryResult> result(CharacterDatabase.Query(
            "SELECT `guild_id`, `bot_rank` FROM `tortoise_bots_guild_bot_rank`"));
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                uint32_t guildId = fields[0].GetUInt32();
                uint32_t rank = fields[1].GetUInt32();
                if (guildId)
                    m_ranks[guildId] = rank;
            } while (result->NextRow());
        }
        m_initialized = true;
    }

    std::optional<uint32_t> Stored(uint32_t guildId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_ranks.find(guildId);
        if (it == m_ranks.end())
            return std::nullopt;
        return it->second;
    }

    bool Store(uint32_t guildId, uint32_t rank)
    {
        if (!guildId)
            return false;
        if (!CharacterDatabase.DirectPExecute(
                "REPLACE INTO `tortoise_bots_guild_bot_rank` (`guild_id`, `bot_rank`, `updated_at`) "
                "VALUES ('%u', '%u', FROM_UNIXTIME('%u'))",
                guildId, rank, static_cast<uint32_t>(std::time(nullptr))))
        {
            sLog.outError("TortoiseBots: failed to persist bot rank %u for guild %u", rank, guildId);
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_ranks[guildId] = rank;
        return true;
    }

    void Forget(uint32_t guildId)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_ranks.erase(guildId);
        }
        CharacterDatabase.DirectPExecute(
            "DELETE FROM `tortoise_bots_guild_bot_rank` WHERE `guild_id` = '%u'", guildId);
    }

private:
    std::mutex m_mutex;
    std::unordered_map<uint32_t, uint32_t> m_ranks;
    bool m_initialized = false;
};

// A guild member the rank rule may touch: a module-owned bot character and
// never a real player's own character. Online, the live session decides
// (Headless without a network transport, like the order giver check);
// offline, the durable marks decide the same way `.bot order` does: claimed
// into a guild or living on a random-pool account. Owned characters of a
// real player's account and autonomous alt-bots are players' own and stay
// out, even when they run on a bot session.
bool IsRankableBot(Player* bot, ObjectGuid guid)
{
    uint32_t guidLow = guid.GetCounter();
    if (AltBotService::Instance().IsAutonomous(guidLow))
        return false;
    if (bot)
    {
        if (BotManager::Instance().IsBot(guid))
            return true;
        WorldSession* session = bot->GetSession();
        if (!session || session->HasNetworkTransport())
            return false;
        return false;
    }
    if (ClaimLifecycle::Instance().IsClaimed(guidLow))
        return true;
    uint32_t accountId = sObjectMgr.GetPlayerAccountIdByGUID(guid);
    return accountId && RandomBotAccountRegistry::Instance().IsRegistered(accountId);
}

// Minimum ranks of the unlocked bank tabs, 1-based tab order, from the
// table the core loads its own bank state from. No new core seam: the
// module reads the same rows the bank shows the client.
std::vector<uint32_t> BankTabMinRanks(uint32_t guildId)
{
    std::vector<uint32_t> minRanks;
    if (!guildId)
        return minRanks;
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
        "SELECT `tabs`, `minrank1`, `minrank2`, `minrank3`, `minrank4`, `minrank5` "
        "FROM `guild_bank_tabs` WHERE `guildid` = '%u' AND `isInferno` = '0'",
        guildId));
    if (!result)
        return minRanks;
    Field* fields = result->Fetch();
    uint32_t tabs = fields[0].GetUInt32();
    for (uint32_t tab = 1; tab <= tabs && tab <= 5; ++tab)
        minRanks.push_back(fields[tab].GetUInt32());
    return minRanks;
}

void MoveMemberToRank(Guild* guild, ObjectGuid guid, uint32_t rank)
{
    if (!guild)
        return;
    MemberSlot* slot = guild->GetMemberSlot(guid);
    if (!slot || slot->RankId == rank)
        return;
    slot->ChangeRank(rank);
}

} // namespace

std::optional<uint32_t> StoredBotRank(uint32_t guildId)
{
    return BotRankStore::Instance().Stored(guildId);
}

bool StoreBotRank(uint32_t guildId, uint32_t rank)
{
    return BotRankStore::Instance().Store(guildId, rank);
}

uint32_t WantedBotRank(uint32_t guildId, uint32_t rankCount)
{
    if (!sPlayerbotAIConfig.guildOrderBotRank)
        return 0;
    std::optional<uint32_t> stored = StoredBotRank(guildId);
    if (!stored)
        return DefaultBotRank(rankCount);
    return ClampBotRank(*stored, rankCount);
}

std::optional<uint32_t> JoinBotRank(uint32_t guildId, uint32_t botGuidLow)
{
    if (!botGuidLow)
        return std::nullopt;
    Guild* guild = sGuildMgr.GetGuildById(guildId);
    if (!guild)
        return std::nullopt;
    uint32_t rank = WantedBotRank(guildId, guild->GetRanksSize());
    if (!rank)
        return std::nullopt;
    ObjectGuid guid(HIGHGUID_PLAYER, botGuidLow);
    Player* bot = sObjectAccessor.FindPlayer(guid);
    if (!IsRankableBot(bot, guid))
        return std::nullopt;
    return rank;
}

void EnsureBotRank(uint32_t guildId, uint32_t botGuidLow)
{
    if (!botGuidLow)
        return;
    Guild* guild = sGuildMgr.GetGuildById(guildId);
    if (!guild)
        return;
    uint32_t rank = WantedBotRank(guildId, guild->GetRanksSize());
    if (!rank)
        return;
    ObjectGuid guid(HIGHGUID_PLAYER, botGuidLow);
    Player* bot = sObjectAccessor.FindPlayer(guid);
    if (!IsRankableBot(bot, guid))
        return;
    MoveMemberToRank(guild, guid, rank);
}

void ApplyBotRankToGuild(uint32_t guildId)
{
    Guild* guild = sGuildMgr.GetGuildById(guildId);
    if (!guild)
        return;
    uint32_t rank = WantedBotRank(guildId, guild->GetRanksSize());
    if (!rank)
        return;
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
        "SELECT `guid` FROM `guild_member` WHERE `guildid` = '%u'", guildId));
    if (!result)
        return;
    do
    {
        uint32_t guidLow = result->Fetch()[0].GetUInt32();
        if (!guidLow)
            continue;
        ObjectGuid guid(HIGHGUID_PLAYER, guidLow);
        Player* bot = sObjectAccessor.FindPlayer(guid);
        if (!IsRankableBot(bot, guid))
            continue;
        MoveMemberToRank(guild, guid, rank);
    } while (result->NextRow());
}

void ApplyBotRanksAtStartup()
{
    BotRankStore::Instance().Initialize();
    if (!sPlayerbotAIConfig.guildOrderBotRank)
        return;
    std::unique_ptr<QueryResult> result(CharacterDatabase.Query(
        "SELECT `guildid`, `guid` FROM `guild_member`"));
    if (!result)
        return;
    uint32_t lastGuildId = 0;
    Guild* guild = nullptr;
    uint32_t rank = 0;
    do
    {
        Field* fields = result->Fetch();
        uint32_t guildId = fields[0].GetUInt32();
        uint32_t guidLow = fields[1].GetUInt32();
        if (!guildId || !guidLow)
            continue;
        if (guildId != lastGuildId)
        {
            lastGuildId = guildId;
            guild = sGuildMgr.GetGuildById(guildId);
            rank = guild ? WantedBotRank(guildId, guild->GetRanksSize()) : 0;
        }
        if (!guild || !rank)
            continue;
        ObjectGuid guid(HIGHGUID_PLAYER, guidLow);
        Player* bot = sObjectAccessor.FindPlayer(guid);
        if (!IsRankableBot(bot, guid))
            continue;
        MoveMemberToRank(guild, guid, rank);
    } while (result->NextRow());
}

void OnBotRankDisband(uint32_t guildId)
{
    BotRankStore::Instance().Forget(guildId);
}

std::string FormatAccessLine(uint32_t guildId, uint32_t askerGuidLow)
{
    Guild* guild = sGuildMgr.GetGuildById(guildId);
    if (!guild)
        return "TBM:ORDER_ACCESS|-|-|-|-|0|0";
    uint32_t rankCount = guild->GetRanksSize();
    uint32_t botRank = WantedBotRank(guildId, rankCount);
    std::string rankName = botRank < rankCount ? guild->GetRankName(botRank) : "-";

    std::vector<uint32_t> tabsOk, tabsDenied;
    SplitBankTabs(botRank, BankTabMinRanks(guildId), tabsOk, tabsDenied);
    bool goldOk = BotRankMayWithdrawGold(botRank);

    ObjectGuid asker(HIGHGUID_PLAYER, askerGuidLow);
    Player* askerPlayer = sObjectAccessor.FindPlayer(asker);
    int32_t askerRank = -1;
    if (askerPlayer && askerPlayer->GetGuildId() == guildId)
        askerRank = static_cast<int32_t>(askerPlayer->GetRank());
    else if (MemberSlot* slot = guild->GetMemberSlot(asker))
        askerRank = static_cast<int32_t>(slot->RankId);
    bool canSetRank = askerRank >= 0 &&
        guild->HasRankRight(static_cast<uint32_t>(askerRank), GR_RIGHT_PROMOTE) &&
        guild->HasRankRight(static_cast<uint32_t>(askerRank), GR_RIGHT_DEMOTE);

    std::ostringstream out;
    out << "TBM:ORDER_ACCESS|" << botRank << "|" << BotCommands::ProtocolSafe(rankName) << "|"
        << FormatTabList(tabsOk) << "|" << FormatTabList(tabsDenied) << "|"
        << (goldOk ? "1" : "0") << "|" << (canSetRank ? "1" : "0");
    return out.str();
}

} // namespace GuildOrders
} // namespace TortoiseBots
