#include "GuildOrderAccess.h"

#include "GuildOrderAccessPolicy.h"
#include "AltBotService.h"
#include "ClaimLifecycle.h"
#include "RandomBotAccountRegistry.h"
#include "../commands/CommandShared.h"
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

    // Whether bots in the guild must not manage it right now (cached: no
    // database read on the AI tick). Missing or false means either not
    // blocked or not yet computed; MarkManagementStale forces a recompute
    // on the next query.
    void SetManagementBlocked(uint32_t guildId, bool blocked)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_managementBlocked[guildId] = blocked;
    }

    void MarkManagementStale(uint32_t guildId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_managementBlocked.erase(guildId);
    }

    std::optional<bool> ManagementBlocked(uint32_t guildId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_managementBlocked.find(guildId);
        if (it == m_managementBlocked.end())
            return std::nullopt;
        return it->second;
    }

private:
    std::mutex m_mutex;
    std::unordered_map<uint32_t, uint32_t> m_ranks;
    std::unordered_map<uint32_t, bool> m_managementBlocked;
    bool m_initialized = false;
};
// A guild member the rank rule may touch. The same durable rule online and
// offline: claimed into a guild or living on a random-pool account. The
// account lookup hits the online player or the cached player data, never
// the database. Autonomous alt-bots and any other character on a real
// player's account stay out, even when they run on a bot session.
bool IsRankableBot(ObjectGuid guid)
{
    uint32_t guidLow = guid.GetCounter();
    if (!guidLow)
        return false;
    if (AltBotService::Instance().IsAutonomous(guidLow))
        return false;
    if (ClaimLifecycle::Instance().IsClaimed(guidLow))
        return true;
    uint32_t accountId = sObjectMgr.GetPlayerAccountIdByGUID(guid);
    return accountId && RandomBotAccountRegistry::Instance().IsRegistered(accountId);
}

// True when at least one member of the guild is a real player's character:
// anyone the durable rule does not count as a bot. All-bot pool guilds
// have none, so the rank rule leaves them alone.
bool GuildHasRealPlayerMember(Guild* guild)
{
    if (!guild)
        return false;
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
        "SELECT `guid` FROM `guild_member` WHERE `guildid` = '%u'", guild->GetId()));
    if (!result)
        return false;
    do
    {
        uint32_t guidLow = result->Fetch()[0].GetUInt32();
        if (guidLow && !IsRankableBot(ObjectGuid(HIGHGUID_PLAYER, guidLow)))
            return true;
    } while (result->NextRow());
    return false;
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
    if (!slot || slot->RankId == rank || !BotRankMayMoveMember(slot->RankId))
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
    bool stored = BotRankStore::Instance().Store(guildId, rank);
    if (stored)
        BotRankStore::Instance().MarkManagementStale(guildId);
    return stored;
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
    // Membership is about to change (the core inserts the row right after
    // this hook): drop the cached management verdict so the next query
    // recomputes it from the new roster.
    BotRankStore::Instance().MarkManagementStale(guildId);
    uint32_t rank = WantedBotRank(guildId, guild->GetRanksSize());
    ObjectGuid guid(HIGHGUID_PLAYER, botGuidLow);
    if (!BotRankAppliesInGuild(GuildHasRealPlayerMember(guild), rank) || !IsRankableBot(guid))
        return std::nullopt;
    return rank;
}

void ApplyBotRankToGuild(uint32_t guildId)
{
    Guild* guild = sGuildMgr.GetGuildById(guildId);
    if (!guild)
        return;
    uint32_t rank = WantedBotRank(guildId, guild->GetRanksSize());
    if (!BotRankAppliesInGuild(GuildHasRealPlayerMember(guild), rank))
        return;
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
        "SELECT `guid` FROM `guild_member` WHERE `guildid` = '%u'", guildId));
    if (!result)
        return;
    do
    {
        ObjectGuid guid(HIGHGUID_PLAYER, result->Fetch()[0].GetUInt32());
        if (!IsRankableBot(guid))
            continue;
        MoveMemberToRank(guild, guid, rank);
    } while (result->NextRow());
}

void ApplyBotRanksAtStartup()
{
    BotRankStore::Instance().Initialize();
    if (!sPlayerbotAIConfig.guildOrderBotRank)
        return;
    // One pass over the whole member table, grouped per guild in memory:
    // a guild is swept only when it holds a real player member.
    std::unique_ptr<QueryResult> result(CharacterDatabase.Query(
        "SELECT `guildid`, `guid` FROM `guild_member` ORDER BY `guildid`"));
    if (!result)
        return;
    std::unordered_map<uint32_t, std::vector<uint32_t>> membersByGuild;
    do
    {
        Field* fields = result->Fetch();
        if (fields[0].GetUInt32() && fields[1].GetUInt32())
            membersByGuild[fields[0].GetUInt32()].push_back(fields[1].GetUInt32());
    } while (result->NextRow());
    for (auto const& [guildId, members] : membersByGuild)
    {
        Guild* guild = sGuildMgr.GetGuildById(guildId);
        if (!guild)
            continue;
        uint32_t rank = WantedBotRank(guildId, guild->GetRanksSize());
        bool realPlayer = false;
        for (uint32_t guidLow : members)
        {
            if (!IsRankableBot(ObjectGuid(HIGHGUID_PLAYER, guidLow)))
            {
                realPlayer = true;
                break;
            }
        }
        if (!BotRankAppliesInGuild(realPlayer, rank))
        {
            BotRankStore::Instance().SetManagementBlocked(guildId, false);
            continue;
        }
        BotRankStore::Instance().SetManagementBlocked(guildId, true);
        for (uint32_t guidLow : members)
        {
            ObjectGuid guid(HIGHGUID_PLAYER, guidLow);
            if (!IsRankableBot(guid))
                continue;
            MoveMemberToRank(guild, guid, rank);
        }
    }
}

void OnBotRankDisband(uint32_t guildId)
{
    BotRankStore::Instance().Forget(guildId);
    BotRankStore::Instance().MarkManagementStale(guildId);
}

void MarkBotRankGuildStale(uint32_t guildId)
{
    BotRankStore::Instance().MarkManagementStale(guildId);
}

bool BotRankBlocksManagement(uint32_t guildId)
{
    if (!guildId)
        return false;
    if (std::optional<bool> cached = BotRankStore::Instance().ManagementBlocked(guildId))
        return *cached;
    Guild* guild = sGuildMgr.GetGuildById(guildId);
    bool blocked = guild && BotRankAppliesInGuild(GuildHasRealPlayerMember(guild),
        WantedBotRank(guildId, guild->GetRanksSize()));
    BotRankStore::Instance().SetManagementBlocked(guildId, blocked);
    return blocked;
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
