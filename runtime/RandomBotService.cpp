#include "RandomBotService.h"
#include "BotActivityLease.h"
#include "BotManager.h"
#include "GearSeedingGuard.h"
#include "HireLifecycle.h"
#include "../host/BotSessionAdapter.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "World.h"
#include "WorldSession.h"
#include "Log.h"
#include "Database/DatabaseEnv.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomBotFacade.h"
#include "RandomBotAccountRegistry.h"
#include "RandomBotPoolReset.h"
#include "AccountMgr.h"
#include "SharedDefines.h"
#include "Database/DBCStores.h"
#include "Util.h"
#include "Timer.h"
#include "../host/ModuleLog.h"

#if __has_include("Handlers/CharacterCreation.h")
#include "Handlers/CharacterCreation.h"
#elif __has_include("CharacterCreation.h")
#include "CharacterCreation.h"
#else
// Feature 01 requires core PR #416
// src/game/Handlers/CharacterCreation.h. If this fires, the module is built
// against a core without the generic synchronous CharacterCreation seam.
#error "TortoiseBots feature 01 requires core PR #416 (Handlers/CharacterCreation.h not found)"
#endif

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>
#include <random>
#include <set>

namespace TortoiseBots
{

namespace
{
// Level-ladder holds (RandomBotService::LadderBuildBuckets / RemoveExpiredBots): a candidate
// that could not be brought online is left alone for a minute instead of being picked again
// every pass; a candidate that left the world within a minute of entering it (a refused
// login, a bad spawn, a crash) is set aside for an hour. These are the only two holds the
// ladder arms, so LADDER_QUICK_LOGOUT_HOLD_MS is also the longest hold a candidate may
// legitimately carry.
uint32_t const LADDER_BUSY_RETRY_MS = 60000;
uint32_t const LADDER_QUICK_LOGOUT_HOLD_MS = 3600000;

std::string GenerateRndBotName()
{
    // Pronounceable 2-3 syllable fantasy names (consonant onset + vowel nucleus,
    // optional coda). Letters only, first upper -> passes CheckPlayerName.
    // Uniqueness via DB (GetPlayerGuidByName + core NAME_IN_USE) with retry.
    static const char* onset[] = {
        "b","br","c","cr","d","dr","f","g","gr","h","j","k","kr","l","m","n",
        "p","r","s","sh","st","t","th","tr","v","w","z","kh","gh","mor","thar","zar"
    };
    static const char* nucleus[] = { "a","e","i","o","u","ae","ar","or","ur","al","yr" };
    static const char* coda[]    = { "","","","n","r","k","l","th","sh","g","rn" };
    uint32 const nOn = sizeof(onset)   / sizeof(onset[0]);
    uint32 const nNu = sizeof(nucleus) / sizeof(nucleus[0]);
    uint32 const nCo = sizeof(coda)    / sizeof(coda[0]);
    uint32 const syllables = urand(2, 3);
    std::string name;
    name.reserve(12);
    for (uint32 i = 0; i < syllables; ++i)
    {
        name += onset[urand(0, nOn - 1)];
        name += nucleus[urand(0, nNu - 1)];
    }
    name += coda[urand(0, nCo - 1)];
    if (name.size() > 11)
        name.resize(11);
    name[0] = char(name[0] & ~0x20); // uppercase first letter, ASCII a-z
    return name;
}

std::string GenerateRandomPassword()
{
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";
    constexpr size_t kLen = 12;
    constexpr size_t kCharsetSize = sizeof(charset) - 1;
    static_assert(kLen <= MAX_ACCOUNT_STR, "password must fit MAX_ACCOUNT_STR");
    // Never log/store plaintext outside immediate AccountMgr::CreateAccount call;
    // core hashes via CalculateShaPassHash. Prefer secure OS/stdlib RNG over game urand.
    std::string pw;
    pw.reserve(kLen);
    try
    {
        std::random_device rd;
        std::uniform_int_distribution<size_t> dist(0, kCharsetSize - 1);
        for (size_t i = 0; i < kLen; ++i)
            pw.push_back(charset[dist(rd)]);
        if (pw.size() == kLen)
            return pw;
    }
    catch (...)
    {
    }
    // Safe bounded fallback: game MTRand, still within length/charset bounds. This used to
    // try OpenSSL's RAND_bytes first, as a middle tier between std::random_device and this -
    // removed as a real external dependency (dynamically linked, not header-only) for a case
    // std::random_device essentially never hits in practice on any real OS: the catch(...)
    // above is defensive, not something normal operation ever reaches, and this fallback was
    // already "safe" on its own before RAND_bytes existed here. Confirmed via dumpbin
    // /imports against a real Windows build: this was the sole reason mangosd.exe needed
    // libcrypto-3-x64.dll at all once the engine side of tortoise-wow#496 landed - the old
    // __has_include(<openssl/rand.h>) guard only checked that some OpenSSL's headers were
    // found, never that the specific OpenSSL a given core links against actually exports
    // RAND_bytes, and tortoise-wow's own bundled Windows OpenSSL 1.1 does not.
    pw.clear();
    for (size_t i = 0; i < kLen; ++i)
        pw.push_back(charset[urand(0, uint32(kCharsetSize - 1))]);
    return pw;
}
} // namespace

RandomBotService& RandomBotService::Instance()
{
    static RandomBotService instance;
    return instance;
}

bool RandomBotService::IsPoolAvailable() const
{
    // A pool operation needs both an authoritative registry and no reset in
    // progress. Without validated registry rows the module cannot tell its own
    // accounts from anybody else's, so nothing may create, hire, or log in a
    // pool character.
    return RandomBotAccountRegistry::Instance().IsValidated() && RandomBotPoolReset::Instance().IsPoolAvailable();
}

void RandomBotService::WarnAboutUnregisteredPrefixAccounts()
{
    // Legacy installations may hold hand-made accounts that look like pool
    // accounts. They are not module-owned until an administrator adopts them
    // explicitly, and startup only reports them.
    std::vector<std::string> unregistered = RandomBotAccountRegistry::Instance().UnregisteredPrefixMatches();
    if (unregistered.empty())
        return;

    std::string names;
    size_t shown = std::min<size_t>(unregistered.size(), 10);
    for (size_t i = 0; i < shown; ++i)
        names += (i ? ", " : "") + unregistered[i];
    if (unregistered.size() > shown)
        names += ", ...";

    sLog.outError("TortoiseBots: %u account(s) match the random-bot prefix but are NOT managed pool accounts (%s). "
        "They are ignored by the pool and will never be reset. Run 'bot pool adopt preview' at the server console "
        "to review them, then 'bot pool adopt confirm <challenge>' to enroll them.",
        static_cast<uint32>(unregistered.size()), names.c_str());
}

bool RandomBotService::RegisterPoolAccount(uint32_t accountId, std::string const& username, RegistrationSource source)
{
    // The registry row is written and read back before the account is used for
    // anything. An account the module cannot prove it registered is never
    // treated as pool infrastructure.
    RegisterResult result = RandomBotAccountRegistry::Instance().RegisterCreatedAccount(accountId, username, source);
    if (result == RegisterResult::Success || result == RegisterResult::AlreadyRegistered)
    {
        if (std::find(m_rndBotAccountIds.begin(), m_rndBotAccountIds.end(), accountId) == m_rndBotAccountIds.end())
            m_rndBotAccountIds.push_back(accountId);
        return true;
    }

    sLog.outError("TortoiseBots: account %u ('%s') could not be registered as a managed pool account (result %u); "
        "no character will be created on it", accountId, NormalizeAccountUsername(username).c_str(), uint32(result));
    return false;
}

void RandomBotService::DrivePoolReset(uint32_t diff)
{
    RandomBotPoolReset& reset = RandomBotPoolReset::Instance();
    if (!reset.IsActive())
        return;

    PoolResetPhase before = reset.Phase();
    reset.Update(diff);
    PoolResetPhase after = reset.Phase();
    if (after == before)
        return;

    if (after == PoolResetPhase::Complete)
    {
        // The pool is empty now: reload the (unchanged) account list and let
        // the normal bounded auto-create refill it toward the configured
        // target.
        LoadCandidates();
        if (sPlayerbotAIConfig.randomBotAutoCreate)
            m_targetCount = DesiredTargetCount();
        else
            m_targetCount = TargetCount();
        m_pinnedGuids.clear();
        m_pinnedResolved = false;
        m_failedAutoCreateAccounts.clear();
        m_freshAutoCreateDisabled = false;
        m_charCreateErrorNextRetry = 0;
        m_accountAllocNextRetry = 0;
        TB_LOG_BASIC("TortoiseBots: random pool rebuilt from %u managed accounts (target %u)",
            static_cast<uint32>(m_rndBotAccountIds.size()), m_targetCount);
    }
    else if (after == PoolResetPhase::Failed)
    {
        sLog.outError("TortoiseBots: random pool reset stopped: %s", reset.LastFailure().c_str());
    }
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
    m_autoCreateNoValidData = false;
    m_accountAllocNextRetry = 0;
    m_charCreateErrorNextRetry = 0;
    m_pendingAccountName.clear();
    m_pendingNextRetry = 0;
    m_pendingSince = 0;
    m_pendingStaleLogged = false;

    // Issue #265: read the recorded generation and plan the startup reset
    // before the service's own gates, so `bot pool status` reports the stored
    // generation accurately even on a server that runs with the random-bot
    // service switched off. Planning schedules nothing without a validated
    // registry, so a disabled service cannot leave a reset half-started.
    RandomBotPoolReset::Instance().PlanAtStartup(sPlayerbotAIConfig.randomBotPoolReset, sPlayerbotAIConfig.randomBotAutoCreate);

    if (!sPlayerbotAIConfig.enabled)
    {
        TB_LOG_BASIC("TortoiseBots: native random-bot service disabled by configuration");
        return;
    }
    // Load pool even if autologin is off when auto-create is on: the deficit
    // check needs the real candidate set.
    if (!sPlayerbotAIConfig.randomBotAutologin && !sPlayerbotAIConfig.randomBotAutoCreate)
    {
        // A configured rebuild must not disappear silently when the service is
        // switched off.
        PoolResetSetting setting = ParsePoolResetSetting(sPlayerbotAIConfig.randomBotPoolReset);
        if (setting.mode == PoolResetMode::Once || setting.mode == PoolResetMode::Always)
            sLog.outError("TortoiseBots: AiPlayerbot.RandomBotPoolReset requests a pool rebuild, but the random-bot "
                "service is disabled (RandomBotAutologin=0 and RandomBotAutoCreate=0); no reset was scheduled");
        TB_LOG_BASIC("TortoiseBots: native random-bot service disabled by configuration");
        return;
    }
    if (!RandomBotAccountRegistry::Instance().IsValidated())
    {
        // Without a validated registry the module cannot tell its own accounts
        // from anybody else's, so the pool stays empty instead of falling back
        // to prefix identity.
        sLog.outError("TortoiseBots: managed-account registry unavailable (%s); random-bot pool stays empty until it can be read",
            RandomBotAccountRegistry::Instance().LastError().c_str());
        return;
    }

    WarnAboutUnregisteredPrefixAccounts();

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

    TB_LOG_BASIC("TortoiseBots: native random-bot pool loaded (%u candidates, target %u, startup %u, autoCreate %u)",
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

    // Issue #265: the pool is exactly the managed-account registry. No prefix
    // query decides what is a bot account, so a personal account that merely
    // looks like one is never loaded, hired, or reset.
    RandomBotAccountRegistry& registry = RandomBotAccountRegistry::Instance();
    if (!registry.IsValidated())
    {
        sLog.outError("TortoiseBots: managed-account registry is not validated; refusing to load a random-bot pool");
        return;
    }

    std::set<uint32> accountIds(registry.AccountIds().begin(), registry.AccountIds().end());
    m_rndBotAccountIds.assign(accountIds.begin(), accountIds.end());

    // Hire characters live on pool accounts but are never pool residents:
    // while hired they belong to their master, and when the hire ends they are
    // deleted. The durable hire ledger is that marker. Excluding them keeps a
    // dismissed companion out of the roaming world for the few ticks its row
    // still exists (and out of the account character count the auto-create
    // pass uses), instead of handing a master-geared character back to the
    // pool.
    std::set<uint32> hiredCharacters;
    {
        std::unique_ptr<QueryResult> ledger(CharacterDatabase.PQuery(
            "SELECT `character_guid` FROM `tortoise_bots_hire`"));
        if (ledger)
        {
            do
            {
                hiredCharacters.insert(ledger->Fetch()[0].GetUInt32());
            } while (ledger->NextRow());
        }
    }

    std::set<uint32> characterIds;
    for (uint32 accountId : accountIds)
    {
        std::unique_ptr<QueryResult> characters(CharacterDatabase.PQuery(
            "SELECT guid, level, race FROM characters WHERE account = '%u' ORDER BY guid", accountId));
        if (!characters)
            continue;

        do
        {
            Field* fields = characters->Fetch();
            uint32 guidLow = fields[0].GetUInt32();
            if (!guidLow || !characterIds.insert(guidLow).second)
                continue;
            if (hiredCharacters.count(guidLow))
                continue;

            Candidate candidate;
            candidate.accountId = accountId;
            candidate.characterGuid = ObjectGuid(HIGHGUID_PLAYER, guidLow);
            candidate.level = std::max<uint8_t>(1, fields[1].GetUInt8());
            candidate.team = Player::TeamForRace(fields[2].GetUInt8());
            m_candidates.push_back(candidate);
        } while (characters->NextRow());
    }

    // Rotate the stable DB order so the first startup does not always select
    // the same low GUIDs. The candidate pool is seeded from the DB at startup
    // and grows only via bounded auto-create appends; there is no per-tick
    // query or full-world scan.
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
    if (m_charCreateErrorNextRetry && time(nullptr) < m_charCreateErrorNextRetry)
        return AutoCreateCharResult::TransientError;

    // Up to 5 name attempts per account per cadence. Transient name collisions
    // (NAME_IN_USE/RESERVED/PROFANE/CHAR_CREATE_FAILED or local normalize miss) are
    // retried silently and do not mark the account permanently failed. Permanent
    // materialization errors (ACCOUNT/SERVER_LIMIT or other) are logged once and
    // the caller must remember the account as permanently failed so it is never
    // retried every RandomBotUpdateInterval. CHAR_CREATE_DISABLED and
    // CHAR_CREATE_PVP_TEAMS_VIOLATION are treated as transient 60s backoff
    // (dynamic creation-disabled/faction-balance, not permanent): core result
    // does not distinguish static NOT_PLAYABLE from dynamic IsFactionImbalanced /
    // CHARACTERS_CREATING_DISABLED, and validAll is already DBC/PlayerInfo and
    // team-filtered, so remaining DISABLED/PVP is likely transient server state
    // that must not permanently poison a healthy account (bounded via
    // m_charCreateErrorNextRetry).
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
            if (!m_autoCreateNoValidData)
            {
                sLog.outError("TortoiseBots: auto-create race %u class %u has no PlayerInfo (playercreateinfo), disabling for this process", uint32(race), uint32(cls));
                m_autoCreateNoValidData = true;
            }
            return AutoCreateCharResult::Permanent;
        }

        CharacterCreateOutcome outcome = CharacterCreation::CreateCharacter(accountId, info);
        if (outcome.result == CHAR_CREATE_SUCCESS)
        {
            if (race == RACE_GOBLIN)
            {
                CharacterDatabase.PExecute(
                    "UPDATE characters SET map = 1, zone = 14, position_x = -618.518, position_y = -4251.67, position_z = 38.718, orientation = 0 WHERE guid = '%u'",
                    outcome.guid.GetCounter());
                CharacterDatabase.PExecute(
                    "REPLACE INTO character_homebind (guid, map, zone, position_x, position_y, position_z) VALUES ('%u', 1, 14, -618.518, -4251.67, 38.718)",
                    outcome.guid.GetCounter());
            }
            else if (race == RACE_HIGH_ELF)
            {
                CharacterDatabase.PExecute(
                    "UPDATE characters SET map = 0, zone = 12, position_x = -8949.95, position_y = -132.493, position_z = 83.5312, orientation = 0 WHERE guid = '%u'",
                    outcome.guid.GetCounter());
                CharacterDatabase.PExecute(
                    "REPLACE INTO character_homebind (guid, map, zone, position_x, position_y, position_z) VALUES ('%u', 0, 12, -8949.95, -132.493, 83.5312)",
                    outcome.guid.GetCounter());
            }

            m_candidates.push_back({accountId, outcome.guid});
            m_ageMs.push_back(0);
            m_strategyAgeMs.push_back(0);
            m_randomizeAgeMs.push_back(0);
            TB_LOG_DETAIL("TortoiseBots: auto-create created character %s (%s) race %u class %u on account %u",
                norm.c_str(), outcome.guid.GetString().c_str(), uint32(race), uint32(cls), accountId);
            return AutoCreateCharResult::Success;
        }

        if (outcome.result == CHAR_CREATE_NAME_IN_USE || outcome.result == CHAR_NAME_RESERVED || outcome.result == CHAR_NAME_PROFANE || outcome.result == CHAR_CREATE_FAILED)
        {
            // Transient name candidate failure – try another name silently.
            // CHAR_NAME_PROFANE is a name-candidate rejection (profane regex) like
            // RESERVED/NAME_IN_USE; CHAR_CREATE_FAILED is DBC-missing. validAll is
            // pre-filtered with ChrRaces/ChrClasses so FAILED is rare – treat as
            // name-like transient to avoid permanently excluding a healthy account.
            continue;
        }
        if (outcome.result == CHAR_CREATE_ERROR)
        {
            if (!m_charCreateErrorNextRetry || time(nullptr) >= m_charCreateErrorNextRetry)
            {
                sLog.outError("TortoiseBots: auto-create transient failure (CHAR_CREATE_ERROR) for %s race %u class %u on account %u, backing off", norm.c_str(), uint32(race), uint32(cls), accountId);
                m_charCreateErrorNextRetry = time(nullptr) + 60;
            }
            return AutoCreateCharResult::TransientError;
        }
        if (outcome.result == CHAR_CREATE_PVP_TEAMS_VIOLATION)
        {
            // Dynamic faction-balance / ALLOW_TWO_SIDE state: transient backoff, not permanent.
            // validAll is team-filtered and mixed accounts are already excluded, so PVP is likely
            // transient server ALLOW_TWO_SIDE/faction state. Use bounded 60s backoff to avoid poisoning.
            if (!m_charCreateErrorNextRetry || time(nullptr) >= m_charCreateErrorNextRetry)
            {
                sLog.outError("TortoiseBots: auto-create transient PVP team violation for account %u race %u (faction balance), backing off", accountId, uint32(race));
                m_charCreateErrorNextRetry = time(nullptr) + 60;
            }
            return AutoCreateCharResult::TransientError;
        }
        if (outcome.result == CHAR_CREATE_ACCOUNT_LIMIT || outcome.result == CHAR_CREATE_SERVER_LIMIT)
        {
            TB_LOG_DETAIL("TortoiseBots: auto-create account %u at limit (%u), excluding from auto-create", accountId, uint32(outcome.result));
            return AutoCreateCharResult::Permanent;
        }
        if (outcome.result == CHAR_CREATE_DISABLED)
        {
            // Dynamic creation-disabled / faction-imbalance state: transient backoff, not permanent.
            // Core does not distinguish static NOT_PLAYABLE from dynamic IsFactionImbalanced /
            // CHARACTERS_CREATING_DISABLED mask; validAll already excludes NOT_PLAYABLE via DBC,
            // so remaining DISABLED is likely transient balance/disabled state.
            if (!m_charCreateErrorNextRetry || time(nullptr) >= m_charCreateErrorNextRetry)
            {
                sLog.outError("TortoiseBots: auto-create transient disabled race %u class %u (creation disabled/faction balance) for account %u, backing off", uint32(race), uint32(cls), accountId);
                m_charCreateErrorNextRetry = time(nullptr) + 60;
            }
            return AutoCreateCharResult::TransientError;
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
    // No account allocation or character creation while the pool is being
    // reset: new characters would be deleted by the running snapshot.
    if (!IsPoolAvailable())
        return false;

    // Snapshot DesiredTargetCount once at Initialize when auto-create is
    // enabled; repeated cadence calls must not re-roll time()%range or ratchet
    // m_targetCount toward MaxRandomBots. m_targetCount is the stable target.
    // Handles bounds via Desired at Initialize and deficit via size check below;
    // non-auto path still uses TargetCount() snapshot in Initialize.
    uint32 desired = m_targetCount;
    if (!desired)
        return false;
    if (m_candidates.size() >= desired)
        return false;

    // One bounded attempt per cadence (caller is throttled to
    // RandomBotUpdateInterval, >=1s). No per-tick LIKE scan.

    uint32 perAccountLimit = sWorld.getConfig(CONFIG_UINT32_CHARACTERS_PER_ACCOUNT);
    if (!perAccountLimit)
        perAccountLimit = 10;
    uint32 perRealmLimit = sWorld.getConfig(CONFIG_UINT32_CHARACTERS_PER_REALM);
    if (!perRealmLimit)
        perRealmLimit = 50;
    uint32 accountLimit = std::min(perAccountLimit, perRealmLimit);

    if (m_autoCreateNoValidData)
        return false;
    if (m_charCreateErrorNextRetry && time(nullptr) < m_charCreateErrorNextRetry)
        return false;

    std::vector<std::pair<uint8_t,uint8_t>> validAll;
    validAll.reserve(64);
    for (uint32_t race = 1; race < MAX_RACES; ++race)
    {
        ChrRacesEntry const* raceEntry = sChrRacesStore.LookupEntry(race);
        if (!raceEntry || raceEntry->HasFlag(CHRRACES_FLAGS_NOT_PLAYABLE))
            continue;
        for (uint32_t cls = 1; cls < MAX_CLASSES; ++cls)
        {
            ChrClassesEntry const* classEntry = sChrClassesStore.LookupEntry(cls);
            if (!classEntry)
                continue;
            if (!sObjectMgr.GetPlayerInfo(race, cls))
                continue;
            validAll.emplace_back(uint8_t(race), uint8_t(cls));
        }
    }
    if (validAll.empty())
    {
        if (!m_autoCreateNoValidData)
        {
            sLog.outError("TortoiseBots: auto-create no valid race/class (DBC/PlayerInfo missing), disabling for this process");
            m_autoCreateNoValidData = true;
        }
        return false;
    }

    bool allowTwoSide = sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_ACCOUNTS) != 0;

    // Pending-account handling for LoginDatabase async AccountMgr::CreateAccount
    // (LoginDatabase queues INSERT after AllowAsyncTransactions, separate from core PR #416): keep
    // exactly one pending fresh account name after AOR_OK with no visible id,
    // retry that same name with bounded/log-throttled cadence (60s) while
    // continuing the existing-account selection path and without allocating
    // another fresh account; log once after prolonged unresolved period (~300s).
    time_t now = time(nullptr);
    if (!m_pendingAccountName.empty())
    {
        bool shouldRetry = !m_pendingNextRetry || now >= m_pendingNextRetry;
        if (shouldRetry)
        {
            uint32 pendingId = sAccountMgr.GetId(m_pendingAccountName);
            if (pendingId)
            {
                std::string resolvedName = m_pendingAccountName;
                m_pendingAccountName.clear();
                m_pendingSince = 0;
                m_pendingNextRetry = 0;
                m_pendingStaleLogged = false;
                m_accountAllocNextRetry = 0;
                if (!RegisterPoolAccount(pendingId, resolvedName, RegistrationSource::AutoCreate))
                {
                    m_accountAllocNextRetry = time(nullptr) + 60;
                    return false;
                }
                AutoCreateCharResult pendingRes = TryCreateCharacterOnAccount(pendingId, validAll);
                if (pendingRes == AutoCreateCharResult::Success)
                    return true;
                if (pendingRes == AutoCreateCharResult::TransientError)
                    return false;
                if (pendingRes == AutoCreateCharResult::Permanent)
                {
                    m_failedAutoCreateAccounts.insert(pendingId);
                    m_freshAutoCreateDisabled = true;
                    sLog.outError("TortoiseBots: auto-create pending account %s (%u) permanently failed, disabling further fresh RNDBOT account allocation for this process (existing accounts remain eligible)",
                        resolvedName.c_str(), pendingId);
                    return false;
                }
                return false;
            }
            m_pendingNextRetry = now + 1;
            if (!m_pendingStaleLogged && m_pendingSince && now - m_pendingSince >= 300)
            {
                sLog.outError("TortoiseBots: auto-create pending account %s unresolved for %ld seconds, continuing with existing accounts (one pending kept, no new allocation)",
                    m_pendingAccountName.c_str(), long(now - m_pendingSince));
                m_pendingStaleLogged = true;
            }
        }
        else if (!m_pendingStaleLogged && m_pendingSince && now - m_pendingSince >= 300)
        {
            sLog.outError("TortoiseBots: auto-create pending account %s unresolved for %ld seconds, continuing with existing accounts (one pending kept, no new allocation)",
                m_pendingAccountName.c_str(), long(now - m_pendingSince));
            m_pendingStaleLogged = true;
        }
        // Unresolved pending does not freeze existing-account creation; fall
        // through to the existing-account selection path below. Fresh-account
        // allocation is still blocked while pending remains.
    }

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
        if (isMixed && !allowTwoSide)
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
        if (res == AutoCreateCharResult::TransientError)
            return false;
        // TransientName – try next account same cadence, do not remember.
    }

    // Do not allocate another fresh account while one pending remains unresolved.
    if (!m_pendingAccountName.empty())
        return false;
    if (m_freshAutoCreateDisabled)
        return false;
    if (m_accountAllocNextRetry && time(nullptr) < m_accountAllocNextRetry)
        return false;

    std::string prefix = sPlayerbotAIConfig.randomBotAccountPrefix;
    if (prefix.empty())
        prefix = "RNDBOT";

    constexpr size_t kSuffixDigits = 6; // fixed-width "000000".."999999" so long prefix cannot make every suffix identical after truncation
    std::string safePrefix = prefix.substr(0, MAX_ACCOUNT_STR > kSuffixDigits ? MAX_ACCOUNT_STR - kSuffixDigits : 0);

    uint32 newAccountId = 0;
    std::string newUsername;
    bool hadAllocDbError = false;
    for (int attempt = 0; attempt < 20 && !newAccountId; ++attempt)
    {
        uint32 suffix = urand(1000, 999999);
        char suffixBuf[16];
        std::snprintf(suffixBuf, sizeof(suffixBuf), "%06u", suffix);
        std::string username = safePrefix + suffixBuf;
        AccountMgr::normalizeString(username);
        if (sAccountMgr.GetId(username) != 0)
            continue;
        std::string password = GenerateRandomPassword();
        AccountOpResult res = sAccountMgr.CreateAccount(username, password);
        if (res == AOR_OK)
        {
            uint32 id = sAccountMgr.GetId(username);
            if (id)
            {
                // The account enters the registry before any character is
                // created on it; without a readable registry row it is not
                // used at all (the empty login account is harmless).
                if (!RegisterPoolAccount(id, username, RegistrationSource::AutoCreate))
                {
                    m_accountAllocNextRetry = time(nullptr) + 60;
                    return false;
                }
                newAccountId = id;
                newUsername = username;
                TB_LOG_DETAIL("TortoiseBots: auto-create created RNDBOT account %s (%u)", username.c_str(), id);
            }
            else
            {
                // Async LoginDatabase INSERT (AllowAsyncTransactions; separate from core PR #416):
                // AccountMgr queues the INSERT, so GetId may still return 0
                // on this world thread. Remember exactly one pending name,
                // retry it with bounded/log-throttled cadence while continuing
                // existing-account creation, and never allocate another fresh
                // account or spin. Log once after prolonged unresolved period.
                m_pendingAccountName = username;
                m_pendingSince = time(nullptr);
                m_pendingNextRetry = m_pendingSince + 1;
                m_pendingStaleLogged = false;
                return false;
            }
        }
        else if (res == AOR_NAME_ALREDY_EXIST)
        {
            continue;
        }
        else
        {
            hadAllocDbError = true;
            continue;
        }
    }
    if (!newAccountId)
    {
        if (!m_accountAllocNextRetry || time(nullptr) >= m_accountAllocNextRetry)
        {
            if (hadAllocDbError)
                sLog.outError("TortoiseBots: auto-create could not allocate RNDBOT account after attempts (LoginDatabase failure), backing off");
            else
                sLog.outError("TortoiseBots: auto-create could not allocate RNDBOT account after attempts, backing off");
            m_accountAllocNextRetry = time(nullptr) + 60;
        }
        return false;
    }
    m_accountAllocNextRetry = 0;

    AutoCreateCharResult freshRes = TryCreateCharacterOnAccount(newAccountId, validAll);
    if (freshRes == AutoCreateCharResult::Success)
        return true;
    if (freshRes == AutoCreateCharResult::TransientError)
        return false;
    if (freshRes == AutoCreateCharResult::Permanent)
    {
        m_failedAutoCreateAccounts.insert(newAccountId);
        m_freshAutoCreateDisabled = true;
        sLog.outError("TortoiseBots: auto-create fresh account %s (%u) permanently failed, disabling further fresh RNDBOT account allocation for this process (existing accounts remain eligible)", newUsername.c_str(), newAccountId);
        return false;
    }
    return false;
}

void RandomBotService::ResolvePinnedBots()
{
    if (m_pinnedResolved)
        return;
    if (!IsPoolAvailable())
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
            TB_LOG_DETAIL("TortoiseBots: pinned bot '%s' was not found or the lookup failed (no retry until restart)", rawName.c_str());
            continue;
        }
        Field* fields = result->Fetch();
        uint32 guidLow = fields[0].GetUInt32();
        if (!guidLow)
        {
            TB_LOG_DETAIL("TortoiseBots: pinned bot '%s' resolved to invalid guid", rawName.c_str());
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
            TB_LOG_DETAIL("TortoiseBots: pinned bot '%s' (guid %u) not in RNDBOT pool, ignoring", rawName.c_str(), guidLow);
            continue;
        }
        m_pinnedGuids.insert(guidLow);
        TB_LOG_DETAIL("TortoiseBots: pinned bot '%s' resolved to guid %u", rawName.c_str(), guidLow);
    }

    m_pinnedResolved = true;
    if (m_pinnedGuids.empty())
        TB_LOG_BASIC("TortoiseBots: no pinned bots resolved from %u configured names", static_cast<uint32>(sPlayerbotAIConfig.pinnedBotNames.size()));
    else
        TB_LOG_BASIC("TortoiseBots: %u pinned bot(s) cached", static_cast<uint32>(m_pinnedGuids.size()));
}

void RandomBotService::OnHumanLogin()
{
    ++m_humanSessions;
    if (m_initialized && sPlayerbotAIConfig.randomBotAutologin && !m_started)
    {
        m_started = true;
        TB_LOG_BASIC("TortoiseBots: native random-bot service started after a human login");
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
        if (m_wasBot.size() != m_candidates.size())
            m_wasBot.resize(m_candidates.size(), 0);
        if (!BotManager::Instance().IsRandomBot(candidate.characterGuid))
        {
            // Was a bot last interval and is gone within a minute of entering the world (or
            // never entered it): whatever threw it out - a saved stun, a bad spot, a refused
            // login - will do so again, so the level ladder does not pick it again for an
            // hour instead of every interval.
            if (sPlayerbotAIConfig.levelLadder && m_wasBot[i] && m_ageMs[i] < 60000)
            {
                if (m_ladderRetryMs.size() != m_candidates.size())
                    m_ladderRetryMs.resize(m_candidates.size(), 0);
                uint32_t nowMs = WorldTimer::getMSTime();
                m_ladderRetryMs[i] = nowMs + LADDER_QUICK_LOGOUT_HOLD_MS;
                ++m_quickLogouts;
                if (nowMs - m_quickLogoutLogMs >= 60000)
                {
                    m_quickLogoutLogMs = nowMs;
                    TB_LOG_BASIC("TortoiseBots: level ladder: %s (level %u) left %u ms after entering the world - set aside for an hour (%u such quick logouts so far)",
                        candidate.characterGuid.GetString().c_str(), uint32(candidate.level), m_ageMs[i], m_quickLogouts);
                }
            }
            m_wasBot[i] = 0;
            m_ageMs[i] = 0;
            continue;
        }
        m_wasBot[i] = 1;

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

        // Issue #192: hired companions ride the RNDBOT account pool but are
        // master-owned with durable ownership. The random reap must never log
        // them off mid-party; HireLifecycle owns their dismissal.
        if (HireLifecycle::Instance().IsHired(candidate.characterGuid))
        {
            m_ageMs[i] = 0;
            continue;
        }
        // A dungeon crew is not logged out under its run.
        if (BotActivityLeaseManager::Instance().GetActivity(candidate.characterGuid.GetCounter()) == BotActivity::Dungeon)
            continue;

        m_ageMs[i] += diff;
        if (!sPlayerbotAIConfig.randomBotTimedLogout || !sPlayerbotAIConfig.maxRandomBotInWorldTime)
            continue;

        uint64_t limit = static_cast<uint64_t>(sPlayerbotAIConfig.maxRandomBotInWorldTime) * 1000;
        if (m_ageMs[i] < limit)
            continue;

        TB_LOG_DETAIL("TortoiseBots: native random bot %s reached its online lifetime; removing",
            candidate.characterGuid.GetString().c_str());
        BotManager::Instance().RemoveBot(candidate.characterGuid, true);
        BotActivityLeaseManager::Instance().Release(candidate.characterGuid.GetCounter(), BotActivity::Grinding);
        // A deliberate timed logout is not a quick logout: the bot was online for its whole
        // lifetime and is expected back. Clearing the age (so the next login counts from
        // zero) while leaving `was a bot` set made the next pass read it as "left 0 ms after
        // entering the world" and set the character aside for an hour - on 2026-09-30 that
        // parked 105 of 500 bots outside the world (level ladder pass: online 395/500,
        // offline eligible 2). Clear both, so only a genuine quick logout is held back.
        m_wasBot[i] = 0;
        m_ageMs[i] = 0;
    }
}

// ---- Level ladder ----------------------------------------------------------
// The levels are cut into bands of LevelLadderBandSize (1-5, 6-10, ..., 56-59) plus level 60
// on its own. Every low band gets an equal share of the online target, level 60 gets
// LevelLadderMaxLevelShare percent of it; that share is a hard cap for the 60s, the rest of
// them stays offline as a reserve for other uses of the characters.
uint32_t RandomBotService::LadderBandCount() const
{
    uint32_t size = std::max<uint32_t>(1, sPlayerbotAIConfig.levelLadderBandSize);
    return (59 + size - 1) / size + 1;
}

uint32_t RandomBotService::LadderBandOf(uint32_t level) const
{
    if (level >= 60)
        return LadderBandCount() - 1;
    uint32_t size = std::max<uint32_t>(1, sPlayerbotAIConfig.levelLadderBandSize);
    return (std::max<uint32_t>(1, level) - 1) / size;
}

std::string RandomBotService::LadderBandName(uint32_t band) const
{
    if (band + 1 >= LadderBandCount())
        return "60";
    uint32_t size = std::max<uint32_t>(1, sPlayerbotAIConfig.levelLadderBandSize);
    uint32_t lo = band * size + 1, hi = std::min<uint32_t>(59, lo + size - 1);
    return std::to_string(lo) + "-" + std::to_string(hi);
}

uint32_t RandomBotService::LadderBandTarget(uint32_t band) const
{
    uint32_t bands = LadderBandCount();
    uint32_t sixty = m_targetCount * std::min<uint32_t>(100, sPlayerbotAIConfig.levelLadderMaxLevelSharePct) / 100;
    if (band + 1 >= bands)
        return sixty;
    uint32_t rest = m_targetCount - sixty, low = bands - 1;
    return rest / low + (band < rest % low ? 1 : 0);
}

// One pass = one sorted list per band of the offline candidates (highest level first, ring
// order among equals so they take turns), built once per service interval. The first version
// rescanned all candidates for every pick, which is quadratic when the target stays short
// (a slow login pipeline) - measured as a world tick of 120 ms instead of 50 at 300 bots.
void RandomBotService::LadderBuildBuckets(std::vector<std::vector<size_t>>& buckets, uint32_t nowMs,
    LadderSkips& skips)
{
    size_t n = m_candidates.size();
    buckets.assign(LadderBandCount(), std::vector<size_t>());
    for (size_t k = 0; k < n; ++k)
    {
        size_t i = (m_nextCandidate + k) % n;
        uint32_t const heldUntil = i < m_ladderRetryMs.size() ? m_ladderRetryMs[i] : 0;
        if (heldUntil && nowMs < heldUntil)
        {
            if (heldUntil - nowMs <= LADDER_QUICK_LOGOUT_HOLD_MS)
            {
                ++skips.held;
                continue;
            }
            // A hold further into the future than the longest one this pass ever arms is not
            // a hold: the deadline is wall-clock derived (WorldTimer::getMSTime), so a clock
            // step makes it jump, and a candidate set aside with such a value is never picked
            // again - it is silently lost from the pool (2026-09-30: 103 of the 105 bots held
            // after a timed logout never came back, `offline eligible 2` with 105 offline).
            // Repair the value and pick the candidate in this same pass.
            m_ladderRetryMs[i] = 0;
            ++skips.staleHold;
        }
        Candidate const& c = m_candidates[i];
        if (BotManager::Instance().IsBot(c.characterGuid))
        {
            ++skips.tracked;
            continue;
        }
        buckets[LadderBandOf(c.level)].push_back(i);
    }
    for (auto& bucket : buckets)
        std::stable_sort(bucket.begin(), bucket.end(), [this](size_t a, size_t b) { return m_candidates[a].level > m_candidates[b].level; });
}

// The band with the largest shortfall that still has offline characters; once every band is
// covered, the highest level across the bands (the overflow goes to the leaders). Inside a
// band the highest level, at equal level the faction with fewer bots online. A level-60
// character is only taken while its band is short (hard cap). Returns the index or -1.
int RandomBotService::LadderPickFromBuckets(std::vector<std::vector<size_t>>& buckets, std::vector<uint32_t> const& onlinePerBand, uint32_t onlineAlliance, uint32_t onlineHorde) const
{
    uint32_t bands = LadderBandCount();
    int bestBand = -1;
    int64_t bestShort = 0;
    uint32_t bestLevel = 0;
    for (uint32_t b = 0; b < bands; ++b)
    {
        if (buckets[b].empty())
            continue;
        int64_t shortfall = int64_t(LadderBandTarget(b)) - int64_t(onlinePerBand[b]);
        if (b + 1 == bands && shortfall <= 0)
            continue;
        int64_t s = std::max<int64_t>(0, shortfall);
        uint32_t level = m_candidates[buckets[b].front()].level;
        if (bestBand < 0 || s > bestShort || (s == bestShort && level > bestLevel))
        {
            bestBand = static_cast<int>(b);
            bestShort = s;
            bestLevel = level;
        }
    }
    if (bestBand < 0)
        return -1;
    std::vector<size_t>& bucket = buckets[bestBand];
    size_t pos = 0;
    uint32_t preferred = onlineHorde < onlineAlliance ? HORDE : (onlineAlliance < onlineHorde ? ALLIANCE : 0);
    if (preferred)
        for (size_t k = 0; k < bucket.size() && k < 64 && m_candidates[bucket[k]].level == bestLevel; ++k)
            if (m_candidates[bucket[k]].team == preferred)
            {
                pos = k;
                break;
            }
    size_t index = bucket[pos];
    bucket.erase(bucket.begin() + pos);
    return static_cast<int>(index);
}

void RandomBotService::LadderLog(uint32_t diff)
{
    m_ladderLogMs += diff;
    uint32_t every = std::max<uint32_t>(1, sPlayerbotAIConfig.levelLadderLogMinutes) * 60 * 1000;
    if (m_ladderLogMs < every)
        return;
    m_ladderLogMs = 0;
    uint32_t bands = LadderBandCount();
    std::vector<uint32_t> online(bands, 0), pool(bands, 0);
    uint32_t reserve = 0, maxLevel = 0;
    for (Candidate const& c : m_candidates)
    {
        uint32_t band = LadderBandOf(c.level);
        ++pool[band];
        maxLevel = std::max<uint32_t>(maxLevel, c.level);
        if (BotManager::Instance().IsRandomBot(c.characterGuid))
            ++online[band];
        else
            ++reserve;
    }
    std::string line;
    for (uint32_t b = 0; b < bands; ++b)
        line += (b ? " " : "") + LadderBandName(b) + ":" + std::to_string(online[b]) + "/" + std::to_string(LadderBandTarget(b)) + "(" + std::to_string(pool[b]) + ")";
    TB_LOG_BASIC("TortoiseBots: level ladder online/target(pool) per band %s | reserve %u, highest level %u", line.c_str(), reserve, maxLevel);
}

void RandomBotService::MaintainOnlinePool()
{
    if (!m_started || sWorld.IsShutdowning())
        return;
    if (!IsPoolAvailable())
        return;
    if (sPlayerbotAIConfig.randomBotLoginWithPlayer && !m_humanSessions)
    {
        for (Candidate const& candidate : m_candidates)
            if (BotManager::Instance().IsRandomBot(candidate.characterGuid))
            {
                BotManager::Instance().RemoveBot(candidate.characterGuid, true);
                BotActivityLeaseManager::Instance().Release(candidate.characterGuid.GetCounter(), BotActivity::Grinding);
            }
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
            if (BotSessionAdapter::GetHeadlessSessionState(pinnedCandidate->characterGuid) != HeadlessSessionState::NotFound)
                continue;
            uint32 guidLow = pinnedCandidate->characterGuid.GetCounter();
            if (!BotActivityLeaseManager::Instance().TryAcquire(guidLow, BotActivity::Grinding, 0))
                continue;

            if (BotManager::Instance().AddRandomBot(pinnedCandidate->accountId, pinnedCandidate->characterGuid))
            {
                ++online;
                ++added;
                TB_LOG_DETAIL("TortoiseBots: pinned random bot %s queued on account %u (prioritized)",
                    pinnedCandidate->characterGuid.GetString().c_str(), pinnedCandidate->accountId);
            }
            else
                BotActivityLeaseManager::Instance().Release(guidLow, BotActivity::Grinding);
        }
    }

    if (sPlayerbotAIConfig.levelLadder)
    {
        // Level ladder: instead of the ring over the whole pool, fill the level band with the
        // largest shortfall first and take the highest-level character inside it, so the field
        // spreads over the levels and the leaders keep playing until 60. One sorted pass per
        // interval; a candidate that cannot log in right now (session still closing, lease held,
        // add refused) is left alone for a minute instead of being picked again every interval.
        uint32_t const nowMs = WorldTimer::getMSTime();
        if (m_ladderRetryMs.size() != m_candidates.size())
            m_ladderRetryMs.resize(m_candidates.size(), 0);
        std::vector<uint32_t> onlinePerBand(LadderBandCount(), 0);
        uint32_t onlineAlliance = 0, onlineHorde = 0;
        for (Candidate const& c : m_candidates)
        {
            if (!BotManager::Instance().IsRandomBot(c.characterGuid))
                continue;
            ++onlinePerBand[LadderBandOf(c.level)];
            if (c.team == HORDE) ++onlineHorde; else ++onlineAlliance;
        }
        std::vector<std::vector<size_t>> buckets;
        LadderSkips skips;
        LadderBuildBuckets(buckets, nowMs, skips);
        uint32_t eligible = 0, sessionBusy = 0, leaseBusy = 0, addFailed = 0;
        for (auto const& bucket : buckets)
            eligible += static_cast<uint32_t>(bucket.size());
        size_t ladderAttempts = 0, maxAttempts = static_cast<size_t>(perInterval) * 4;
        while (online < m_targetCount && added < perInterval && ladderAttempts < maxAttempts)
        {
            ++ladderAttempts;
            int picked = LadderPickFromBuckets(buckets, onlinePerBand, onlineAlliance, onlineHorde);
            if (picked < 0)
                break;
            size_t index = static_cast<size_t>(picked);
            Candidate const& candidate = m_candidates[index];
            if (Player* player = sObjectAccessor.FindPlayer(candidate.characterGuid))
            {
                // A network session owns the character; never steal it.
                if (!player->GetSession() || !player->GetSession()->IsHeadless())
                {
                    m_ladderRetryMs[index] = nowMs + LADDER_BUSY_RETRY_MS;
                    ++sessionBusy;
                    continue;
                }
            }
            if (BotSessionAdapter::GetHeadlessSessionState(candidate.characterGuid) != HeadlessSessionState::NotFound)
            {
                m_ladderRetryMs[index] = nowMs + LADDER_BUSY_RETRY_MS;
                ++sessionBusy;
                continue;
            }
            uint32 guidLow = candidate.characterGuid.GetCounter();
            if (!BotActivityLeaseManager::Instance().TryAcquire(guidLow, BotActivity::Grinding, 0))
            {
                m_ladderRetryMs[index] = nowMs + LADDER_BUSY_RETRY_MS;
                ++leaseBusy;
                continue;
            }
            if (BotManager::Instance().AddRandomBot(candidate.accountId, candidate.characterGuid))
            {
                ++online;
                ++added;
                ++onlinePerBand[LadderBandOf(candidate.level)];
                if (candidate.team == HORDE) ++onlineHorde; else ++onlineAlliance;
                TB_LOG_DETAIL("TortoiseBots: level ladder queued random bot %s (level %u, band %s) on account %u",
                    candidate.characterGuid.GetString().c_str(), uint32(candidate.level),
                    LadderBandName(LadderBandOf(candidate.level)).c_str(), candidate.accountId);
            }
            else
            {
                BotActivityLeaseManager::Instance().Release(guidLow, BotActivity::Grinding);
                m_ladderRetryMs[index] = nowMs + LADDER_BUSY_RETRY_MS;
                ++addFailed;
            }
        }
        ++m_nextCandidate; // equals take turns across passes
        if (online < m_targetCount && nowMs - m_ladderPassLogMs >= 60000)
        {
            m_ladderPassLogMs = nowMs;
            TB_LOG_BASIC("TortoiseBots: level ladder pass: online %u/%u, added %u, offline eligible %u, skipped: session %u, lease %u, add refused %u, held %u, tracked %u, stale holds %u",
                online, m_targetCount, added, eligible, sessionBusy, leaseBusy, addFailed,
                skips.held, skips.tracked, skips.staleHold);
        }
        return;
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
        if (BotSessionAdapter::GetHeadlessSessionState(candidate.characterGuid) != HeadlessSessionState::NotFound)
            continue;

        uint32 guidLow = candidate.characterGuid.GetCounter();
        if (!BotActivityLeaseManager::Instance().TryAcquire(guidLow, BotActivity::Grinding, 0))
            continue;

        if (BotManager::Instance().AddRandomBot(candidate.accountId, candidate.characterGuid))
        {
            ++online;
            ++added;
            TB_LOG_DETAIL("TortoiseBots: native random bot %s queued on account %u",
                candidate.characterGuid.GetString().c_str(), candidate.accountId);
        }
        else
            BotActivityLeaseManager::Instance().Release(guidLow, BotActivity::Grinding);
    }
}

void RandomBotService::Update(uint32_t diff)
{
    if (!m_initialized || !sPlayerbotAIConfig.enabled)
        return;

    sRandomBotFacade.RefreshAuctionPrices(diff);

    // Issue #265: a startup reset runs before anything else touches the pool,
    // one bounded step per world tick.
    DrivePoolReset(diff);
    if (!IsPoolAvailable())
        return;

    uint32_t cadence = std::max<uint32_t>(1000, sPlayerbotAIConfig.randomBotUpdateInterval);
    m_serviceElapsedMs += diff;
    if (m_serviceElapsedMs < cadence)
        return;

    uint32_t elapsed = m_serviceElapsedMs;
    m_serviceElapsedMs = 0;

    // Bounded auto-create: up to 5 creations per cadence if progressing toward target
    if (sPlayerbotAIConfig.randomBotAutoCreate)
    {
        for (int i = 0; i < 5; ++i)
        {
            if (!TryAutoCreate())
                break;
        }
    }

    if (!sPlayerbotAIConfig.randomBotAutologin)
        return;

    if (!m_pinnedResolved)
        ResolvePinnedBots();
    RemoveExpiredBots(elapsed);
    if (sPlayerbotAIConfig.levelLadder)
        LadderLog(elapsed);

    for (size_t i = 0; i < m_candidates.size(); ++i)
    {
        Candidate const& candidate = m_candidates[i];
        BotRecord* record = BotManager::Instance().FindBot(candidate.characterGuid);
        Player* player = sObjectAccessor.FindPlayer(candidate.characterGuid);
        if (!record || !record->enteredWorld || !player)
            continue;

        // Keep the cached level current for the level ladder (a level-up while online).
        m_candidates[i].level = static_cast<uint8_t>(std::max<uint32>(1, player->GetLevel()));

        // A dungeon crew (another module holds the Dungeon lease) keeps its strategies and its
        // corpse runs: no recovery revive, no strategy roulette, no timed gear seeding.
        if (BotActivityLeaseManager::Instance().GetActivity(player->GetGUIDLow()) == BotActivity::Dungeon)
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
            // Same fresh-bot-only rule as login seeding (GearSeedingGuard.h):
            // never overwrite earned gear on a timer tick. The login path
            // normally seeds first; this is a backstop for pool bots that
            // somehow entered the world unseeded.
            uint32 timerGuidLow = player->GetGUIDLow();
            bool freshTimerBot = TortoiseBots::NeedsInitialGearSeeding(
                player->GetTotalPlayedTime(), sRandomBotFacade.GetValue(timerGuidLow, "seeded"));
            if (player->GetLevel() >= 5 && freshTimerBot)
            {
                sRandomBotFacade.UpdateGearSpells(player);
                sRandomBotFacade.SetValue(timerGuidLow, "seeded", 1);
            }
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
        BotActivityLeaseManager::Instance().Release(candidate.characterGuid.GetCounter(), BotActivity::Grinding);
    }

    m_started = false;
    m_initialized = false;
    m_pinnedGuids.clear();
    m_pinnedResolved = false;
    m_rndBotAccountIds.clear();
    m_failedAutoCreateAccounts.clear();
    m_freshAutoCreateDisabled = false;
    m_autoCreateNoValidData = false;
    m_accountAllocNextRetry = 0;
    m_charCreateErrorNextRetry = 0;
    m_pendingAccountName.clear();
    m_pendingNextRetry = 0;
    m_pendingSince = 0;
    m_pendingStaleLogged = false;
    RandomBotPoolReset::Instance().Shutdown();
}

} // namespace TortoiseBots
