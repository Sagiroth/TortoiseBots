#include "RandomBotPoolReset.h"

#include "RandomBotAccountRegistry.h"
#include "BotManager.h"
#include "BotActivityLease.h"
#include "CharacterCleanup.h"
#include "HireLifecycle.h"
#include "../host/BotSessionAdapter.h"
#include "../host/ModuleLog.h"

#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "World.h"
#include "WorldSession.h"
#include "Log.h"
#include "Database/DatabaseEnv.h"
#include "Database/DBCStores.h"
#include "Timer.h"

#include <algorithm>
#include <memory>
#include <sstream>

namespace TortoiseBots
{

namespace
{
// The managed random-bot pool. One row in tortoise_bots_pool_state.
constexpr char kPoolName[] = "random";

// A pool session that refuses to close must not turn into "delete anyway": the
// reset stops and reports instead, and the generation stays unapplied.
constexpr uint32 kSessionDrainTimeoutMs = 120000;
// Same idea for a single character that keeps being busy during deletion.
constexpr uint32 kDeleteStallTimeoutMs = 120000;
constexpr uint32 kProgressLogIntervalMs = 10000;
// Verification retries within this window, because the core's character
// deletion is committed through the character database's async queue.
constexpr uint32 kVerifyTimeoutMs = 15000;
constexpr size_t kVerifyChunkSize = 200;

std::string SqlIdList(std::vector<uint32> const& ids)
{
    std::string list;
    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (i)
            list += ",";
        list += std::to_string(ids[i]);
    }
    return list;
}

} // namespace

RandomBotPoolReset& RandomBotPoolReset::Instance()
{
    static RandomBotPoolReset instance;
    return instance;
}

bool RandomBotPoolReset::IsActive() const
{
    switch (m_phase)
    {
        case PoolResetPhase::Planning:
        case PoolResetPhase::SettlingAuctions:
        case PoolResetPhase::Deleting:
        case PoolResetPhase::Verifying:
        case PoolResetPhase::Rebuilding:
            return true;
        default:
            return false;
    }
}

char const* RandomBotPoolReset::PhaseName() const
{
    switch (m_phase)
    {
        case PoolResetPhase::Disabled:   return "disabled";
        case PoolResetPhase::Planning:   return "planning";
        case PoolResetPhase::SettlingAuctions: return "settling auctions";
        case PoolResetPhase::Deleting:   return "deleting";
        case PoolResetPhase::Verifying:  return "verifying";
        case PoolResetPhase::Rebuilding: return "rebuilding";
        case PoolResetPhase::Complete:   return "complete";
        case PoolResetPhase::Failed:     return "failed";
        default:                         return "unknown";
    }
}

void RandomBotPoolReset::Fail(std::string reason)
{
    m_lastFailure = std::move(reason);
    m_phase = PoolResetPhase::Failed;
    sLog.outError("TortoiseBots: random pool reset failed: %s", m_lastFailure.c_str());
    if (m_maintenance)
        sLog.outError("TortoiseBots: random pool consumers stay paused; the generation was NOT recorded, "
            "so the next server start resumes the reset");
}

bool RandomBotPoolReset::LoadAppliedGeneration(std::string& out, std::string& error) const
{
    uint32 rows = 0;
    std::unique_ptr<QueryResult> count(CharacterDatabase.PQuery(
        "SELECT COUNT(*) FROM `tortoise_bots_pool_state` WHERE `pool_name` = '%s'", kPoolName));
    if (!count)
    {
        error = "pool reset state table could not be read";
        return false;
    }
    rows = count->Fetch()[0].GetUInt32();
    out.clear();
    if (!rows)
        return true;

    std::unique_ptr<QueryResult> state(CharacterDatabase.PQuery(
        "SELECT `applied_generation` FROM `tortoise_bots_pool_state` WHERE `pool_name` = '%s'", kPoolName));
    if (!state)
    {
        error = "pool reset state row could not be read";
        return false;
    }
    out = state->Fetch()[0].GetCppString();
    return true;
}

bool RandomBotPoolReset::SnapshotTargets(std::string& error)
{
    m_targets.clear();
    if (!m_managedAccounts)
        return true;

    // The account scope comes from the registry, exactly like reporting and
    // verification, so the three can never disagree about what is managed.
    std::string accounts = RandomBotAccountRegistry::Instance().AccountIdList();
    std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
        "SELECT `guid`, `account`, `name` FROM `characters` "
        "WHERE `deleteDate` IS NULL AND `account` IN (%s) ORDER BY `account`, `guid`", accounts.c_str()));
    if (!rows)
    {
        uint32 remaining = 0;
        if (!RandomBotAccountRegistry::Instance().CountManagedCharacters(remaining, error))
            return false;
        if (remaining)
        {
            error = "managed characters could not be listed";
            return false;
        }
        return true;
    }

    do
    {
        Field* fields = rows->Fetch();
        Target target;
        target.guidLow = fields[0].GetUInt32();
        target.accountId = fields[1].GetUInt32();
        target.name = fields[2].GetCppString();
        if (!target.guidLow || !target.accountId)
        {
            error = "a managed character row is invalid (missing guid or account)";
            return false;
        }
        m_targets.push_back(std::move(target));
    } while (rows->NextRow());

    return true;
}

bool RandomBotPoolReset::PreflightHumanSessions(std::string& error) const
{
    for (Target const& target : m_targets)
    {
        ObjectGuid guid(HIGHGUID_PLAYER, target.guidLow);
        Player* player = sObjectAccessor.FindPlayer(guid);
        if (!player)
            continue;
        WorldSession* session = player->GetSession();
        if (session && !session->IsHeadless())
        {
            error = "pool character " + target.name + " (" + std::to_string(target.guidLow) +
                ") is being played by a network session; this feature never logs out or deletes human sessions";
            return false;
        }
    }
    return true;
}

bool RandomBotPoolReset::PreflightGuilds(std::string& error) const
{
    if (m_targets.empty())
        return true;

    std::vector<uint32> guids;
    guids.reserve(m_targets.size());
    for (Target const& target : m_targets)
        guids.push_back(target.guidLow);
    std::string guidList = SqlIdList(guids);

    uint32 ledGuilds = 0;
    std::unique_ptr<QueryResult> count(CharacterDatabase.PQuery(
        "SELECT COUNT(*) FROM `guild` WHERE `leaderguid` IN (%s)", guidList.c_str()));
    if (!count)
    {
        error = "guild preflight query failed";
        return false;
    }
    ledGuilds = count->Fetch()[0].GetUInt32();
    if (!ledGuilds)
        return true;

    // Deleting a bot that leads a guild either promotes another member or
    // disbands the guild once it is empty (Player::DeleteFromDB ->
    // Guild::DelMember). A guild that holds anyone outside the managed pool
    // must not be touched by a pool reset, so any non-pool member blocks the
    // whole reset before the first deletion.
    std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
        "SELECT g.`guildid`, g.`name`, g.`leaderguid`, gm.`guid`, c.`account` "
        "FROM `guild` g "
        "LEFT JOIN `guild_member` gm ON gm.`guildid` = g.`guildid` "
        "LEFT JOIN `characters` c ON c.`guid` = gm.`guid` "
        "WHERE g.`leaderguid` IN (%s)", guidList.c_str()));
    if (!rows)
    {
        error = "guild membership preflight query failed";
        return false;
    }

    RandomBotAccountRegistry& registry = RandomBotAccountRegistry::Instance();
    do
    {
        Field* fields = rows->Fetch();
        std::string guildName = fields[1].GetCppString();
        uint32 leaderGuid = fields[2].GetUInt32();
        if (fields[3].IsNULL())
            continue; // bot-led guild without members

        uint32 memberAccount = fields[4].IsNULL() ? 0 : fields[4].GetUInt32();
        if (!memberAccount || !registry.IsRegistered(memberAccount))
        {
            std::string memberName;
            if (PlayerCacheData const* data = sObjectMgr.GetPlayerDataByGUID(fields[3].GetUInt32()))
                memberName = data->sName;
            error = "guild '" + guildName + "' is led by pool character " + std::to_string(leaderGuid) +
                " but holds a member outside the managed pool (character " + std::to_string(fields[3].GetUInt32()) +
                (memberName.empty() ? std::string() : " '" + memberName + "'") + "); reset aborted";
            return false;
        }
    } while (rows->NextRow());

    return true;
}

void RandomBotPoolReset::PlanAtStartup(std::string const& configuredValue, bool autoCreateEnabled)
{
    m_setting = ParsePoolResetSetting(configuredValue);
    m_phase = PoolResetPhase::Disabled;
    m_targets.clear();
    m_managedAccounts = 0;
    m_managedCharacters = 0;
    m_next = 0;
    m_settleNext = 0;
    m_settledAuctions = 0;
    m_stallMs = 0;
    m_progressLogMs = 0;
    m_lastTargetBlocked = false;
    m_maintenance = false;
    m_lastFailure.clear();

    // Read for every mode so `bot pool status` can always report which
    // generation is recorded, even when nothing is scheduled or the registry
    // cannot be validated.
    std::string error;
    bool haveAppliedGeneration = LoadAppliedGeneration(m_appliedGeneration, error);

    RandomBotAccountRegistry& registry = RandomBotAccountRegistry::Instance();
    if (!registry.IsValidated())
    {
        // The registry is the authority for what may be deleted; without it
        // nothing is scheduled and the pool stays as it was.
        m_lastFailure = registry.LastError().empty()
            ? "the managed-account registry is not validated"
            : registry.LastError();
        sLog.outError("TortoiseBots: random pool reset disabled for this start: %s", m_lastFailure.c_str());
        return;
    }

    m_managedAccounts = static_cast<uint32>(registry.AccountCount());

    if (!registry.CountManagedCharacters(m_managedCharacters, error))
    {
        m_lastFailure = error;
        sLog.outError("TortoiseBots: random pool summary unavailable: %s", error.c_str());
        return;
    }

    if (m_setting.mode == PoolResetMode::Invalid)
    {
        m_lastFailure = m_setting.error;
        sLog.outError("TortoiseBots: AiPlayerbot.RandomBotPoolReset is invalid (%s); no reset was scheduled",
            m_setting.error.c_str());
        return;
    }

    if (m_setting.mode == PoolResetMode::Off)
    {
        TB_LOG_BASIC("TortoiseBots: random pool reset off; %u managed accounts, %u characters",
            m_managedAccounts, m_managedCharacters);
        if (!haveAppliedGeneration)
            sLog.outError("TortoiseBots: applied pool generation could not be read: %s", error.c_str());
        return;
    }

    if (!haveAppliedGeneration)
    {
        // Without the recorded generation the module cannot tell whether a
        // one-shot token was already applied, so it does not reset.
        m_lastFailure = error;
        sLog.outError("TortoiseBots: random pool reset skipped: %s", error.c_str());
        return;
    }

    if (!ShouldResetForGeneration(m_setting, m_appliedGeneration))
    {
        TB_LOG_BASIC("TortoiseBots: random pool generation '%s' already applied; reset skipped",
            m_appliedGeneration.c_str());
        return;
    }

    std::string generation = m_setting.mode == PoolResetMode::Once
        ? "'" + m_setting.token + "'"
        : std::string("(always)");

    if (!autoCreateEnabled)
    {
        m_lastFailure = "AiPlayerbot.RandomBotAutoCreate=0";
        sLog.outError("TortoiseBots: random pool generation %s needs AiPlayerbot.RandomBotAutoCreate=1 to refill the pool; "
            "no reset was scheduled (set it and restart)", generation.c_str());
        return;
    }

    if (!SnapshotTargets(error))
    {
        m_lastFailure = error;
        sLog.outError("TortoiseBots: random pool reset skipped: %s", error.c_str());
        return;
    }

    if (!PreflightGuilds(error) || !PreflightHumanSessions(error))
    {
        m_lastFailure = error;
        sLog.outError("TortoiseBots: random pool reset aborted before any deletion: %s", error.c_str());
        return;
    }

    m_maintenance = true;
    m_phase = PoolResetPhase::Planning;
    TB_LOG_BASIC("TortoiseBots: random pool generation %s; reset scheduled for %u characters on %u managed accounts",
        generation.c_str(), static_cast<uint32>(m_targets.size()), m_managedAccounts);
}

bool RandomBotPoolReset::DrainPoolSessions(std::string& error, bool& busy)
{
    busy = false;
    for (Target const& target : m_targets)
    {
        ObjectGuid guid(HIGHGUID_PLAYER, target.guidLow);

        if (BotRecord* record = BotManager::Instance().FindBot(guid))
        {
            if (record->lifecycle != BotLifecycle::Removing)
            {
                // No save: the character is about to be deleted, and the
                // deletion must not race a character save.
                BotManager::Instance().RemoveBot(guid, false);
                HireLifecycle::Instance().Forget(guid);
                BotActivityLeaseManager::Instance().Release(target.guidLow, BotActivity::Grinding);
            }
            busy = true;
            continue;
        }

        Player* player = sObjectAccessor.FindPlayer(guid);
        if (player)
        {
            WorldSession* session = player->GetSession();
            if (session && !session->IsHeadless())
            {
                error = "pool character " + target.name + " (" + std::to_string(target.guidLow) +
                    ") gained a network session during the reset; human sessions are never deleted";
                return false;
            }
            busy = true;
            continue;
        }

        if (BotSessionAdapter::GetHeadlessSessionState(guid) != HeadlessSessionState::NotFound)
            busy = true;
    }
    return true;
}

bool RandomBotPoolReset::TargetOwnsAuctions(uint32 guidLow, uint32& count) const
{
    return CharacterOwnsAuctions(guidLow, count);
}

bool RandomBotPoolReset::SettleAuctions(uint32 guidLow, std::string& error)
{
    uint32 settled = 0;
    if (!SettleCharacterAuctions(guidLow, settled, error))
        return false;
    m_settledAuctions += settled;
    return true;
}

bool RandomBotPoolReset::SettleAllTargetAuctions(std::string& error, bool& done)
{
    done = false;
    if (m_settleNext >= m_targets.size())
    {
        done = true;
        return true;
    }

    // One character per world tick, exactly like deletion: settlement touches
    // the in-memory auction manager and the character database.
    Target const& target = m_targets[m_settleNext];
    if (!SettleAuctions(target.guidLow, error))
        return false;

    ++m_settleNext;
    if (m_settleNext >= m_targets.size() || m_settleNext % 25 == 0)
        TB_LOG_BASIC("TortoiseBots: random pool reset: settled auctions of %u/%u characters (%u listing(s) removed)",
            m_settleNext, static_cast<uint32>(m_targets.size()), m_settledAuctions);

    done = m_settleNext >= m_targets.size();
    return true;
}

bool RandomBotPoolReset::DeleteNextTarget(std::string& error)
{
    Target const& target = m_targets[m_next];
    ObjectGuid guid(HIGHGUID_PLAYER, target.guidLow);
    m_lastTargetBlocked = false;

    // Revalidate the snapshotted ownership: a character that moved to another
    // account between planning and deletion is not ours to delete.
    std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery(
        "SELECT `account` FROM `characters` WHERE `guid` = '%u'", target.guidLow));
    if (!row)
    {
        uint32 exists = 0;
        std::unique_ptr<QueryResult> count(CharacterDatabase.PQuery(
            "SELECT COUNT(*) FROM `characters` WHERE `guid` = '%u'", target.guidLow));
        if (!count)
        {
            error = "character ownership could not be revalidated for guid " + std::to_string(target.guidLow);
            return false;
        }
        exists = count->Fetch()[0].GetUInt32();
        if (exists)
        {
            error = "character " + std::to_string(target.guidLow) + " could not be read back";
            return false;
        }
        ++m_next; // already gone (for example from an interrupted earlier reset)
        return true;
    }

    uint32 accountId = row->Fetch()[0].GetUInt32();
    if (accountId != target.accountId)
    {
        error = "character " + target.name + " (" + std::to_string(target.guidLow) +
            ") moved to account " + std::to_string(accountId) + " during the reset";
        return false;
    }

    if (Player* player = sObjectAccessor.FindPlayer(guid))
    {
        WorldSession* session = player->GetSession();
        if (session && !session->IsHeadless())
        {
            error = "pool character " + target.name + " (" + std::to_string(target.guidLow) +
                ") gained a network session during the reset; human sessions are never deleted";
            return false;
        }
        if (BotManager::Instance().IsBot(guid))
            BotManager::Instance().RemoveBot(guid, false);
        m_lastTargetBlocked = true;
        return true;
    }

    if (BotManager::Instance().IsBot(guid))
    {
        BotManager::Instance().RemoveBot(guid, false);
        m_lastTargetBlocked = true;
        return true;
    }

    // Settlement happens in its own phase, while every pool bidder still
    // exists. A listing that shows up here would be deleted without a refund,
    // so stop instead.
    uint32 ownedAuctions = 0;
    if (TargetOwnsAuctions(target.guidLow, ownedAuctions))
    {
        error = "character " + target.name + " (" + std::to_string(target.guidLow) + ") still owns " +
            std::to_string(ownedAuctions) + " auction(s) after settlement; refusing to delete them unrefunded";
        return false;
    }

    DeleteCharacterEverywhere(target.guidLow, target.accountId);

    HireLifecycle::Instance().Forget(guid);
    BotActivityLeaseManager::Instance().Release(target.guidLow, BotActivity::Grinding);

    ++m_next;
    return true;
}

bool RandomBotPoolReset::VerifyModuleRowsCleared(std::string& error) const
{
    for (size_t offset = 0; offset < m_targets.size(); offset += kVerifyChunkSize)
    {
        std::vector<uint32> chunk;
        size_t end = std::min(offset + kVerifyChunkSize, m_targets.size());
        for (size_t i = offset; i < end; ++i)
            chunk.push_back(m_targets[i].guidLow);
        std::string guids = SqlIdList(chunk);

        struct
        {
            char const* sql;
            char const* label;
        } checks[] = {
            { "SELECT COUNT(*) FROM `ai_playerbot_db_store` WHERE `guid` IN (%s)", "ai_playerbot_db_store" },
            { "SELECT COUNT(*) FROM `ai_playerbot_custom_strategy` WHERE `owner` IN (%s)", "ai_playerbot_custom_strategy" },
            { "SELECT COUNT(*) FROM `tortoise_bots_owned_character` WHERE `character_guid` IN (%s)", "tortoise_bots_owned_character" },
            { "SELECT COUNT(*) FROM `tortoise_bots_hire` WHERE `character_guid` IN (%s)", "tortoise_bots_hire" },
        };

        for (auto const& check : checks)
        {
            std::string sql = check.sql;
            size_t at = sql.find("%s");
            sql.replace(at, 2, guids);
            std::unique_ptr<QueryResult> count(CharacterDatabase.PQuery(sql.c_str()));
            if (!count)
            {
                error = std::string("verification query failed for ") + check.label;
                return false;
            }
            uint32 remaining = count->Fetch()[0].GetUInt32();
            if (remaining)
            {
                error = std::string(check.label) + " still holds " + std::to_string(remaining) +
                    " row(s) for deleted characters";
                return false;
            }
        }
    }
    return true;
}

bool RandomBotPoolReset::VerifyDeletion(std::string& error) const
{
    uint32 remaining = 0;
    if (!RandomBotAccountRegistry::Instance().CountManagedCharacters(remaining, error))
        return false;
    if (remaining)
    {
        error = std::to_string(remaining) + " managed character(s) still exist after deletion";
        return false;
    }
    return VerifyModuleRowsCleared(error);
}

bool RandomBotPoolReset::ApplyGeneration(std::string& error)
{
    if (m_setting.mode == PoolResetMode::Once)
    {
        std::string token = m_setting.token;
        CharacterDatabase.escape_string(token);
        if (!CharacterDatabase.DirectPExecute(
                "INSERT INTO `tortoise_bots_pool_state` (`pool_name`, `applied_generation`, `completed_at`) "
                "VALUES ('%s', '%s', NOW()) "
                "ON DUPLICATE KEY UPDATE `applied_generation` = VALUES(`applied_generation`), `completed_at` = NOW()",
                kPoolName, token.c_str()))
        {
            error = "the applied generation could not be recorded";
            return false;
        }
        m_appliedGeneration = m_setting.token;
        return true;
    }

    // 'always' is diagnostics only and must not overwrite a stored one-shot
    // token: an operator who ran the development mode and later returns to
    // 'once:<token>' must still get the documented one-shot behaviour.
    if (!CharacterDatabase.DirectPExecute(
            "INSERT INTO `tortoise_bots_pool_state` (`pool_name`, `applied_generation`, `completed_at`) "
            "VALUES ('%s', '', NOW()) "
            "ON DUPLICATE KEY UPDATE `completed_at` = NOW()", kPoolName))
    {
        error = "the reset completion time could not be recorded";
        return false;
    }
    return true;
}

void RandomBotPoolReset::EnterSettlingAuctions()
{
    m_phase = PoolResetPhase::SettlingAuctions;
    m_stallMs = 0;
    m_settleNext = 0;
    m_settledAuctions = 0;
    TB_LOG_BASIC("TortoiseBots: random pool reset: settling auctions of %u character(s) before any deletion",
        static_cast<uint32>(m_targets.size()));
}

void RandomBotPoolReset::EnterDeleting()
{
    m_phase = PoolResetPhase::Deleting;
    m_stallMs = 0;
    TB_LOG_BASIC("TortoiseBots: random pool reset: %u character(s) to delete, one per world tick (%u listing(s) settled)",
        static_cast<uint32>(m_targets.size()), m_settledAuctions);
}

PoolResetTick RandomBotPoolReset::Update(uint32 diff)
{
    switch (m_phase)
    {
        case PoolResetPhase::Planning:
        {
            std::string error;
            bool busy = false;
            if (!DrainPoolSessions(error, busy))
            {
                Fail(error);
                return PoolResetTick::Failed;
            }
            if (busy)
            {
                // Bounded wait: a session that refuses to close stops the
                // reset instead of turning into a deletion of a live player.
                m_stallMs += diff;
                if (m_stallMs >= kSessionDrainTimeoutMs)
                {
                    Fail("pool sessions did not close within " + std::to_string(kSessionDrainTimeoutMs / 1000) + " seconds");
                    return PoolResetTick::Failed;
                }
                return PoolResetTick::Working;
            }
            m_stallMs = 0;
            EnterSettlingAuctions();
            return PoolResetTick::Working;
        }

        case PoolResetPhase::SettlingAuctions:
        {
            std::string error;
            bool done = false;
            if (!SettleAllTargetAuctions(error, done))
            {
                Fail(error);
                return PoolResetTick::Failed;
            }
            if (!done)
                return PoolResetTick::Working;
            EnterDeleting();
            return PoolResetTick::Working;
        }

        case PoolResetPhase::Deleting:
        {
            if (m_next >= m_targets.size())
            {
                m_phase = PoolResetPhase::Verifying;
                m_stallMs = 0;
                return PoolResetTick::Working;
            }

            std::string error;
            if (!DeleteNextTarget(error))
            {
                Fail(error);
                return PoolResetTick::Failed;
            }

            if (m_lastTargetBlocked)
            {
                m_stallMs += diff;
                if (m_stallMs >= kDeleteStallTimeoutMs)
                {
                    Fail("character " + m_targets[m_next].name + " (" + std::to_string(m_targets[m_next].guidLow) +
                        ") stayed busy for " + std::to_string(kDeleteStallTimeoutMs / 1000) + " seconds");
                    return PoolResetTick::Failed;
                }
                return PoolResetTick::Working;
            }

            m_stallMs = 0;
            m_progressLogMs += diff;
            if (m_progressLogMs >= kProgressLogIntervalMs || m_next % 25 == 0 || m_next >= m_targets.size())
            {
                m_progressLogMs = 0;
                TB_LOG_BASIC("TortoiseBots: random pool reset progress: %u/%u characters deleted",
                    m_next, static_cast<uint32>(m_targets.size()));
            }

            if (m_next >= m_targets.size())
            {
                m_phase = PoolResetPhase::Verifying;
                m_stallMs = 0;
            }
            return PoolResetTick::Working;
        }

        case PoolResetPhase::Verifying:
        {
            std::string error;
            if (!VerifyDeletion(error))
            {
                // Character deletion runs through the character database's
                // async transaction queue, so a committed delete can still be
                // visible for a moment. Retry inside a bounded window before
                // declaring failure; a genuine leftover never clears.
                m_stallMs += diff;
                if (m_stallMs < kVerifyTimeoutMs)
                    return PoolResetTick::Working;
                Fail(error);
                return PoolResetTick::Failed;
            }
            m_stallMs = 0;
            TB_LOG_BASIC("TortoiseBots: random pool reset verified: 0 characters remain on %u managed accounts",
                m_managedAccounts);
            m_phase = PoolResetPhase::Rebuilding;
            return PoolResetTick::Working;
        }

        case PoolResetPhase::Rebuilding:
        {
            std::string error;
            if (!ApplyGeneration(error))
            {
                Fail(error);
                return PoolResetTick::Failed;
            }
            m_phase = PoolResetPhase::Complete;
            m_maintenance = false;
            if (m_setting.mode == PoolResetMode::Once)
                TB_LOG_BASIC("TortoiseBots: random pool generation '%s' applied; pool rebuild starts now",
                    m_setting.token.c_str());
            else
                TB_LOG_BASIC("TortoiseBots: random pool reset complete (development 'always' mode); pool rebuild starts now");
            return PoolResetTick::Completed;
        }

        default:
            return PoolResetTick::Idle;
    }
}

void RandomBotPoolReset::Shutdown()
{
    m_phase = PoolResetPhase::Disabled;
    m_targets.clear();
    m_managedAccounts = 0;
    m_managedCharacters = 0;
    m_next = 0;
    m_settleNext = 0;
    m_settledAuctions = 0;
    m_stallMs = 0;
    m_progressLogMs = 0;
    m_lastTargetBlocked = false;
    m_maintenance = false;
    m_appliedGeneration.clear();
    m_lastFailure.clear();
    m_setting = PoolResetSetting();
}

} // namespace TortoiseBots
