// pi-lens-ignore: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access,clang:uninitialized,clang:undefined_identifier,clang:undeclared_identifier,clang:all
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
#include "playerbot/RandomBotFacade.h"
#include "AccountMgr.h"
#include "SharedDefines.h"
#include "Database/DBCStores.h"
#include "Util.h"

#if __has_include("Handlers/CharacterCreation.h")
#include "Handlers/CharacterCreation.h"
#elif __has_include("CharacterCreation.h")
#include "CharacterCreation.h"
#else
// Feature 01 requires core commit 94dfa7e (src/game/Handlers/CharacterCreation.h).
// If this fires, the module is built against a core without the generic
// synchronous CharacterCreation seam.
#error "TortoiseBots feature 01 requires core 94dfa7e: src/game/Handlers/CharacterCreation.h not found"
#endif

#include <algorithm>
#include <ctime>
#include <memory>
#include <set>

namespace TortoiseBots
{

namespace
{
std::string GenerateRndBotName()
{
    // 4..8 chars, first upper, rest lower, passes CheckPlayerName.
    // Prefix Rnd ensures recognizable but not donor-styled; uniqueness via DB.
    static const char letters[] = "abcdefghijklmnopqrstuvwxyz";
    std::string name;
    name.reserve(8);
    name.push_back(char('A' + urand(0, 25)));
    uint32 len = urand(5, 8);
    for (uint32 i = 1; i < len; ++i)
        name.push_back(letters[urand(0, 25)]);
    return name;
}

std::string GenerateRandomPassword()
{
    // 12 chars alphanumeric, well within MAX_ACCOUNT_STR (16). No credential
    // exposure: not derived from username, hashed via CalculateShaPassHash;
    // random, not "secure" in any cryptographic sense.
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";
    std::string pw;
    pw.reserve(12);
    for (int i = 0; i < 12; ++i)
        pw.push_back(charset[urand(0, 61)]);
    return pw;
}
} // namespace

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
    m_pinnedGuids.clear();
    m_pinnedResolved = false;
    m_rndBotAccountIds.clear();
    m_failedAutoCreateAccounts.clear();
    m_freshAutoCreateDisabled = false;
    if (!sPlayerbotAIConfig.enabled)
    {
        sLog.outString("TortoiseBots: native random-bot service disabled by configuration");
        return;
    }
    // Load pool even if autologin is off when auto-create is on: the deficit
    // check needs the real candidate set.
    if (!sPlayerbotAIConfig.randomBotAutologin && !sPlayerbotAIConfig.randomBotAutoCreate)
    {
        sLog.outString("TortoiseBots: native random-bot service disabled by configuration");
        return;
    }

    LoadCandidates();
    // m_targetCount historically capped to candidates size. With auto-create
    // the desired target may exceed current candidates, so keep desired when
    // auto-create is enabled.
    if (sPlayerbotAIConfig.randomBotAutoCreate)
        m_targetCount = DesiredTargetCount();
    else
        m_targetCount = TargetCount();
    m_started = sPlayerbotAIConfig.randomBotLoginAtStartup &&
        (!sPlayerbotAIConfig.randomBotLoginWithPlayer || m_humanSessions > 0);

    sLog.outString("TortoiseBots: native random-bot pool loaded (%u candidates, target %u, startup %u, autoCreate %u)",
        static_cast<uint32>(m_candidates.size()), m_targetCount, m_started, sPlayerbotAIConfig.randomBotAutoCreate ? 1 : 0);
}

void RandomBotService::LoadCandidates()
{
    m_candidates.clear();
    m_ageMs.clear();
    m_strategyAgeMs.clear();
    m_randomizeAgeMs.clear();
    m_rndBotAccountIds.clear();
    m_nextCandidate = 0;

    std::set<uint32> accountIds;
    std::unique_ptr<QueryResult> accounts(LoginDatabase.PQuery("SELECT id FROM account WHERE username LIKE '%s%%'",
        sPlayerbotAIConfig.randomBotAccountPrefix.c_str()));
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

    m_rndBotAccountIds.assign(accountIds.begin(), accountIds.end());

    std::set<uint32> characterIds;
    for (uint32 accountId : accountIds)
    {
        std::unique_ptr<QueryResult> characters(CharacterDatabase.PQuery(
            "SELECT guid FROM characters WHERE account = '%u' ORDER BY guid", accountId));
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

uint32 RandomBotService::DesiredTargetCount() const
{
    uint32 minCount = std::min(sPlayerbotAIConfig.minRandomBots, sPlayerbotAIConfig.maxRandomBots);
    uint32 maxCount = std::max(sPlayerbotAIConfig.minRandomBots, sPlayerbotAIConfig.maxRandomBots);
    if (!maxCount)
        return 0;
    uint32 range = maxCount - minCount;
    uint32 configuredTarget = minCount;
    if (range)
        configuredTarget += static_cast<uint32>(std::time(nullptr) % (range + 1));
    return configuredTarget;
}

// Deterministic allowed team from all cached candidates for this account.
// TEAM_NONE = empty or unknown (any faction allowed). Sets isMixed=true when
// the cached candidates contain both factions (incompatible under
// ALLOW_TWO_SIDE_ACCOUNTS=0) – caller must exclude that account.
uint32_t RandomBotService::GetAccountAllowedTeam(uint32_t accountId, bool& isMixed) const
{
    bool hasAlliance = false;
    bool hasHorde = false;
    isMixed = false;
    for (Candidate const& c : m_candidates)
    {
        if (c.accountId != accountId)
            continue;
        PlayerCacheData const* data = sObjectMgr.GetPlayerDataByGUID(c.characterGuid.GetCounter());
        if (!data || !data->uiRace)
            continue;
        Team t = Player::TeamForRace(data->uiRace);
        if (t == ALLIANCE)
            hasAlliance = true;
        else if (t == HORDE)
            hasHorde = true;
        if (hasAlliance && hasHorde)
        {
            isMixed = true;
            return TEAM_NONE;
        }
    }
    if (hasAlliance && hasHorde)
    {
        isMixed = true;
        return TEAM_NONE;
    }
    if (hasAlliance)
        return ALLIANCE;
    if (hasHorde)
        return HORDE;
    return TEAM_NONE;
}

RandomBotService::AutoCreateCharResult RandomBotService::TryCreateCharacterOnAccount(uint32_t accountId, std::vector<std::pair<uint8_t, uint8_t>> const& validForAccount)
{
    if (validForAccount.empty())
    {
        sLog.outError("TortoiseBots: auto-create account %u has no valid race/class for its team, skipping", accountId);
        return AutoCreateCharResult::Permanent;
    }

    // Up to 5 name attempts per account per cadence. Transient name collisions
    // (NAME_IN_USE/RESERVED/CHAR_CREATE_FAILED or local normalize miss) are
    // retried silently and do not mark the account permanently failed. Permanent
    // materialization errors (DISABLED, PVP_TEAMS_VIOLATION, ACCOUNT/SERVER_LIMIT,
    // or other) are logged once and the caller must remember the account as
    // permanently failed so it is never retried every RandomBotUpdateInterval.
    for (int nameAttempt = 0; nameAttempt < 5; ++nameAttempt)
    {
        std::string name = GenerateRndBotName();
        std::string norm = name;
        if (!normalizePlayerName(norm))
            continue;
        if (sObjectMgr.GetPlayerGuidByName(norm))
            continue;

        auto pr = validForAccount[urand(0, uint32(validForAccount.size() - 1))];
        uint8 race = pr.first;
        uint8 cls = pr.second;

        CharacterCreateInfo info;
        info.name = norm;
        info.race = race;
        info.class_ = cls;
        info.gender = urand(0, 1) ? GENDER_FEMALE : GENDER_MALE;
        info.skin = uint8(urand(0, 7));
        info.face = uint8(urand(0, 7));
        info.hairStyle = uint8(urand(0, 7));
        info.hairColor = uint8(urand(0, 7));
        info.facialHair = uint8(urand(0, 7));
        info.outfitId = 0;
        info.challengeMask = 0;

        if (!sObjectMgr.GetPlayerInfo(race, cls))
        {
            sLog.outError("TortoiseBots: auto-create race %u class %u has no PlayerInfo", uint32(race), uint32(cls));
            return AutoCreateCharResult::Permanent;
        }

        CharacterCreateOutcome outcome = CharacterCreation::CreateCharacter(accountId, info);
        if (outcome.result == CHAR_CREATE_SUCCESS)
        {
            m_candidates.push_back({accountId, outcome.guid});
            m_ageMs.push_back(0);
            m_strategyAgeMs.push_back(0);
            m_randomizeAgeMs.push_back(0);
            sLog.outString("TortoiseBots: auto-create created character %s (%s) race %u class %u on account %u",
                norm.c_str(), outcome.guid.GetString().c_str(), uint32(race), uint32(cls), accountId);
            return AutoCreateCharResult::Success;
        }

        if (outcome.result == CHAR_CREATE_NAME_IN_USE || outcome.result == CHAR_NAME_RESERVED || outcome.result == CHAR_CREATE_FAILED)
        {
            // Transient name collision – try another name silently.
            continue;
        }
        if (outcome.result == CHAR_CREATE_PVP_TEAMS_VIOLATION)
        {
            sLog.outError("TortoiseBots: auto-create account %u team violation for race %u (mixed or wrong faction), skipping account", accountId, uint32(race));
            return AutoCreateCharResult::Permanent;
        }
        if (outcome.result == CHAR_CREATE_ACCOUNT_LIMIT || outcome.result == CHAR_CREATE_SERVER_LIMIT)
        {
            sLog.outString("TortoiseBots: auto-create account %u at limit (%u), excluding from auto-create", accountId, uint32(outcome.result));
            return AutoCreateCharResult::Permanent;
        }
        if (outcome.result == CHAR_CREATE_DISABLED)
        {
            sLog.outError("TortoiseBots: auto-create race %u class %u disabled (team disabled or not playable), skipping account %u", uint32(race), uint32(cls), accountId);
            return AutoCreateCharResult::Permanent;
        }
        // Generic fail-closed – permanent for this process, log once.
        sLog.outError("TortoiseBots: auto-create for %s race %u class %u on account %u failed %u, excluding account", norm.c_str(), uint32(race), uint32(cls), accountId, uint32(outcome.result));
        return AutoCreateCharResult::Permanent;
    }

    // Exhausted name attempts – transient, not a permanent exclusion.
    return AutoCreateCharResult::TransientName;
}

bool RandomBotService::TryAutoCreate()
{
    if (!sPlayerbotAIConfig.randomBotAutoCreate)
        return false;
    if (!m_initialized || !sPlayerbotAIConfig.enabled)
        return false;
    if (sWorld.IsShutdowning())
        return false;

    uint32 desired = DesiredTargetCount();
    if (!desired)
        return false;
    if (m_candidates.size() >= desired)
        return false;
    // Keep target in sync when auto-create is active (original TargetCount
    // capped to candidates and would stay 0 on a fresh DB).
    if (m_targetCount < desired)
        m_targetCount = desired;

    // One bounded attempt per cadence (caller is throttled to
    // RandomBotUpdateInterval, >=1s). No per-tick LIKE scan.

    uint32 perAccountLimit = sWorld.getConfig(CONFIG_UINT32_CHARACTERS_PER_ACCOUNT);
    if (!perAccountLimit)
        perAccountLimit = 10;
    uint32 perRealmLimit = sWorld.getConfig(CONFIG_UINT32_CHARACTERS_PER_REALM);
    if (!perRealmLimit)
        perRealmLimit = 50;
    uint32 accountLimit = std::min(perAccountLimit, perRealmLimit);

    // Valid race/class from PlayerInfo (covers Goblin/High Elf when Turtle data present).
    std::vector<std::pair<uint8_t,uint8_t>> validAll;
    validAll.reserve(64);
    for (uint32_t race = 1; race < MAX_RACES; ++race)
        for (uint32_t cls = 1; cls < MAX_CLASSES; ++cls)
            if (sObjectMgr.GetPlayerInfo(race, cls))
                validAll.emplace_back(uint8_t(race), uint8_t(cls));
    if (validAll.empty())
    {
        sLog.outError("TortoiseBots: auto-create no valid PlayerInfo race/class");
        return false;
    }

    bool allowTwoSide = sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_ACCOUNTS) != 0;

    // Try existing accounts in deterministic order. Permanently failed accounts
    // (mixed, disabled, faction violation, limit) are remembered in
    // m_failedAutoCreateAccounts, logged once, and never retried every
    // RandomBotUpdateInterval. Transient name collisions are not remembered
    // and remain retryable next cadence.
    for (uint32 accId : m_rndBotAccountIds)
    {
        if (m_failedAutoCreateAccounts.find(accId) != m_failedAutoCreateAccounts.end())
            continue;

        uint32 cnt = 0;
        for (Candidate const& c : m_candidates)
            if (c.accountId == accId)
                ++cnt;
        if (cnt >= accountLimit)
        {
            sLog.outError("TortoiseBots: auto-create account %u at character limit (%u), excluding from auto-create", accId, accountLimit);
            m_failedAutoCreateAccounts.insert(accId);
            continue;
        }

        bool isMixed = false;
        uint32_t allowed = GetAccountAllowedTeam(accId, isMixed);
        if (isMixed)
        {
            sLog.outError("TortoiseBots: auto-create account %u has mixed-faction RNDBOT characters, excluding from auto-create", accId);
            m_failedAutoCreateAccounts.insert(accId);
            continue;
        }

        std::vector<std::pair<uint8_t,uint8_t>> validForAccount = validAll;
        if (allowed != uint32_t(TEAM_NONE) && !allowTwoSide)
        {
            std::vector<std::pair<uint8_t,uint8_t>> filtered;
            filtered.reserve(validAll.size());
            for (auto const& pr : validAll)
                if (Player::TeamForRace(pr.first) == allowed)
                    filtered.push_back(pr);
            if (!filtered.empty())
                validForAccount.swap(filtered);
            else
            {
                sLog.outError("TortoiseBots: auto-create account %u team %u has no valid race/class, excluding from auto-create", accId, uint32_t(allowed));
                m_failedAutoCreateAccounts.insert(accId);
                continue;
            }
        }

        AutoCreateCharResult res = TryCreateCharacterOnAccount(accId, validForAccount);
        if (res == AutoCreateCharResult::Success)
            return true;
        if (res == AutoCreateCharResult::Permanent)
        {
            m_failedAutoCreateAccounts.insert(accId);
            continue;
        }
        // TransientName – try next account same cadence, do not remember.
    }

    // No existing account succeeded – allocate a new RNDBOT account unless a
    // previous fresh-account permanent failure disabled this path for the
    // process lifetime (log once, still allow existing accounts to fill).
    if (m_freshAutoCreateDisabled)
        return false;

    std::string prefix = sPlayerbotAIConfig.randomBotAccountPrefix;
    if (prefix.empty())
        prefix = "RNDBOT";

    uint32 newAccountId = 0;
    std::string newUsername;
    // Bound attempts: at most 20 usernames per interval, random password.
    for (int attempt = 0; attempt < 20 && !newAccountId; ++attempt)
    {
        uint32 suffix = urand(1000, 999999);
        std::string username = prefix + std::to_string(suffix);
        if (username.size() > MAX_ACCOUNT_STR)
            username = username.substr(0, MAX_ACCOUNT_STR);
        if (sAccountMgr.GetId(username) != 0)
            continue;
        std::string password = GenerateRandomPassword();
        AccountOpResult res = sAccountMgr.CreateAccount(username, password);
        if (res == AOR_OK)
        {
            uint32 id = sAccountMgr.GetId(username);
            if (id)
            {
                newAccountId = id;
                newUsername = username;
                m_rndBotAccountIds.push_back(id);
                sLog.outString("TortoiseBots: auto-create created RNDBOT account %s (%u)", username.c_str(), id);
            }
            else
            {
                sLog.outError("TortoiseBots: auto-create account %s created but GetId failed", username.c_str());
                return false;
            }
        }
        else if (res == AOR_NAME_ALREDY_EXIST)
        {
            continue;
        }
        else
        {
            sLog.outError("TortoiseBots: auto-create CreateAccount %s failed %d, trying next", username.c_str(), int(res));
            continue;
        }
    }
    if (!newAccountId)
    {
        sLog.outError("TortoiseBots: auto-create could not allocate RNDBOT account after attempts");
        return false;
    }

    // New account is empty – any faction allowed, use full valid list.
    AutoCreateCharResult freshRes = TryCreateCharacterOnAccount(newAccountId, validAll);
    if (freshRes == AutoCreateCharResult::Success)
        return true;
    if (freshRes == AutoCreateCharResult::Permanent)
    {
        m_failedAutoCreateAccounts.insert(newAccountId);
        m_freshAutoCreateDisabled = true;
        sLog.outError("TortoiseBots: auto-create fresh account %s (%u) permanently failed, disabling further fresh RNDBOT account allocation for this process (existing accounts remain eligible)", newUsername.c_str(), newAccountId);
        return false;
    }
    // TransientName on fresh account – keep it for next interval but do not
    // yet disable fresh allocation; next cadence will retry existing + fresh
    // before allocating another.
    return false;
}

void RandomBotService::ResolvePinnedBots()
{
    if (m_pinnedResolved)
        return;
    if (sPlayerbotAIConfig.pinnedBotNames.empty())
    {
        m_pinnedResolved = true;
        return;
    }

    // One-time, deferred until DB is usable: `characters.name` -> guid lookup
    // using the database's stored collation (separate from the teleport
    // facade's normalized comparison). PQuery null means not found or lookup
    // failed (does not distinguish); m_pinnedResolved avoids retry until
    // restart. No per-tick SELECT.
    for (std::string const& rawName : sPlayerbotAIConfig.pinnedBotNames)
    {
        if (rawName.empty())
            continue;
        std::string escaped = rawName;
        CharacterDatabase.escape_string(escaped);
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT guid FROM characters WHERE name = '%s' LIMIT 1", escaped.c_str()));
        if (!result)
        {
            sLog.outString("TortoiseBots: pinned bot '%s' was not found or the lookup failed (no retry until restart)", rawName.c_str());
            continue;
        }
        Field* fields = result->Fetch();
        uint32 guidLow = fields[0].GetUInt32();
        if (!guidLow)
        {
            sLog.outString("TortoiseBots: pinned bot '%s' resolved to invalid guid", rawName.c_str());
            continue;
        }
        // Bounded, resolution-only check: a name that exists in `characters`
        // but is not in the discovered RNDBOT pool is logged and ignored.
        // Without this, such names were silently ignored later in the
        // prioritized pool pass, which mismatched the documented behavior.
        bool inPool = false;
        for (Candidate const& candidate : m_candidates)
            if (candidate.characterGuid.GetCounter() == guidLow)
            {
                inPool = true;
                break;
            }
        if (!inPool)
        {
            sLog.outString("TortoiseBots: pinned bot '%s' (guid %u) not in RNDBOT pool, ignoring", rawName.c_str(), guidLow);
            continue;
        }
        m_pinnedGuids.insert(guidLow);
        sLog.outString("TortoiseBots: pinned bot '%s' resolved to guid %u", rawName.c_str(), guidLow);
    }

    m_pinnedResolved = true;
    if (m_pinnedGuids.empty())
        sLog.outString("TortoiseBots: no pinned bots resolved from %u configured names", static_cast<uint32>(sPlayerbotAIConfig.pinnedBotNames.size()));
    else
        sLog.outString("TortoiseBots: %u pinned bot(s) cached", static_cast<uint32>(m_pinnedGuids.size()));
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

        // Pinned bots are exempt from timed logout (native login teleport is
        // skipped separately via sRandomBotFacade::IsPinnedBot's normalized
        // name match, best-effort) but still gated by
        // RandomBotLoginWithPlayer=1 (see MaintainOnlinePool).
        if (IsPinnedGuid(candidate.characterGuid.GetCounter()))
        {
            m_ageMs[i] = 0;
            continue;
        }

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

    // Pinned bots are prioritized within the cap, not additive beyond it.
    if (!m_pinnedGuids.empty())
    {
        for (uint32 pinnedGuid : m_pinnedGuids)
        {
            if (online >= m_targetCount || added >= perInterval)
                break;

            Candidate const* pinnedCandidate = nullptr;
            for (Candidate const& c : m_candidates)
                if (c.characterGuid.GetCounter() == pinnedGuid)
                {
                    pinnedCandidate = &c;
                    break;
                }

            if (!pinnedCandidate)
                continue;

            if (BotManager::Instance().IsBot(pinnedCandidate->characterGuid))
                continue;

            if (Player* player = sObjectAccessor.FindPlayer(pinnedCandidate->characterGuid))
            {
                if (!player->GetSession() || !player->GetSession()->IsHeadless())
                    continue;
            }
            if (sWorld.FindHeadlessSession(pinnedCandidate->characterGuid) ||
                sWorld.HasPendingHeadlessSession(pinnedCandidate->characterGuid))
                continue;

            if (BotManager::Instance().AddRandomBot(pinnedCandidate->accountId, pinnedCandidate->characterGuid))
            {
                ++online;
                ++added;
                sLog.outString("TortoiseBots: pinned random bot %s queued on account %u (prioritized)",
                    pinnedCandidate->characterGuid.GetString().c_str(), pinnedCandidate->accountId);
            }
        }
    }

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
    if (!m_initialized || !sPlayerbotAIConfig.enabled)
        return;

    uint32_t cadence = std::max<uint32_t>(1000, sPlayerbotAIConfig.randomBotUpdateInterval);
    m_serviceElapsedMs += diff;
    if (m_serviceElapsedMs < cadence)
        return;

    uint32_t elapsed = m_serviceElapsedMs;
    m_serviceElapsedMs = 0;

    // Bounded auto-create: one attempt per cadence, no per-tick LIKE scan.
    if (sPlayerbotAIConfig.randomBotAutoCreate)
        TryAutoCreate();

    if (!sPlayerbotAIConfig.randomBotAutologin)
        return;

    if (!m_pinnedResolved)
        ResolvePinnedBots();
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
        sRandomBotFacade.ProcessBot(player);

        m_strategyAgeMs[i] += elapsed;
        uint32 strategyInterval = sPlayerbotAIConfig.minRandomBotChangeStrategyTime;
        if (sPlayerbotAIConfig.maxRandomBotChangeStrategyTime > strategyInterval)
            strategyInterval = urand(strategyInterval, sPlayerbotAIConfig.maxRandomBotChangeStrategyTime);
        if (strategyInterval && m_strategyAgeMs[i] >= strategyInterval * 1000)
        {
            sRandomBotFacade.ChangeStrategy(player);
            m_strategyAgeMs[i] = 0;
        }

        m_randomizeAgeMs[i] += elapsed;
        uint32 randomizeInterval = sPlayerbotAIConfig.minRandomBotRandomizeTime;
        if (sPlayerbotAIConfig.maxRandomBotRandomizeTime > randomizeInterval)
            randomizeInterval = urand(randomizeInterval, sPlayerbotAIConfig.maxRandomBotRandomizeTime);
        if (sPlayerbotAIConfig.randomGearUpgradeEnabled && randomizeInterval &&
            m_randomizeAgeMs[i] >= randomizeInterval * 1000)
        {
            sRandomBotFacade.UpdateGearSpells(player);
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
    m_pinnedGuids.clear();
    m_pinnedResolved = false;
    m_rndBotAccountIds.clear();
    m_failedAutoCreateAccounts.clear();
    m_freshAutoCreateDisabled = false;
}

} // namespace TortoiseBots
