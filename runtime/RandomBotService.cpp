#include "RandomBotService.h"

#include "BotManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "World.h"
#include "WorldSession.h"
#include "Log.h"
#include "Database/DatabaseEnv.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomPlayerbotMgr.h"

#include <algorithm>
#include <ctime>
#include <set>

namespace TortoiseBots
{

RandomBotService& RandomBotService::Instance()
{
    static RandomBotService instance;
    return instance;
}

void RandomBotService::Initialize()
{
    if (m_initialized)
        return;

    m_initialized = true;
    m_serviceElapsedMs = 0;
    if (!sPlayerbotAIConfig.enabled || !sPlayerbotAIConfig.randomBotAutologin)
    {
        sLog.outString("TortoiseBots: native random-bot service disabled by configuration");
        return;
    }

    LoadCandidates();
    m_targetCount = TargetCount();
    m_started = sPlayerbotAIConfig.randomBotLoginAtStartup &&
        (!sPlayerbotAIConfig.randomBotLoginWithPlayer || m_humanSessions > 0);

    if (sPlayerbotAIConfig.randomBotAutoCreate)
        sLog.outString("TortoiseBots: native random-bot service uses existing random characters; account/character auto-create is not enabled");

    sLog.outString("TortoiseBots: native random-bot pool loaded (%u candidates, target %u, startup %u)",
        static_cast<uint32>(m_candidates.size()), m_targetCount, m_started);
}

void RandomBotService::LoadCandidates()
{
    m_candidates.clear();
    m_ageMs.clear();
    m_strategyAgeMs.clear();
    m_randomizeAgeMs.clear();
    m_nextCandidate = 0;

    std::set<uint32> accountIds;
    auto accounts = LoginDatabase.PQuery("SELECT id FROM account WHERE username LIKE '%s%%'",
        sPlayerbotAIConfig.randomBotAccountPrefix.c_str());
    if (!accounts)
        return;

    do
    {
        Field* fields = accounts->Fetch();
        uint32 accountId = fields[0].GetUInt32();
        if (accountId)
        {
            accountIds.insert(accountId);
            sPlayerbotAIConfig.IsInRandomAccountList(accountId);
        }
    } while (accounts->NextRow());

    std::set<uint32> characterIds;
    for (uint32 accountId : accountIds)
    {
        auto characters = CharacterDatabase.PQuery(
            "SELECT guid FROM characters WHERE account = '%u' ORDER BY guid", accountId);
        if (!characters)
            continue;

        do
        {
            Field* fields = characters->Fetch();
            uint32 guidLow = fields[0].GetUInt32();
            if (!guidLow || !characterIds.insert(guidLow).second)
                continue;

            m_candidates.push_back({accountId, ObjectGuid(HIGHGUID_PLAYER, guidLow)});
        } while (characters->NextRow());
    }

    // Rotate the stable DB order so the first startup does not always select
    // the same low GUIDs. The candidate pool itself remains immutable until a
    // process restart, so there is no query or full-world scan per tick.
    if (!m_candidates.empty())
    {
        size_t offset = static_cast<size_t>(std::time(nullptr)) % m_candidates.size();
        std::rotate(m_candidates.begin(), m_candidates.begin() + offset, m_candidates.end());
    }

    m_ageMs.assign(m_candidates.size(), 0);
    m_strategyAgeMs.assign(m_candidates.size(), 0);
    m_randomizeAgeMs.assign(m_candidates.size(), 0);
}

uint32 RandomBotService::TargetCount() const
{
    uint32 minCount = std::min(sPlayerbotAIConfig.minRandomBots, sPlayerbotAIConfig.maxRandomBots);
    uint32 maxCount = std::max(sPlayerbotAIConfig.minRandomBots, sPlayerbotAIConfig.maxRandomBots);
    if (!maxCount || m_candidates.empty())
        return 0;

    uint32 range = maxCount - minCount;
    uint32 configuredTarget = minCount;
    if (range)
        configuredTarget += static_cast<uint32>(std::time(nullptr) % (range + 1));

    return std::min<uint32>(configuredTarget, static_cast<uint32>(m_candidates.size()));
}

void RandomBotService::OnHumanLogin()
{
    ++m_humanSessions;
    if (m_initialized && sPlayerbotAIConfig.randomBotAutologin && !m_started)
    {
        m_started = true;
        sLog.outString("TortoiseBots: native random-bot service started after a human login");
    }
}

void RandomBotService::OnHumanLogout()
{
    if (m_humanSessions)
        --m_humanSessions;
}

void RandomBotService::RemoveExpiredBots(uint32_t diff)
{
    for (size_t i = 0; i < m_candidates.size(); ++i)
    {
        Candidate const& candidate = m_candidates[i];
        if (!BotManager::Instance().IsRandomBot(candidate.characterGuid))
        {
            m_ageMs[i] = 0;
            continue;
        }

        BotRecord* record = BotManager::Instance().FindBot(candidate.characterGuid);
        if (!record || !record->enteredWorld)
            continue;

        m_ageMs[i] += diff;
        if (!sPlayerbotAIConfig.randomBotTimedLogout || !sPlayerbotAIConfig.maxRandomBotInWorldTime)
            continue;

        uint64_t limit = static_cast<uint64_t>(sPlayerbotAIConfig.maxRandomBotInWorldTime) * 1000;
        if (m_ageMs[i] < limit)
            continue;

        sLog.outString("TortoiseBots: native random bot %s reached its online lifetime; removing",
            candidate.characterGuid.GetString().c_str());
        BotManager::Instance().RemoveBot(candidate.characterGuid, true);
        m_ageMs[i] = 0;
    }
}

void RandomBotService::MaintainOnlinePool()
{
    if (!m_started || sWorld.IsShutdowning())
        return;
    if (sPlayerbotAIConfig.randomBotLoginWithPlayer && !m_humanSessions)
    {
        for (Candidate const& candidate : m_candidates)
            if (BotManager::Instance().IsRandomBot(candidate.characterGuid))
                BotManager::Instance().RemoveBot(candidate.characterGuid, true);
        return;
    }

    uint32 online = 0;
    for (Candidate const& candidate : m_candidates)
        if (BotManager::Instance().IsRandomBot(candidate.characterGuid))
            ++online;

    if (online >= m_targetCount || m_candidates.empty())
        return;

    uint32 perInterval = sPlayerbotAIConfig.randomBotsMaxLoginsPerInterval;
    if (!perInterval)
        return;

    uint32 added = 0;
    size_t attempts = 0;
    while (online < m_targetCount && added < perInterval && attempts < m_candidates.size())
    {
        size_t index = m_nextCandidate++ % m_candidates.size();
        ++attempts;
        Candidate const& candidate = m_candidates[index];

        if (BotManager::Instance().IsBot(candidate.characterGuid))
            continue;

        if (Player* player = sObjectAccessor.FindPlayer(candidate.characterGuid))
        {
            // A network session owns the character; native random control may
            // never steal it. Headless sessions are likewise left to the
            // existing BotManager record/reclaim path.
            if (!player->GetSession() || !player->GetSession()->IsHeadless())
                continue;
        }
        if (sWorld.FindHeadlessSession(candidate.characterGuid) ||
            sWorld.HasPendingHeadlessSession(candidate.characterGuid))
            continue;

        if (BotManager::Instance().AddRandomBot(candidate.accountId, candidate.characterGuid))
        {
            ++online;
            ++added;
            sLog.outString("TortoiseBots: native random bot %s queued on account %u",
                candidate.characterGuid.GetString().c_str(), candidate.accountId);
        }
    }
}

void RandomBotService::Update(uint32_t diff)
{
    if (!m_initialized || !sPlayerbotAIConfig.enabled || !sPlayerbotAIConfig.randomBotAutologin)
        return;

    uint32_t cadence = std::max<uint32_t>(1000, sPlayerbotAIConfig.randomBotUpdateInterval);
    m_serviceElapsedMs += diff;
    if (m_serviceElapsedMs < cadence)
        return;

    uint32_t elapsed = m_serviceElapsedMs;
    m_serviceElapsedMs = 0;
    RemoveExpiredBots(elapsed);

    for (size_t i = 0; i < m_candidates.size(); ++i)
    {
        Candidate const& candidate = m_candidates[i];
        BotRecord* record = BotManager::Instance().FindBot(candidate.characterGuid);
        Player* player = sObjectAccessor.FindPlayer(candidate.characterGuid);
        if (!record || !record->enteredWorld || !player)
            continue;

        // Recovery/expired-value work stays on the world thread and is bounded
        // by the configured service cadence rather than a second AI loop.
        sRandomPlayerbotMgr.ProcessBot(player);

        m_strategyAgeMs[i] += elapsed;
        uint32 strategyInterval = sPlayerbotAIConfig.minRandomBotChangeStrategyTime;
        if (sPlayerbotAIConfig.maxRandomBotChangeStrategyTime > strategyInterval)
            strategyInterval = urand(strategyInterval, sPlayerbotAIConfig.maxRandomBotChangeStrategyTime);
        if (strategyInterval && m_strategyAgeMs[i] >= strategyInterval * 1000)
        {
            sRandomPlayerbotMgr.ChangeStrategy(player);
            m_strategyAgeMs[i] = 0;
        }

        m_randomizeAgeMs[i] += elapsed;
        uint32 randomizeInterval = sPlayerbotAIConfig.minRandomBotRandomizeTime;
        if (sPlayerbotAIConfig.maxRandomBotRandomizeTime > randomizeInterval)
            randomizeInterval = urand(randomizeInterval, sPlayerbotAIConfig.maxRandomBotRandomizeTime);
        if (sPlayerbotAIConfig.randomGearUpgradeEnabled && randomizeInterval &&
            m_randomizeAgeMs[i] >= randomizeInterval * 1000)
        {
            sRandomPlayerbotMgr.UpdateGearSpells(player);
            m_randomizeAgeMs[i] = 0;
        }
    }

    MaintainOnlinePool();
}

void RandomBotService::Shutdown()
{
    if (!m_initialized)
        return;

    for (Candidate const& candidate : m_candidates)
    {
        if (BotManager::Instance().IsRandomBot(candidate.characterGuid))
            BotManager::Instance().RemoveBot(candidate.characterGuid, true);
    }

    m_started = false;
    m_initialized = false;
}

} // namespace TortoiseBots
