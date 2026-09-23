// pi-lens-ignore-file: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access
#include "../ai/playerbot/GroupMembers.h"
#include "HireProvisionService.h"
#include "HireCost.h"
#include "HireLifecycle.h"
#include "BotManager.h"
#include "BotActivityLease.h"
#include "PlayerbotAIStorage.h"
#include "../host/BotSessionAdapter.h"
#include "../host/ModuleLog.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/PlayerbotFactory.h"
#include "../ai/playerbot/ChatHelper.h"
#include "../ai/playerbot/strategy/actions/ChangeTalentsAction.h"
#include "../ai/playerbot/strategy/Event.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
#include "World.h"
#include "WorldSession.h"
#include "WorldPacket.h"
#include "AccountMgr.h"
#include "SharedDefines.h"
#include "Log.h"
#include "Group/Group.h"
#include "Database/DatabaseEnv.h"
#include "Database/DBCStores.h"

#if __has_include("Handlers/CharacterCreation.h")
#include "Handlers/CharacterCreation.h"
#elif __has_include("CharacterCreation.h")
#include "CharacterCreation.h"
#else
#error "TortoiseBots hire requires core Handlers/CharacterCreation.h"
#endif

#include <algorithm>
#include <chrono>
#include <ctime>
#include <random>
#include <thread>

namespace TortoiseBots
{

namespace
{
constexpr uint32 kMaxLevel = 60;
constexpr uint32 kPendingProvisionTimeoutSec = 300;

std::string RandomHireName()
{
    static const char* onset[] = {
        "b", "br", "c", "cr", "d", "dr", "f", "g", "gr", "h", "j", "k", "kr", "l", "m", "n",
        "p", "r", "s", "sh", "st", "t", "th", "tr", "v", "w", "z", "kh", "mor", "thal", "bren",
    };
    static const char* nucleus[] = { "a", "e", "i", "o", "u", "ae", "ia", "or", "an", "el" };
    static const char* coda[] = { "", "", "", "n", "r", "s", "th", "ric", "d", "l" };
    uint32 nOn = sizeof(onset) / sizeof(onset[0]);
    uint32 nNu = sizeof(nucleus) / sizeof(nucleus[0]);
    uint32 nCo = sizeof(coda) / sizeof(coda[0]);
    std::string name;
    for (uint32 i = 0, syllables = urand(2, 3); i < syllables; ++i)
    {
        name += onset[urand(0, nOn - 1)];
        name += nucleus[urand(0, nNu - 1)];
    }
    name += coda[urand(0, nCo - 1)];
    if (name.size() > 11)
        name.resize(11);
    if (!name.empty())
        name[0] = char(name[0] & ~0x20);
    return name;
}

} // namespace

HireProvisionService& HireProvisionService::Instance()
{
    static HireProvisionService instance;
    return instance;
}

uint8 HireProvisionService::DefaultRoleForClass(uint8 classId)
{
    switch (classId)
    {
        case CLASS_WARRIOR:
        case CLASS_ROGUE:
        case CLASS_HUNTER:
        case CLASS_MAGE:
        case CLASS_WARLOCK:
            return ai::BOT_ROLE_DPS;
        case CLASS_PRIEST:
        case CLASS_SHAMAN:
        case CLASS_DRUID:
        case CLASS_PALADIN:
            // Hybrids default to their most-demanded group role; the gossip
            // spec step (and .bot role afterwards) can still pick DPS/tank.
            return ai::BOT_ROLE_HEALER;
        default:
            return ai::BOT_ROLE_DPS;
    }
}

bool HireProvisionService::ClassCanRole(uint8 classId, uint8 role)
{
    if (role == ai::BOT_ROLE_TANK)
        return classId == CLASS_WARRIOR || classId == CLASS_PALADIN || classId == CLASS_DRUID;
    if (role == ai::BOT_ROLE_HEALER)
        return classId == CLASS_PRIEST || classId == CLASS_PALADIN ||
            classId == CLASS_SHAMAN || classId == CLASS_DRUID;
    if (role == ai::BOT_ROLE_DPS)
        return classId != 0;
    return false;
}

HireOutcome HireProvisionService::Hire(Player* requester, HireSelection const& sel, bool fromGossip)
{
    HireOutcome outcome;
    if (!requester || !requester->GetSession() || !requester->IsInWorld())
    {
        outcome.status = HireStatus::Failed;
        outcome.message = "You must be in-game to hire a companion.";
        return outcome;
    }
    if (!sPlayerbotAIConfig.hireEnabled)
    {
        outcome.status = HireStatus::Disabled;
        outcome.message = "Companion hiring is disabled on this server.";
        return outcome;
    }
    if (requester->GetSession()->GetSecurity() < static_cast<int>(sPlayerbotAIConfig.hireMinAccountSecurity))
    {
        outcome.status = HireStatus::NoPermission;
        outcome.message = "Your account may not hire companions.";
        return outcome;
    }
    if (!fromGossip && sPlayerbotAIConfig.hireRequiresResting &&
        !requester->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING))
    {
        outcome.status = HireStatus::RestingRequired;
        outcome.message = "You must be resting in an inn or city to hire a companion. Speak to a <Mercenary Hire> recruiter, or rest first.";
        return outcome;
    }
    if (!sel.classId || !sel.race || (sel.gender != GENDER_MALE && sel.gender != GENDER_FEMALE))
    {
        outcome.status = HireStatus::InvalidChoice;
        outcome.message = "Choose a class, race, and gender first.";
        return outcome;
    }
    if (!sObjectMgr.GetPlayerInfo(sel.race, sel.classId))
    {
        outcome.status = HireStatus::InvalidChoice;
        outcome.message = "That race cannot be that class.";
        return outcome;
    }
    Team requesterTeam = requester->GetTeam();
    if (Player::TeamForRace(sel.race) != requesterTeam)
    {
        outcome.status = HireStatus::InvalidChoice;
        outcome.message = "That race fights for the other faction.";
        return outcome;
    }
    uint8 role = sel.role ? sel.role : DefaultRoleForClass(sel.classId);
    if (!ClassCanRole(sel.classId, role))
    {
        outcome.status = HireStatus::InvalidChoice;
        outcome.message = "That class cannot fill that role.";
        return outcome;
    }

    // Live-slot cap for THIS master guid: live HireLifecycle hires plus this
    // master's mustering (login-queued) provisions (CountHired covers both).
    // Never GetOwnedCharacters here: same-account alts are not hires, and a
    // dismissed bot's durable ownership row must not block the next hire
    // (Dismiss/.bot remove always release the lifecycle record). Each alt
    // (master guid) has its own hire slots.
    uint32_t owned = HireProvisionService::Instance().CountHired(requester);
    uint32_t maxHires = std::min<uint32_t>(sPlayerbotAIConfig.hireMaxBotsPerPlayer, 39);

    Group* group = requester->GetGroup();
    if (group && group->isBGGroup())
        group = requester->GetOriginalGroup();
    bool inRaid = group && group->isRaidGroup();
    // A normal party holds 5 (player + 4). Raid conversion lifts the cap to
    // the config. New players with no group hire their first companion freely:
    // the group is created by the invite path below.
    uint32_t partyCap = inRaid ? std::min<uint32_t>(39, maxHires) : 4;
    if (!group)
        partyCap = std::max<uint32_t>(partyCap, 1);
    if (owned >= maxHires)
    {
        outcome.status = HireStatus::CapReached;
        outcome.message = "You already command the maximum number of companions.";
        return outcome;
    }
    uint32_t liveGroupBots = 0;
    if (group)
    {
        for (Player* member : LiveGroupMembers(group))
        {
            if (!member || member == requester)
                continue;
            BotRecord* record = BotManager::Instance().FindBot(member->GetObjectGuid());
            if (record && BotManager::Instance().IsControllableBot(member))
                ++liveGroupBots;
        }
        // The raid cap follows the config ( HireMaxBotsPerPlayer ); the party
        // cap is the hardcoded 5-man shape. Count only module bots: real
        // players in the group never consume a hire slot.
        uint32_t groupCap = inRaid ? maxHires : 4;
        if (liveGroupBots >= groupCap)
        {
            outcome.status = HireStatus::GroupFull;
            if (inRaid)
                outcome.message = "Your raid already holds the maximum number of hired companions.";
            else
                outcome.message = "Your party is full. Convert to a raid to hire more companions.";
            return outcome;
        }
        if (!inRaid && group->IsFull() && owned + 1 > 4)
        {
            outcome.status = HireStatus::GroupFull;
            outcome.message = "Your party is full. Convert to a raid to hire more companions.";
            return outcome;
        }
    }
    if (!inRaid && owned + 1 > 4 && owned + 1 > maxHires)
    {
        outcome.status = HireStatus::CapReached;
        outcome.message = "You already command the maximum number of companions.";
        return outcome;
    }

    uint32_t level = requester->GetLevel();
    if (level < 1)
        level = 1;
    if (level > kMaxLevel)
        level = kMaxLevel;
    HireCost::CostConfig costs;
    costs.baseCopper = sPlayerbotAIConfig.hireBaseCostCopper;
    costs.mult2 = sPlayerbotAIConfig.hirePartyMult2;
    costs.mult3 = sPlayerbotAIConfig.hirePartyMult3;
    costs.mult4 = sPlayerbotAIConfig.hirePartyMult4;
    costs.raidFlatCopper = sPlayerbotAIConfig.hireRaidFlatCostCopper;
    uint32_t cost = HireCost::ForNextHire(costs, owned, level);
    if (requester->GetMoney() < cost)
    {
        outcome.status = HireStatus::Poor;
        outcome.message = std::string("You cannot afford that companion (") +
            ai::ChatHelper::formatMoney(cost) + ").";
        outcome.cost = cost;
        return outcome;
    }

    uint32_t accountId = 0;
    ObjectGuid guid;
    HireSelection resolved = sel;
    resolved.role = role;
    // Own dismissed bots first: an offline bot the requester already owns and
    // that is not hired out to another master is re-leveled and re-provisioned
    // instead of minting a new character (no DB bloat, keeps names stable).
    // Pool strangers second, fresh creation last.
    bool haveCandidate = FindOwnedReusableCandidate(requester, resolved, accountId, guid);
    if (!haveCandidate)
        haveCandidate = FindReusableCandidate(resolved, accountId, guid);
    Team team = requesterTeam;
    if (!haveCandidate && !CreateCandidate(resolved, team, accountId, guid))
    {
        outcome.status = HireStatus::NoCandidate;
        outcome.message = "No mercenary of that kind is available right now. Try again shortly.";
        sLog.outError("TortoiseBots: hire NoCandidate class %u race %u gender %u role %u (reuse + create both failed)",
            uint32(resolved.classId), uint32(resolved.race), uint32(resolved.gender), uint32(resolved.role));
        return outcome;
    }

    // Charge before the login queue: a queued login that later fails must not
    // mint a free companion, and a failed charge path must not queue a login.
    requester->ModifyMoney(-static_cast<int32>(cost));
    requester->SaveToDB();

    ObjectGuid masterGuid = requester->GetObjectGuid();
    uint32_t ownerAccountId = requester->GetSession()->GetAccountId();
    if (!BotManager::Instance().AddBotWithMaster(accountId, guid, masterGuid))
    {
        // Refund: the login queue rejected us (already online/pending).
        requester->ModifyMoney(static_cast<int32>(cost));
        requester->SaveToDB();
        outcome.status = HireStatus::Failed;
        outcome.message = "That mercenary is already mustering. Try again shortly.";
        return outcome;
    }
    if (!BotManager::Instance().RegisterOwnedCharacter(ownerAccountId, accountId, guid, masterGuid))
    {
        BotManager::Instance().RemoveBot(guid, false);
        requester->ModifyMoney(static_cast<int32>(cost));
        requester->SaveToDB();
        outcome.status = HireStatus::Failed;
        outcome.message = "Could not record your ownership. No gold was taken.";
        return outcome;
    }
    if (BotRecord* record = BotManager::Instance().FindBot(guid))
        record->random = true;
    HireLifecycle::Instance().Claim(guid, masterGuid, ownerAccountId);
    BotActivityLeaseManager::Instance().ClaimForMaster(guid.GetCounter());

    PendingProvision pending;
    pending.botGuid = guid;
    pending.masterGuid = masterGuid;
    pending.masterAccountId = ownerAccountId;
    pending.targetLevel = static_cast<uint8>(level);
    pending.role = role;
    pending.specIndex = sel.specIndex;
    pending.queuedAt = time(nullptr);
    m_pending.push_back(pending);

    PlayerCacheData const* data = sObjectMgr.GetPlayerDataByGUID(guid.GetCounter());
    outcome.status = HireStatus::Ok;
    outcome.cost = cost;
    outcome.botName = data ? data->sName : "companion";
    outcome.message = std::string("Hired ") + outcome.botName + " for " +
        ai::ChatHelper::formatMoney(cost) + ". Your companion is mustering.";
    return outcome;
}

// Own dismissed bots first: offline, requester-owned, matching
// class/race/gender, and not currently hired by anyone. Re-levels and
// re-provisions the same character instead of minting a new one.
bool HireProvisionService::FindOwnedReusableCandidate(Player* requester, HireSelection const& sel, uint32_t& accountId, ObjectGuid& guid)
{
    if (!requester || !requester->GetSession())
        return false;
    uint32_t ownerAccountId = requester->GetSession()->GetAccountId();
    for (OwnedCharacter const& row : BotManager::Instance().GetOwnedCharacters(ownerAccountId))
    {
        if (row.ownerAccountId != ownerAccountId)
            continue;
        PlayerCacheData const* data = sObjectMgr.GetPlayerDataByGUID(row.characterGuid.GetCounter());
        if (!data || data->uiClass != sel.classId || data->uiRace != sel.race || data->uiGender != sel.gender)
            continue;
        if (data->uiAccount == 0)
            continue;
        if (BotManager::Instance().FindBot(row.characterGuid))
            continue;
        if (sObjectAccessor.FindPlayer(row.characterGuid))
            continue;
        if (BotSessionAdapter::GetHeadlessSessionState(row.characterGuid) != HeadlessSessionState::NotFound)
            continue;
        if (HireLifecycle::Instance().IsHired(row.characterGuid))
            continue;
        OwnedCharacter live;
        if (BotManager::Instance().GetOwnedCharacter(row.characterGuid, live) && !live.masterGuid.IsEmpty() &&
            live.masterGuid != requester->GetObjectGuid())
            continue;
        accountId = data->uiAccount;
        guid = row.characterGuid;
        return true;
    }
    return false;
}

bool HireProvisionService::FindReusableCandidate(HireSelection const& sel, uint32_t& accountId, ObjectGuid& guid)
{
    std::string prefix = sPlayerbotAIConfig.randomBotAccountPrefix;
    if (prefix.empty())
        prefix = "RNDBOT";
    std::unique_ptr<QueryResult> accounts(LoginDatabase.PQuery(
        "SELECT id FROM account WHERE username LIKE '%s%%'", prefix.c_str()));
    if (!accounts)
        return false;

    std::vector<uint32_t> accountIds;
    do
    {
        Field* fields = accounts->Fetch();
        if (uint32_t id = fields[0].GetUInt32())
            accountIds.push_back(id);
    } while (accounts->NextRow());

    uint32_t perAccountLimit = sWorld.getConfig(CONFIG_UINT32_CHARACTERS_PER_REALM);
    if (!perAccountLimit)
        perAccountLimit = sWorld.getConfig(CONFIG_UINT32_CHARACTERS_PER_ACCOUNT);
    if (!perAccountLimit)
        perAccountLimit = 10;

    for (uint32_t id : accountIds)
    {
        std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
            "SELECT guid, name, race, class, gender, online, account FROM characters "
            "WHERE account = '%u' AND deleteDate IS NULL ORDER BY guid LIMIT 50", id));
        if (!rows)
            continue;
        uint32_t rowCount = 0;
        struct Row { uint32_t guid; uint32_t race; uint32_t cls; uint32_t gender; uint32_t online; std::string name; };
        std::vector<Row> parsed;
        do
        {
            Field* f = rows->Fetch();
            ++rowCount;
            Row r;
            r.guid = f[0].GetUInt32();
            r.name = f[1].GetString();
            r.race = f[2].GetUInt32();
            r.cls = f[3].GetUInt32();
            r.gender = f[4].GetUInt32();
            r.online = f[5].GetUInt32();
            parsed.push_back(r);
        } while (rows->NextRow());
        if (rowCount >= perAccountLimit)
        {
            TB_LOG_DETAIL("TortoiseBots: hire reuse skips full RNDBOT account %u (%u chars)", id, rowCount);
            continue;
        }
        for (Row const& r : parsed)
        {
            if (r.race != sel.race || r.cls != sel.classId || r.gender != sel.gender)
                continue;
            if (r.online)
                continue;
            ObjectGuid candidate(HIGHGUID_PLAYER, r.guid);
            if (BotManager::Instance().FindBot(candidate))
                continue;
            if (sObjectAccessor.FindPlayer(candidate))
                continue;
            if (BotSessionAdapter::GetHeadlessSessionState(candidate) != HeadlessSessionState::NotFound)
                continue;
            OwnedCharacter ownedRow;
            if (BotManager::Instance().GetOwnedCharacter(candidate, ownedRow) && ownedRow.ownerAccountId)
                continue;
            accountId = id;
            guid = candidate;
            return true;
        }
    }
    return false;
}

bool HireProvisionService::CreateCandidate(HireSelection const& sel, uint32_t requesterTeam, uint32_t& accountId, ObjectGuid& guid)
{
    std::string prefix = sPlayerbotAIConfig.randomBotAccountPrefix;
    if (prefix.empty())
        prefix = "RNDBOT";

    uint32_t perAccountLimit = sWorld.getConfig(CONFIG_UINT32_CHARACTERS_PER_REALM);
    if (!perAccountLimit)
        perAccountLimit = sWorld.getConfig(CONFIG_UINT32_CHARACTERS_PER_ACCOUNT);
    if (!perAccountLimit)
        perAccountLimit = 10;
    bool allowTwoSide = sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_ACCOUNTS) != 0;


    std::unique_ptr<QueryResult> accounts(LoginDatabase.PQuery(
        "SELECT id FROM account WHERE username LIKE '%s%%' ORDER BY id", prefix.c_str()));
    std::vector<uint32_t> accountIds;
    if (accounts)
    {
        do
        {
            Field* f = accounts->Fetch();
            if (uint32_t id = f[0].GetUInt32())
                accountIds.push_back(id);
        } while (accounts->NextRow());
    }

    auto accountEligible = [&](uint32_t id) -> bool
    {
        std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
            "SELECT guid, race, account FROM characters WHERE account = '%u' AND deleteDate IS NULL", id));
        uint32_t count = 0;
        bool hasAlliance = false;
        bool hasHorde = false;
        if (rows)
        {
            do
            {
                Field* f = rows->Fetch();
                ++count;
                Team t = Player::TeamForRace(f[1].GetUInt32());
                if (t == ALLIANCE)
                    hasAlliance = true;
                else if (t == HORDE)
                    hasHorde = true;
            } while (rows->NextRow());
        }
        if (count >= perAccountLimit)
            return false;
        if (!allowTwoSide)
        {
            if (hasAlliance && hasHorde)
            {
                TB_LOG_DETAIL("TortoiseBots: hire create skips mixed-faction RNDBOT account %u", id);
                return false;
            }
            if ((hasAlliance && requesterTeam == HORDE) || (hasHorde && requesterTeam == ALLIANCE))
            {
                TB_LOG_DETAIL("TortoiseBots: hire create skips wrong-team RNDBOT account %u", id);
                return false;
            }
        }
        return true;
    };

    uint32_t chosenAccount = 0;
    for (uint32_t id : accountIds)
    {
        if (accountEligible(id))
        {
            chosenAccount = id;
            break;
        }
    }
    if (!chosenAccount)
    {
        // Allocate one fresh RNDBOT account. Password is random, hashed by the
        // core, and never logged. Bounded: 20 name attempts, then fail closed.
        // NOTE: CreateAccount queues its INSERT on the async LoginDatabase
        // worker at runtime; GetId serves a stale in-memory map, so retry the
        // SAME name until it drains instead of minting orphan accounts.
        std::string safePrefix = prefix.substr(0, MAX_ACCOUNT_STR > 6 ? MAX_ACCOUNT_STR - 6 : 0);
        for (int attempt = 0; attempt < 20 && !chosenAccount; ++attempt)
        {
            uint32 suffix = urand(1000, 999999);
            char buf[16];
            snprintf(buf, sizeof(buf), "%06u", suffix);
            std::string username = safePrefix + buf;
            AccountMgr::normalizeString(username);
            if (sAccountMgr.GetId(username) != 0)
                continue;
            static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
            std::string password;
            try
            {
                std::random_device rd;
                std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
                for (int i = 0; i < 12; ++i)
                    password.push_back(charset[dist(rd)]);
            }
            catch (...)
            {
                password.clear();
                for (int i = 0; i < 12; ++i)
                    password.push_back(charset[urand(0, uint32(sizeof(charset) - 2))]);
            }
            AccountOpResult created = sAccountMgr.CreateAccount(username, password);
            if (created != AOR_OK)
            {
                sLog.outError("TortoiseBots: hire CreateAccount %s failed result %u", username.c_str(), uint32(created));
                continue;
            }
            // GetId re-queries the DB on a cache miss (synchronous query
            // connection), so it observes the just-committed row once the
            // async INSERT drains. Retry the same hired name a few times
            // instead of minting orphan accounts per attempt.
            uint32_t freshId = 0;
            for (int wait = 0; wait < 10 && !freshId; ++wait)
            {
                freshId = sAccountMgr.GetId(username);
                if (!freshId)
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            if (!freshId)
            {
                sLog.outError("TortoiseBots: hire created account %s but its id never became visible; giving up on it", username.c_str());
                return false;
            }
            sLog.outString("TortoiseBots: hire minted fresh account %s (%u)", username.c_str(), freshId);
            chosenAccount = freshId;
        }
    }

    for (int attempt = 0; attempt < 8; ++attempt)
    {
        std::string name = RandomHireName();
        std::string norm = name;
        if (!normalizePlayerName(norm))
            continue;
        if (sObjectMgr.GetPlayerGuidByName(norm))
            continue;

        CharacterCreateInfo info;
        info.name = norm;
        info.race = sel.race;
        info.class_ = sel.classId;
        info.gender = sel.gender;
        info.skin = uint8(urand(0, 7));
        info.face = uint8(urand(0, 7));
        info.hairStyle = uint8(urand(0, 7));
        info.hairColor = uint8(urand(0, 7));
        info.facialHair = uint8(urand(0, 7));
        info.outfitId = 0;
        info.challengeMask = 0;

        CharacterCreateOutcome outcome = CharacterCreation::CreateCharacter(chosenAccount, info);
        if (outcome.result == CHAR_CREATE_SUCCESS)
        {
            accountId = chosenAccount;
            guid = outcome.guid;
            return true;
        }
        if (outcome.result == CHAR_CREATE_NAME_IN_USE || outcome.result == CHAR_NAME_RESERVED ||
            outcome.result == CHAR_NAME_PROFANE || outcome.result == CHAR_CREATE_FAILED)
            continue;
        sLog.outError("TortoiseBots: hire CreateCharacter %s on account %u failed result %u",
            norm.c_str(), chosenAccount, uint32(outcome.result));
        return false;
    }
    return false;
}

bool HireProvisionService::EnsureGrouped(Player* master, Player* bot)
{
    if (!master || !bot || !master->GetSession() || master == bot)
        return false;
    if (bot->IsInSameGroupWith(master))
        return true;

    if (Group* oldGroup = bot->GetGroup())
    {
        if (oldGroup != master->GetGroup())
            oldGroup->RemoveMember(bot->GetObjectGuid(), 0);
        else
            return bot->IsInSameGroupWith(master);
    }

    Group* requesterGroup = master->GetGroup();
    if (requesterGroup && requesterGroup->isBGGroup())
        requesterGroup = master->GetOriginalGroup();
    if (requesterGroup && !requesterGroup->isRaidGroup() && requesterGroup->GetMembersCount() > 4)
        requesterGroup->ConvertToRaid();

    auto* previousInvite = bot->GetGroupInvite();
    WorldPacket packet;
    packet << bot->GetName() << uint32(0);
    master->GetSession()->HandleGroupInviteOpcode(packet);
    if (bot->GetGroupInvite() == previousInvite)
        return false;

    if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
    {
        ai::Event inviteEvent("group invite", "", master);
        if (ai->DoSpecificAction("accept invitation", inviteEvent, true) && bot->IsInSameGroupWith(master))
            return true;
    }
    return bot->IsInSameGroupWith(master);
}

uint32_t HireProvisionService::CountHired(Player* master) const
{
    if (!master || !master->GetSession())
        return 0;
    ObjectGuid masterGuid = master->GetObjectGuid();
    uint32_t count = 0;
    for (Player* bot : BotManager::Instance().GetBotsForMaster(masterGuid))
    {
        BotRecord* record = BotManager::Instance().FindBot(bot->GetObjectGuid());
        if (record && record->random && HireLifecycle::Instance().IsHired(bot->GetObjectGuid()))
            ++count;
    }
    // Include hired companions still mustering (login queued, not yet
    // controllable) so rapid double-hires cannot overshoot the cap.
    for (PendingProvision const& pending : m_pending)
    {
        if (pending.masterGuid != masterGuid)
            continue;
        bool alreadyCounted = false;
        for (Player* bot : BotManager::Instance().GetBotsForMaster(masterGuid))
        {
            if (bot && bot->GetObjectGuid() == pending.botGuid)
            {
                alreadyCounted = true;
                break;
            }
        }
        if (!alreadyCounted)
            ++count;
    }
    return count;
}

bool HireProvisionService::ProvisionNow(Player* bot, PendingProvision const& pending)
{
    if (!bot || !bot->IsInWorld())
        return false;
    Player* master = sObjectAccessor.FindPlayer(pending.masterGuid);
    bool masterOnline = master && master->IsInWorld() && master->GetSession() &&
        !master->GetSession()->IsHeadless();

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
        return false;

    // Level sync: GiveLevel runs the full stat/talent-point pipeline (and
    // UpdateSkillsForLevel rides along inside it); the bot keeps its
    // race/class/gender/name and only grows into the master's level.
    // Downgrades (reused higher-level candidate) go through SetLevel plus
    // the public stat/XP follow-ups GiveLevel would have run.
    if (bot->GetLevel() != pending.targetLevel)
    {
        if (pending.targetLevel > bot->GetLevel())
            bot->GiveLevel(pending.targetLevel);
        else
        {
            bot->SetLevel(pending.targetLevel);
            bot->InitStatsForLevel(false);
            bot->SetUInt32Value(PLAYER_XP, 0);
            bot->SetUInt32Value(PLAYER_NEXT_LEVEL_XP, sObjectMgr.GetXPForLevel(pending.targetLevel));
        }
    }
    // Spec first: pick a premade build matching the hired role where one
    // exists, otherwise keep the class default. The forced role makes the
    // mature auto-talents path converge on the hired kit. A gossip spec word
    // narrows it further: Feral Cat stays on the shared feral path while
    // Balance resolves to the balance path (both read as DPS).
    uint8 forcedRole = pending.role ? pending.role : DefaultRoleForClass(bot->GetClass());
    ai->SetForcedRole(forcedRole);
    {
        std::string specName;
        if (bot->GetClass() == CLASS_DRUID && pending.specIndex >= 0)
            specName = pending.specIndex == 2 ? "balance" : pending.specIndex == 1 ? "restoration" : "feral";
        std::vector<TalentPath*> paths = specName.empty() ? ai::ChangeTalentsAction::getPremadePaths(bot->GetClass(), "", (ai::BotRoles)forcedRole)
            : ai::ChangeTalentsAction::getPremadePaths(bot->GetClass(), specName, (ai::BotRoles)forcedRole);
        if (paths.empty())
            paths = ai::ChangeTalentsAction::getPremadePaths(bot->GetClass(), "", ai::BOT_ROLE_NONE);
        bool appliedSpec = false;
        if (!paths.empty())
        {
            TalentPath* chosen = paths[urand(0, uint32(paths.size() - 1))];
            TalentSpec spec = *ai::ChangeTalentsAction::GetBestPremadeSpec(bot, chosen->id);
            std::ostringstream out;
            if (spec.CheckTalents(bot, &out))
            {
                spec.ApplyTalents(bot, &out);
                sRandomBotFacade.SetValue(bot->GetGUIDLow(), "specNo", chosen->id + 1);
                sRandomBotFacade.SetValue(bot->GetGUIDLow(), "specLink", 0);
                appliedSpec = true;
            }
        }
        // Fall through to the generic auto path when no premade spec matched:
        // AutoSelectTalents converges on the class default for the new level.
        ai::Event talentEvent("hire", "", masterOnline ? master : nullptr);
        if (!appliedSpec)
            ai->DoSpecificAction("auto talents", talentEvent, true);
        else if (PlayerbotAIStorage::Instance().GetAI(bot))
        {
            PlayerbotAIStorage::Instance().GetAI(bot)->UpdateTalentSpec();
            ai->ResetStrategies();
        }
    }
    // Spells/skills/gear through the public factory wrapper: incremental
    // only, never wiping earned gear. Quest/trainer/dropped spells follow
    // the same knobs as fresh pool bots; the mature "auto learn spell"
    // action would no-op here (random-pool + master gating), so the factory
    // path is the correct one for a just-bound companion.
    {
        PlayerbotFactory factory(bot, bot->GetLevel());
        factory.ProvisionSpellsAndGear();
    }
    bot->SaveToDB();

    // Role strategies mirror `.bot role`: tank hires hold the protection kit
    // on both engines. Heal/DPS kits come from the spec-driven defaults.
    if (forcedRole == ai::BOT_ROLE_TANK)
    {
        ai->ChangeStrategy("+protection,+tank feral,+tank assist", BotState::BOT_STATE_NON_COMBAT);
        ai->ChangeStrategy("+protection,+tank feral,+tank assist,+pull,+pull back,+close", BotState::BOT_STATE_COMBAT);
        sPlayerbotDbStore.Save(ai);
    }

    if (masterOnline)
    {
        bot->TeleportTo(master->GetMapId(), master->GetPositionX(), master->GetPositionY(),
            master->GetPositionZ(), master->GetOrientation(), 0);
        if (!EnsureGrouped(master, bot))
        {
            sLog.outError("TortoiseBots: hired bot %s could not join master %s after provisioning",
                bot->GetName(), master->GetName());
            return false;
        }
        TB_LOG_BASIC("TortoiseBots: hired companion %s (level %u role %u) joined %s",
            bot->GetName(), bot->GetLevel(), forcedRole, master->GetName());
    }
    else
    {
        // Master is offline (grace-period hire): leave the bot where it is.
        // HireLifecycle reunites them when the master returns.
        TB_LOG_BASIC("TortoiseBots: hired companion %s provisioned while master offline; grouped on return",
            bot->GetName());
    }
    return true;
}

void HireProvisionService::DropStalePending()
{
    time_t now = time(nullptr);
    for (auto it = m_pending.begin(); it != m_pending.end();)
    {
        bool stale = (now - it->queuedAt) > static_cast<time_t>(kPendingProvisionTimeoutSec);
        bool gone = !BotManager::Instance().FindBot(it->botGuid) &&
            BotSessionAdapter::GetHeadlessSessionState(it->botGuid) == HeadlessSessionState::NotFound;
        if (stale || gone)
            it = m_pending.erase(it);
        else
            ++it;
    }
}

void HireProvisionService::Update(uint32_t diff)
{
    m_updateElapsedMs += diff;
    if (m_updateElapsedMs < 1000)
        return;
    m_updateElapsedMs = 0;
    if (m_pending.empty())
        return;

    DropStalePending();
    uint32_t completed = 0;
    for (auto it = m_pending.begin(); it != m_pending.end() && completed < 2;)
    {
        Player* bot = sObjectAccessor.FindPlayer(it->botGuid);
        BotRecord* record = BotManager::Instance().FindBot(it->botGuid);
        bool controllable = bot && record && BotManager::Instance().IsControllableBot(bot) &&
            PlayerbotAIStorage::Instance().GetAI(bot);
        if (!controllable)
        {
            ++it;
            continue;
        }
        PendingProvision pending = *it;
        if (ProvisionNow(bot, pending))
        {
            it = m_pending.erase(it);
            ++completed;
        }
        else
        {
            // Retry next tick unless the master went offline mid-provision:
            // keep the bot claimed and let the grace path reunite them.
            Player* master = sObjectAccessor.FindPlayer(pending.masterGuid);
            if (!master || !master->IsInWorld())
            {
                it = m_pending.erase(it);
                ++completed;
            }
            else
                ++it;
        }
    }
}

} // namespace TortoiseBots
