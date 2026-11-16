// pi-lens-ignore-file: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access
#include "AhBotSellerAccount.h"

#include "BotNameService.h"
#include "runtime/policy/ah/AhBotSellerAccountPolicy.h"
#include "runtime/policy/provision/BotAppearancePolicy.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../host/ModuleLog.h"

#include "AccountMgr.h"
#include "Database/DBCStores.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Util.h"
#include "World.h"

#if __has_include("Handlers/CharacterCreation.h")
#include "Handlers/CharacterCreation.h"
#elif __has_include("CharacterCreation.h")
#include "CharacterCreation.h"
#else
#error "TortoiseBots AhBotSellerAccount requires core Handlers/CharacterCreation.h"
#endif

#include <algorithm>
#include <random>
#include <sstream>
#include <utility>

namespace TortoiseBots
{

static_assert(ai::kAhBotMaxAccountNameLen == MAX_ACCOUNT_STR,
    "AhBot seller account name limit drifted from MAX_ACCOUNT_STR");
static_assert(RACE_HUMAN == 1 && RACE_ORC == 2 && RACE_DWARF == 3 && RACE_NIGHTELF == 4 &&
    RACE_UNDEAD == 5 && RACE_TAUREN == 6 && RACE_GNOME == 7 && RACE_TROLL == 8 &&
    RACE_GOBLIN == 9 && RACE_HIGH_ELF == 10,
    "AhBot seller race IDs drifted from SharedDefines.h");

namespace
{

constexpr uint32_t kPendingAccountPollMs = 250;
constexpr uint32_t kPendingAccountTimeoutMs = 30000;
constexpr uint32_t kRepairBackoffMs = 10000;
constexpr size_t kMailDeleteChunkSize = 100;

std::string GenerateRandomPassword()
{
    static char const charset[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";
    constexpr size_t kLen = 12;
    constexpr size_t kCharsetSize = sizeof(charset) - 1;
    static_assert(kLen <= MAX_ACCOUNT_STR, "password must fit MAX_ACCOUNT_STR");

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

    pw.clear();
    for (size_t i = 0; i < kLen; ++i)
        pw.push_back(charset[urand(0, uint32(kCharsetSize - 1))]);
    return pw;
}

bool QueryLoginAccountId(std::string const& normalizedName, uint32_t& outAccountId, bool& outDbOk)
{
    outAccountId = 0;
    outDbOk = false;
    if (normalizedName.empty())
        return false;

    std::string escaped = normalizedName;
    LoginDatabase.escape_string(escaped);
    // COUNT(*) + MAX(id) always yields 1 row when the DB answers, separating
    // "missing account" from a failed query. Also bypasses AccountMgr's
    // m_accountNameToId cache, which DeleteAccount does not invalidate.
    std::unique_ptr<QueryResult> res(LoginDatabase.PQuery(
        "SELECT COUNT(*), COALESCE(MAX(`id`), 0) FROM `account` WHERE `username` = '%s'",
        escaped.c_str()));
    if (!res)
        return false;

    outDbOk = true;
    Field* fields = res->Fetch();
    if (fields[0].GetUInt32() > 0)
        outAccountId = fields[1].GetUInt32();
    return true;
}

std::string JoinUintList(std::vector<uint32_t> const& ids, size_t start, size_t end)
{
    std::string out;
    for (size_t i = start; i < end; ++i)
    {
        if (i > start)
            out += ',';
        out += std::to_string(ids[i]);
    }
    return out;
}

void DeleteByIdsInChunks(char const* sqlPrefix, std::vector<uint32_t> const& ids)
{
    for (size_t offset = 0; offset < ids.size(); offset += kMailDeleteChunkSize)
    {
        size_t end = std::min(offset + kMailDeleteChunkSize, ids.size());
        std::string list = JoinUintList(ids, offset, end);
        CharacterDatabase.DirectPExecute("%s (%s)", sqlPrefix, list.c_str());
    }
}

std::vector<uint8_t> ValidClassesForRace(uint8_t race)
{
    static uint8_t const kClasses[] = {
        CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_PRIEST,
        CLASS_SHAMAN, CLASS_MAGE, CLASS_WARLOCK, CLASS_DRUID
    };
    std::vector<uint8_t> result;
    for (uint8_t cls : kClasses)
    {
        if (!sChrClassesStore.LookupEntry(cls))
            continue;
        if (!sObjectMgr.GetPlayerInfo(race, cls))
            continue;
        result.push_back(cls);
    }
    return result;
}

} // namespace

AhBotSellerAccount& AhBotSellerAccount::Instance()
{
    static AhBotSellerAccount instance;
    return instance;
}

void AhBotSellerAccount::EnsureLoadedLocked() const
{
    if (m_dbLoaded)
        return;

    m_allianceGuids.clear();
    m_hordeGuids.clear();
    m_sellerGuidSet.clear();
    m_knownSellerAccounts.clear();
    m_accountId = 0;

    std::string configuredName = sPlayerbotAIConfig.ahBotAccountName;
    if (ai::IsValidSellerAccountName(configuredName, sPlayerbotAIConfig.randomBotAccountPrefix))
    {
        m_accountName = ai::NormalizeAccountNameUpper(configuredName);
        uint32_t resolvedId = 0;
        bool dbOk = false;
        if (QueryLoginAccountId(m_accountName, resolvedId, dbOk) && resolvedId != 0)
        {
            m_accountId = resolvedId;
            m_knownSellerAccounts.insert(resolvedId);
        }
    }
    else
    {
        m_accountName = ai::NormalizeAccountNameUpper(configuredName);
    }

    std::unique_ptr<QueryResult> countRes(CharacterDatabase.PQuery(
        "SELECT COUNT(*) FROM `tortoise_bots_ahbot_seller`"));
    if (!countRes)
    {
        // Mark loaded so failed read does not hammer the DB on every guard query;
        // RunRepairCheck() re-queries directly when repair runs.
        m_dbLoaded = true;
        return;
    }

    uint32_t rowCount = countRes->Fetch()[0].GetUInt32();
    if (rowCount > 0)
    {
        std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
            "SELECT `guid`, `account_id`, `faction` FROM `tortoise_bots_ahbot_seller` ORDER BY `guid`"));
        if (rows)
        {
            do
            {
                Field* fields = rows->Fetch();
                uint32_t guid = fields[0].GetUInt32();
                uint32_t accId = fields[1].GetUInt32();
                if (guid)
                    m_sellerGuidSet.insert(guid);
                if (accId)
                    m_knownSellerAccounts.insert(accId);
            } while (rows->NextRow());
        }
    }

    if (m_accountId != 0)
    {
        std::unique_ptr<QueryResult> liveRows(CharacterDatabase.PQuery(
            "SELECT c.`guid`, c.`race` FROM `characters` c "
            "INNER JOIN `tortoise_bots_ahbot_seller` s ON s.`guid` = c.`guid` "
            "WHERE c.`account` = '%u' AND c.`deleteDate` IS NULL ORDER BY c.`guid`",
            m_accountId));
        if (liveRows)
        {
            do
            {
                Field* fields = liveRows->Fetch();
                uint32_t guid = fields[0].GetUInt32();
                uint8_t race = fields[1].GetUInt8();
                ai::SellerFaction faction{};
                if (!guid || !ai::FactionForSellerRace(race, faction))
                    continue;
                if (faction == ai::SellerFaction::Alliance)
                    m_allianceGuids.push_back(guid);
                else
                    m_hordeGuids.push_back(guid);
            } while (liveRows->NextRow());
        }
    }

    m_dbLoaded = true;
}

void AhBotSellerAccount::Update(uint32_t diff)
{
    if (sWorld.IsShutdowning())
        return;

    if (!ai::IsValidSellerAccountName(sPlayerbotAIConfig.ahBotAccountName, sPlayerbotAIConfig.randomBotAccountPrefix))
    {
        if (!m_invalidNameLogged)
        {
            m_invalidNameLogged = true;
            sLog.outError("TortoiseBots: AhBot.AccountName '%s' is invalid or collides with RandomBotAccountPrefix '%s'; seller account disabled",
                sPlayerbotAIConfig.ahBotAccountName.c_str(), sPlayerbotAIConfig.randomBotAccountPrefix.c_str());
        }
        return;
    }
    m_invalidNameLogged = false;

    if (m_pendingAccountResolve)
    {
        m_pendingRetryMs += diff;
        m_pendingElapsedMs += diff;
        if (m_pendingRetryMs < kPendingAccountPollMs)
            return;
        m_pendingRetryMs = 0;

        uint32_t resolvedId = 0;
        bool dbOk = false;
        if (QueryLoginAccountId(m_pendingAccountName, resolvedId, dbOk) && resolvedId != 0)
        {
            m_pendingAccountResolve = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_accountId = resolvedId;
                m_accountName = m_pendingAccountName;
                m_knownSellerAccounts.insert(resolvedId);
            }
            m_passwordRotatedThisRun = true;
            RunRepairCheck();
            return;
        }
        if (m_pendingElapsedMs >= kPendingAccountTimeoutMs)
        {
            m_pendingAccountResolve = false;
            m_backoffMs = kRepairBackoffMs;
            sLog.outError("TortoiseBots: AhBot seller account '%s' creation timed out waiting for LoginDatabase; retrying later",
                m_pendingAccountName.c_str());
        }
        return;
    }

    if (m_backoffMs > 0)
    {
        if (diff < m_backoffMs)
        {
            m_backoffMs -= diff;
            return;
        }
        m_backoffMs = 0;
    }

    if (m_initialCheckDone)
    {
        m_repairElapsedMs += diff;
        uint32_t intervalSec = std::max<uint32_t>(10, sPlayerbotAIConfig.ahBotSellerRepairIntervalSeconds);
        if (!ai::IsSellerRepairDue(true, m_repairElapsedMs, intervalSec))
            return;
    }

    m_repairElapsedMs = 0;
    m_initialCheckDone = true;
    RunRepairCheck();
}

bool AhBotSellerAccount::CreateSellerCharacter(uint32_t accountId, uint8_t factionIndex, uint32_t& outGuid)
{
    outGuid = 0;
    ai::SellerFaction faction = factionIndex == 0 ? ai::SellerFaction::Alliance : ai::SellerFaction::Horde;

    // Pick a playable race of the target faction that has at least one valid class.
    uint8_t race = 0;
    std::vector<uint8_t> classes;
    uint32_t startRoll = urand(0, 4);
    for (uint32_t offset = 0; offset < 5; ++offset)
    {
        uint8_t candidateRace = ai::PickSellerRace(faction, startRoll + offset);
        ChrRacesEntry const* raceEntry = sChrRacesStore.LookupEntry(candidateRace);
        if (!raceEntry || raceEntry->HasFlag(CHRRACES_FLAGS_NOT_PLAYABLE))
            continue;
        classes = ValidClassesForRace(candidateRace);
        if (!classes.empty())
        {
            race = candidateRace;
            break;
        }
    }
    if (!race || classes.empty())
    {
        sLog.outError("TortoiseBots: AhBot no valid playable race/class found for %s seller", ai::SellerFactionName(faction));
        return false;
    }

    uint8_t cls = classes[urand(0, static_cast<uint32_t>(classes.size() - 1))];
    uint8_t gender = urand(0, 1) ? GENDER_FEMALE : GENDER_MALE;

    BotAppearanceCounts look{};
    if (!BotAppearanceCountsFor(race, gender, look))
        return false;

    for (int nameAttempt = 0; nameAttempt < 8; ++nameAttempt)
    {
        std::string name = PickBotCharacterName(race, cls, gender);
        if (!ai::IsValidSellerCharacterName(name))
            continue;

        CharacterCreateInfo info;
        info.name = name;
        info.race = race;
        info.class_ = cls;
        info.gender = gender;
        info.skin = static_cast<uint8_t>(urand(0, look.skin - 1));
        info.face = static_cast<uint8_t>(urand(0, look.face - 1));
        info.hairStyle = static_cast<uint8_t>(urand(0, look.hairStyle - 1));
        info.hairColor = static_cast<uint8_t>(urand(0, look.hairColor - 1));
        info.facialHair = static_cast<uint8_t>(urand(0, look.facialHair - 1));
        info.outfitId = 0;
        info.challengeMask = 0;

        CharacterCreateOutcome outcome = CharacterCreation::CreateCharacter(accountId, info);
        if (outcome.result == CHAR_CREATE_SUCCESS)
        {
            uint32_t guidLow = outcome.guid.GetCounter();
            if (!CharacterDatabase.DirectPExecute(
                    "REPLACE INTO `tortoise_bots_ahbot_seller` (`guid`, `account_id`, `faction`) "
                    "VALUES ('%u', '%u', '%s')",
                    guidLow, accountId, ai::SellerFactionName(faction)))
            {
                sLog.outError("TortoiseBots: AhBot created seller '%s' (%u) but failed to record row in tortoise_bots_ahbot_seller",
                    name.c_str(), guidLow);
            }
            outGuid = guidLow;
            TB_LOG_BASIC("TortoiseBots: AhBot created %s seller '%s' (guid %u) on account %u",
                ai::SellerFactionName(faction), name.c_str(), guidLow, accountId);
            return true;
        }

        if (outcome.result == CHAR_CREATE_NAME_IN_USE ||
            outcome.result == CHAR_NAME_RESERVED ||
            outcome.result == CHAR_NAME_PROFANE ||
            outcome.result == CHAR_CREATE_FAILED)
        {
            continue;
        }

        if (outcome.result == CHAR_CREATE_PVP_TEAMS_VIOLATION)
        {
            if (!m_pvpTeamsViolationLogged)
            {
                m_pvpTeamsViolationLogged = true;
                sLog.outError("TortoiseBots: AhBot cannot create %s seller on account %u (CHAR_CREATE_PVP_TEAMS_VIOLATION); enable AllowTwoSide.Accounts = 1 on PvP realms for cross-faction sellers",
                    ai::SellerFactionName(faction), accountId);
            }
            return false;
        }

        if (outcome.result == CHAR_CREATE_SERVER_LIMIT || outcome.result == CHAR_CREATE_ACCOUNT_LIMIT)
        {
            if (!m_charLimitLogged)
            {
                m_charLimitLogged = true;
                sLog.outError("TortoiseBots: AhBot seller account %u reached character limit (%u); lower AiPlayerbot.AhBot.SellersPerFaction or raise CharactersPerRealm",
                    accountId, static_cast<uint32_t>(outcome.result));
            }
            return false;
        }

        sLog.outError("TortoiseBots: AhBot CreateCharacter failed for %s seller on account %u (result %u)",
            ai::SellerFactionName(faction), accountId, static_cast<uint32_t>(outcome.result));
        return false;
    }

    sLog.outError("TortoiseBots: AhBot exhausted name attempts creating %s seller on account %u",
        ai::SellerFactionName(faction), accountId);
    return false;
}

void AhBotSellerAccount::RunRepairCheck()
{
    std::string normalizedName = ai::NormalizeAccountNameUpper(sPlayerbotAIConfig.ahBotAccountName);
    if (!ai::IsValidSellerAccountName(normalizedName, sPlayerbotAIConfig.randomBotAccountPrefix))
        return;

    uint32_t targetPerFaction = std::max<uint32_t>(1, sPlayerbotAIConfig.ahBotSellersPerFaction);

    if (m_accountName != normalizedName)
        m_passwordRotatedThisRun = false;

    uint32_t accountId = 0;
    bool loginDbOk = false;
    if (!QueryLoginAccountId(normalizedName, accountId, loginDbOk) || !loginDbOk)
    {
        sLog.outError("TortoiseBots: AhBot seller repair could not query LoginDatabase for account '%s'",
            normalizedName.c_str());
        m_backoffMs = kRepairBackoffMs;
        return;
    }

    // Count existing registry rows first so we detect stale rows even if the
    // account was deleted and recreated.
    std::unique_ptr<QueryResult> regCountRes(CharacterDatabase.PQuery(
        "SELECT COUNT(*) FROM `tortoise_bots_ahbot_seller`"));
    if (!regCountRes)
    {
        sLog.outError("TortoiseBots: AhBot seller repair could not read tortoise_bots_ahbot_seller");
        m_backoffMs = kRepairBackoffMs;
        return;
    }
    uint32_t regTotal = regCountRes->Fetch()[0].GetUInt32();

    if (accountId == 0)
    {
        // Account missing or deleted (.account delete). Clean up any orphaned
        // registry rows from the vanished account before creating the new one.
        if (regTotal > 0)
            CharacterDatabase.DirectPExecute("DELETE FROM `tortoise_bots_ahbot_seller`");

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_dbLoaded = true;
            m_accountId = 0;
            m_accountName = normalizedName;
            m_allianceGuids.clear();
            m_hordeGuids.clear();
            m_sellerGuidSet.clear();
        }

        std::string pw = GenerateRandomPassword();
        AccountOpResult res = sAccountMgr.CreateAccount(normalizedName, pw);
        if (res == AOR_NAME_ALREDY_EXIST)
        {
            // The login DB has no row (checked above), but the core keeps deleted
            // account names cached until restart (.account delete does not evict
            // them), so insert the row the same way CreateAccount does.
            std::string name = normalizedName;
            std::string pass = pw;
            AccountMgr::normalizeString(name);
            AccountMgr::normalizeString(pass);
            std::string hash = sAccountMgr.CalculateShaPassHash(name, pass);
            LoginDatabase.escape_string(name);
            if (LoginDatabase.PExecute("INSERT INTO account(username,sha_pass_hash,joindate) VALUES('%s','%s',NOW())",
                    name.c_str(), hash.c_str()))
            {
                LoginDatabase.Execute("REPLACE INTO realmcharacters (realmid, acctid, numchars) SELECT realmlist.id, account.id, 0 FROM realmlist,account LEFT JOIN realmcharacters ON acctid=account.id WHERE acctid IS NULL");
                res = AOR_OK;
            }
        }
        if (res != AOR_OK)
        {
            sLog.outError("TortoiseBots: AhBot could not create seller account '%s' (result %u)",
                normalizedName.c_str(), static_cast<uint32_t>(res));
            m_backoffMs = kRepairBackoffMs;
            return;
        }

        uint32_t newId = 0;
        bool checkOk = false;
        if (QueryLoginAccountId(normalizedName, newId, checkOk) && newId != 0)
        {
            accountId = newId;
            m_passwordRotatedThisRun = true;
            TB_LOG_BASIC("TortoiseBots: AhBot created seller account '%s' (id %u)", normalizedName.c_str(), accountId);
        }
        else
        {
            // AccountMgr::CreateAccount queues an async LoginDatabase INSERT;
            // poll briefly on Update() without stalling the world thread.
            m_pendingAccountResolve = true;
            m_pendingAccountName = normalizedName;
            m_pendingRetryMs = 0;
            m_pendingElapsedMs = 0;
            TB_LOG_BASIC("TortoiseBots: AhBot queued seller account '%s' creation; waiting for id",
                normalizedName.c_str());
            return;
        }
    }

    // Account exists. Rotate password once per process start so it is never
    // predictable or reused across restarts.
    if (!m_passwordRotatedThisRun)
    {
        std::string pw = GenerateRandomPassword();
        sAccountMgr.ChangePassword(accountId, pw, normalizedName);
        m_passwordRotatedThisRun = true;
    }

    // Verify active non-deleted characters on this account.
    std::unique_ptr<QueryResult> charCountRes(CharacterDatabase.PQuery(
        "SELECT COUNT(*) FROM `characters` WHERE `account` = '%u' AND `deleteDate` IS NULL", accountId));
    if (!charCountRes)
    {
        sLog.outError("TortoiseBots: AhBot seller repair could not query characters on account %u", accountId);
        m_backoffMs = kRepairBackoffMs;
        return;
    }
    uint32_t charTotal = charCountRes->Fetch()[0].GetUInt32();

    std::vector<uint32_t> liveAlliance;
    std::vector<uint32_t> liveHorde;
    std::vector<uint32_t> needRegistryBackfill;
    std::vector<ai::SellerFaction> needRegistryFaction;
    std::unordered_set<uint32_t> liveGuidSet;

    if (charTotal > 0)
    {
        std::unique_ptr<QueryResult> charRows(CharacterDatabase.PQuery(
            "SELECT c.`guid`, c.`race`, s.`account_id`, s.`faction` "
            "FROM `characters` c "
            "LEFT JOIN `tortoise_bots_ahbot_seller` s ON s.`guid` = c.`guid` "
            "WHERE c.`account` = '%u' AND c.`deleteDate` IS NULL ORDER BY c.`guid`",
            accountId));
        if (!charRows)
        {
            sLog.outError("TortoiseBots: AhBot seller repair failed reading characters on account %u", accountId);
            m_backoffMs = kRepairBackoffMs;
            return;
        }

        do
        {
            Field* fields = charRows->Fetch();
            uint32_t guid = fields[0].GetUInt32();
            uint8_t race = fields[1].GetUInt8();
            ai::SellerFaction faction{};
            if (!guid || !ai::FactionForSellerRace(race, faction))
                continue;

            liveGuidSet.insert(guid);
            if (faction == ai::SellerFaction::Alliance)
                liveAlliance.push_back(guid);
            else
                liveHorde.push_back(guid);

            bool hasRow = !fields[2].IsNULL();
            uint32_t rowAccount = hasRow ? fields[2].GetUInt32() : 0;
            std::string rowFaction = fields[3].IsNULL() ? std::string() : fields[3].GetCppString();
            if (!hasRow || rowAccount != accountId || rowFaction != ai::SellerFactionName(faction))
            {
                needRegistryBackfill.push_back(guid);
                needRegistryFaction.push_back(faction);
            }
        } while (charRows->NextRow());
    }

    for (size_t i = 0; i < needRegistryBackfill.size(); ++i)
    {
        CharacterDatabase.DirectPExecute(
            "REPLACE INTO `tortoise_bots_ahbot_seller` (`guid`, `account_id`, `faction`) "
            "VALUES ('%u', '%u', '%s')",
            needRegistryBackfill[i], accountId, ai::SellerFactionName(needRegistryFaction[i]));
    }

    // Remove registry rows pointing to soft-deleted/missing characters or an old account.
    std::vector<uint32_t> staleGuids;
    if (regTotal > 0)
    {
        std::unique_ptr<QueryResult> regRows(CharacterDatabase.PQuery(
            "SELECT `guid`, `account_id` FROM `tortoise_bots_ahbot_seller`"));
        if (regRows)
        {
            do
            {
                Field* fields = regRows->Fetch();
                uint32_t guid = fields[0].GetUInt32();
                uint32_t rowAcc = fields[1].GetUInt32();
                if (rowAcc != accountId || liveGuidSet.count(guid) == 0)
                    staleGuids.push_back(guid);
            } while (regRows->NextRow());
        }
    }
    if (!staleGuids.empty())
    {
        DeleteByIdsInChunks("DELETE FROM `tortoise_bots_ahbot_seller` WHERE `guid` IN", staleGuids);
        TB_LOG_BASIC("TortoiseBots: AhBot pruned %zu stale seller row(s) from tortoise_bots_ahbot_seller",
            staleGuids.size());
    }

    ai::SellerRepairPlan plan = ai::DecideSellerRepair(
        true,
        true,
        m_passwordRotatedThisRun,
        targetPerFaction,
        static_cast<uint32_t>(liveAlliance.size()),
        static_cast<uint32_t>(liveHorde.size()),
        !staleGuids.empty());

    for (uint32_t i = 0; i < plan.createAllianceCount; ++i)
    {
        uint32_t newGuid = 0;
        if (!CreateSellerCharacter(accountId, 0, newGuid))
            break;
        liveAlliance.push_back(newGuid);
        liveGuidSet.insert(newGuid);
    }

    for (uint32_t i = 0; i < plan.createHordeCount; ++i)
    {
        uint32_t newGuid = 0;
        if (!CreateSellerCharacter(accountId, 1, newGuid))
            break;
        liveHorde.push_back(newGuid);
        liveGuidSet.insert(newGuid);
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_dbLoaded = true;
        m_accountId = accountId;
        m_accountName = normalizedName;
        m_allianceGuids = std::move(liveAlliance);
        m_hordeGuids = std::move(liveHorde);
        // Keep m_sellerGuidSet containing all live sellers (plus any retained
        // protective entries for the current process if desired, though stale
        // deleted guids no longer exist in characters).
        for (uint32_t g : liveGuidSet)
            m_sellerGuidSet.insert(g);
        m_knownSellerAccounts.insert(accountId);
    }
}

bool AhBotSellerAccount::IsReady() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureLoadedLocked();
    return ai::IsReadyForSellers(m_accountId != 0, m_allianceGuids.size(), m_hordeGuids.size());
}

uint32_t AhBotSellerAccount::PickSellerGuid(uint32_t auctionHouseId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureLoadedLocked();
    return ai::PickSellerGuidForHouse(auctionHouseId, m_allianceGuids, m_hordeGuids, urand(0, UINT32_MAX - 1));
}

uint32_t AhBotSellerAccount::AccountId() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureLoadedLocked();
    return m_accountId;
}

bool AhBotSellerAccount::IsSellerGuid(uint32_t guidLow) const
{
    if (!guidLow)
        return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureLoadedLocked();
    return m_sellerGuidSet.count(guidLow) != 0;
}

bool AhBotSellerAccount::IsSellerAccount(uint32_t accountId) const
{
    if (!accountId)
        return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureLoadedLocked();
    return m_accountId == accountId || m_knownSellerAccounts.count(accountId) != 0;
}

std::vector<uint32_t> AhBotSellerAccount::SellerGuids() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureLoadedLocked();
    std::vector<uint32_t> out;
    out.reserve(m_allianceGuids.size() + m_hordeGuids.size());
    out.insert(out.end(), m_allianceGuids.begin(), m_allianceGuids.end());
    out.insert(out.end(), m_hordeGuids.begin(), m_hordeGuids.end());
    return out;
}

uint32_t AhBotSellerAccount::PurgeSellerMail(uint32_t maxMails)
{
    if (!maxMails)
        return 0;

    std::vector<uint32_t> sellers = SellerGuids();
    if (sellers.empty())
        return 0;

    std::string receiverList = JoinUintList(sellers, 0, sellers.size());
    std::unique_ptr<QueryResult> mails(CharacterDatabase.PQuery(
        "SELECT `id`, `itemTextId`, `has_items` FROM `mail` WHERE `receiver` IN (%s) LIMIT %u",
        receiverList.c_str(), maxMails));
    if (!mails)
        return 0;

    std::vector<uint32_t> mailIds;
    std::vector<uint32_t> itemTextIds;
    std::vector<uint32_t> mailIdsWithItems;
    do
    {
        Field* fields = mails->Fetch();
        uint32_t mailId = fields[0].GetUInt32();
        uint32_t itemTextId = fields[1].GetUInt32();
        bool hasItems = fields[2].GetBool();
        if (!mailId)
            continue;
        mailIds.push_back(mailId);
        if (itemTextId)
            itemTextIds.push_back(itemTextId);
        if (hasItems)
            mailIdsWithItems.push_back(mailId);
    } while (mails->NextRow());

    if (mailIds.empty())
        return 0;

    if (!mailIdsWithItems.empty())
    {
        std::vector<uint32_t> itemGuids;
        for (size_t offset = 0; offset < mailIdsWithItems.size(); offset += kMailDeleteChunkSize)
        {
            size_t end = std::min(offset + kMailDeleteChunkSize, mailIdsWithItems.size());
            std::string idList = JoinUintList(mailIdsWithItems, offset, end);
            std::unique_ptr<QueryResult> items(CharacterDatabase.PQuery(
                "SELECT `item_guid` FROM `mail_items` WHERE `mail_id` IN (%s)",
                idList.c_str()));
            if (items)
            {
                do
                {
                    uint32_t itemGuid = items->Fetch()[0].GetUInt32();
                    if (itemGuid)
                        itemGuids.push_back(itemGuid);
                } while (items->NextRow());
            }
        }
        if (!itemGuids.empty())
            DeleteByIdsInChunks("DELETE FROM `item_instance` WHERE `guid` IN", itemGuids);
        DeleteByIdsInChunks("DELETE FROM `mail_items` WHERE `mail_id` IN", mailIdsWithItems);
    }

    if (!itemTextIds.empty())
        DeleteByIdsInChunks("DELETE FROM `item_text` WHERE `id` IN", itemTextIds);

    DeleteByIdsInChunks("DELETE FROM `mail` WHERE `id` IN", mailIds);
    return static_cast<uint32_t>(mailIds.size());
}

std::string AhBotSellerAccount::GetStatus() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureLoadedLocked();

    std::ostringstream oss;
    if (!ai::IsValidSellerAccountName(sPlayerbotAIConfig.ahBotAccountName, sPlayerbotAIConfig.randomBotAccountPrefix))
    {
        oss << "seller account '" << sPlayerbotAIConfig.ahBotAccountName << "': invalid (empty, >16 chars, or matches random-bot prefix)";
        return oss.str();
    }

    bool ready = ai::IsReadyForSellers(m_accountId != 0, m_allianceGuids.size(), m_hordeGuids.size());
    oss << "seller account '" << (m_accountName.empty() ? sPlayerbotAIConfig.ahBotAccountName : m_accountName)
        << "' (#" << m_accountId << "): " << (ready ? "ready" : "not ready")
        << ", alliance=" << m_allianceGuids.size()
        << ", horde=" << m_hordeGuids.size();
    return oss.str();
}

} // namespace TortoiseBots
