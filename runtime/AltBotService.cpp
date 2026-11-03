// pi-lens-ignore-file: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access
#include "AltBotService.h"
#include <algorithm>
#include "BotManager.h"
#include "ClaimLifecycle.h"
#include "HireLifecycle.h"
#include "PlayerbotAIStorage.h"
#include "RandomBotAccountRegistry.h"
#include "RandomBotService.h"
#include "../host/ModuleLog.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/strategy/Event.h"

#include "Bag.h"
#include "Chat.h"
#include "Database/DBCStructure.h"
#include "Group/Group.h"
#include "Maps/Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Timer.h"
#include "Transports/Transport.h"
#include "WorldSession.h"
#include "World.h"
#include "Util.h"
#include "../ai/playerbot/GroupMembers.h"

namespace TortoiseBots
{

namespace
{
constexpr char const* kPrefix = "|cff00ff00[TortoiseBots]|r ";

void Tell(Player* player, std::string const& text)
{
    if (player && player->GetSession())
        ChatHandler(player).PSendSysMessage("%s%s", kPrefix, text.c_str());
}

Player* OnlineOwner(ObjectGuid ownerGuid)
{
    Player* owner = ownerGuid.IsEmpty() ? nullptr : sObjectAccessor.FindPlayer(ownerGuid);
    return owner && owner->GetSession() && owner->GetSession()->HasNetworkTransport() ? owner : nullptr;
}

// Free slots in the backpack and ordinary bags (not quivers or special bags).
uint32_t FreeBagSlots(Player* bot)
{
    uint32_t free = 0;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            ++free;
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag const* pBag = dynamic_cast<Bag const*>(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag));
        if (pBag && pBag->GetProto()->Class == ITEM_CLASS_CONTAINER &&
            pBag->GetProto()->SubClass == ITEM_SUBCLASS_CONTAINER)
            free += pBag->GetFreeSlots();
    }
    return free;
}

char const* RefusalText(AltBotStartRefusal refusal)
{
    switch (refusal)
    {
        case AltBotStartRefusal::Hardcore:
            return "is a Hardcore character and cannot play on its own: one bot death would end your run. It stays with you.";
        default:
            return nullptr;
    }
}
// Location inputs for the pure PickAltBotStartExit decision (Issue #872).
AltBotLocation DescribeLocation(Player* bot)
{
    AltBotLocation where;
    if (!bot || !bot->IsInWorld())
        return where;
    where.inWorld = true;
    where.alive = bot->IsAlive();
    where.inBattleGround = bot->InBattleGround();
    where.onTransport = bot->GetTransport() != nullptr;
    // bot->GetMap() is null only while the bot is between maps: fail closed
    // (no relocation) rather than guessing dungeon-ness from the map id.
    if (Map const* map = bot->GetMap())
    {
        where.instanceableMap = map->Instanceable();
        where.dungeonMap = map->IsDungeon();
    }
    if (where.dungeonMap)
        where.haveExit = sObjectMgr.GetGoBackTrigger(bot->GetMapId()) != nullptr;
    where.haveGrave = sObjectMgr.GetClosestGraveYard(bot->GetPositionX(), bot->GetPositionY(),
        bot->GetPositionZ(), bot->GetMapId(), bot->GetTeam()) != nullptr;
    return where;
}

// True when no other real player shares the bot's group on its own map.
bool GroupAloneOnMap(Player* bot)
{
    Group* group = bot ? bot->GetGroup() : nullptr;
    if (!group)
        return true;
    for (Player* member : LiveGroupMembers(group))
    {
        if (member && member != bot && member->GetMapId() == bot->GetMapId() && member->GetSession() &&
            member->GetSession()->HasNetworkTransport())
            return false;
    }
    return true;
}

// Teleports an autonomous alt out of where it stands alone (Issue #872):
// instance exit portal first, else the BG/graveyard release, else the C19
// level-fitting scatter. Called after the session exists, so Update's sweep
// keeps it even if the bot relogs inside.
bool RelocateMisplacedAlt(Player* bot, AltBotExitTarget target)
{
    if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
        return false;
    if (!bot->IsAlive())
    {
        TB_LOG_DETAIL("TortoiseBots: alt-bot %s misplaced but dead: leaving it with its corpse", bot->GetName());
        return false;
    }
    switch (target)
    {
        case AltBotExitTarget::InstanceExit:
        {
            AreaTriggerTeleport const* exit = sObjectMgr.GetGoBackTrigger(bot->GetMapId());
            if (!exit)
                return false;
            bot->GetMotionMaster()->Clear();
            bot->StopMoving();
            bool const ok = bot->TeleportTo(exit->destination.mapId, exit->destination.x, exit->destination.y,
                exit->destination.z, exit->destination.o, TELE_TO_NOT_UNSUMMON_PET);
            TB_LOG_BASIC("TortoiseBots: alt-bot %s left its instance via the exit portal: %s",
                bot->GetName(), ok ? "ok" : "failed");
            return ok;
        }
        case AltBotExitTarget::Graveyard:
        {
            if (bot->GetTransport())
                bot->GetTransport()->RemovePassenger(bot);
            bot->GetMotionMaster()->Clear();
            bot->StopMoving();
            if (bot->InBattleGround())
                return bot->TeleportToBGEntryPoint();
            WorldSafeLocsEntry const* grave = sObjectMgr.GetClosestGraveYard(bot->GetPositionX(),
                bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId(), bot->GetTeam());
            if (!grave)
                return false;
            float orientation = bot->GetOrientation();
            if (float facing = sObjectMgr.GetWorldSafeLocFacing(grave->ID))
                orientation = facing;
            bool const ok = bot->TeleportTo(grave->map_id, grave->x, grave->y, grave->z, orientation,
                TELE_TO_NOT_UNSUMMON_PET);
            TB_LOG_BASIC("TortoiseBots: alt-bot %s released to its graveyard (map %u): %s",
                bot->GetName(), grave->map_id, ok ? "ok" : "failed");
            return ok;
        }
        case AltBotExitTarget::LevelZone:
            return BotManager::Instance().TeleportToLevelFittingZone(bot);
        case AltBotExitTarget::None:
            return false;
    }
    return false;
}
} // namespace

AltBotService& AltBotService::Instance()
{
    static AltBotService instance;
    return instance;
}

bool AltBotService::IsAutonomous(uint32_t guidLow) const
{
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    return m_sessions.find(guidLow) != m_sessions.end();
}

uint32_t AltBotService::GetOwnerAccount(uint32_t guidLow) const
{
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    auto it = m_sessions.find(guidLow);
    return it == m_sessions.end() ? 0 : it->second.ownerAccountId;
}

void AltBotService::QueueGroupLeave(ObjectGuid guid)
{
    if (!sPlayerbotAIConfig.altBots || !guid.IsPlayer())
        return;
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingLeaves.push_back(guid);
}

void AltBotService::QueueGroupDisband(Group* group)
{
    if (!sPlayerbotAIConfig.altBots || !group)
        return;
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    for (Group::member_citerator it = group->GetMemberSlots().begin(); it != group->GetMemberSlots().end(); ++it)
        m_pendingLeaves.push_back(it->guid);
}

void AltBotService::Update()
{
    std::vector<ObjectGuid> pending;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        pending.swap(m_pendingLeaves);
    }
    for (ObjectGuid const& guid : pending)
        TryStart(guid);

    // Issue #872: an autonomous alt left alone inside an instance leaves for
    // the entrance. Throttled to ~5 s and limited to the alt sessions: never
    // a world scan. Grouped alts with company stay put (F42).
    uint32_t const nowMs = WorldTimer::getMSTime();
    if (!AltBotSweepDue(nowMs, m_instanceSweepMs))
        return;
    m_instanceSweepMs = nowMs;
    std::vector<ObjectGuid> stray;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        for (auto const& kv : m_sessions)
            stray.emplace_back(HIGHGUID_PLAYER, kv.first);
    }
    for (ObjectGuid const& guid : stray)
    {
        Player* bot = sObjectAccessor.FindPlayer(guid);
        if (!bot || !bot->GetMap() || !bot->GetMap()->IsDungeon())
            continue;
        if (!ShouldAltBotLeaveInstance(true, bot->IsInWorld(), bot->IsAlive(), bot->IsBeingTeleported(),
                true, bot->GetGroup() != nullptr, !GroupAloneOnMap(bot)))
            continue;
        if (RelocateMisplacedAlt(bot, AltBotExitTarget::InstanceExit))
            Tell(OnlineOwner(GetOwnerAccount(guid.GetCounter())),
                std::string(bot->GetName()) + " was alone in an instance and came out to the entrance.");
    }
}

void AltBotService::TryStart(ObjectGuid guid)
{
    if (IsAutonomous(guid))
        return;

    BotRecord* record = BotManager::Instance().FindBot(guid);
    if (!record || record->random || record->lifecycle != BotLifecycle::InWorld)
        return;

    Player* bot = sObjectAccessor.FindPlayer(guid);
    if (!bot || !bot->GetSession() || !bot->GetSession()->IsHeadless())
        return;
    // Still (or again) grouped: a party member was removed, not this alt.
    if (bot->GetGroup())
        return;

    // Only a companion of a real player goes autonomous: its owner is the
    // player whose party it just left.
    Player* owner = OnlineOwner(record->masterGuid);
    if (!owner)
        return;

    bool const ownedAlt = !HireLifecycle::Instance().IsHired(guid) &&
        !RandomBotAccountRegistry::Instance().IsRegistered(record->accountId);
    AltBotStartRefusal const refusal = CheckAltBotStart(sPlayerbotAIConfig.altBots, ownedAlt,
        bot->IsHardcore(), bot->IsInWorld());
    if (refusal != AltBotStartRefusal::None)
    {
        if (char const* text = RefusalText(refusal))
            Tell(owner, std::string(bot->GetName()) + " " + text);
        return;
    }

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai || !BotManager::Instance().ClearBotMaster(guid))
        return;

    Session session;
    session.ownerAccountId = record->ownerAccountId ? record->ownerAccountId : record->accountId;
    session.ownerGuid = owner->GetObjectGuid();
    session.counters.startLevel = bot->GetLevel();
    session.counters.startMoney = bot->GetMoney();
    DisplacePoolBot(session, bot);
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        m_sessions.emplace(guid.GetCounter(), session);
    }

    // Drop everything it was doing for its master (follow target, orders) and
    // rebuild the strategies: IsFreeBot now gives it the pool's leisure set.
    // No snapshot replay: its saved kit is the follower one (follow/return,
    // no travel/guild/rpg/grind) and would wipe the pool kit just rebuilt.
    ai->Reset(true);
    bot->StopMoving();
    ai->ResetStrategies(false);

    // C19: alone, it goes to a zone that fits its level, like a pool bot's
    // login scatter. Below level 10 its quests are where it stands. Logged
    // out inside an instance/BG or left on a boat (Issue #872), the scatter
    // cannot reach it: the exit portal (or the BG/graveyard release) comes
    // first. Dead alts keep their corpse: the corpse run owns them.
    AltBotExitTarget target = PickAltBotStartExit(DescribeLocation(bot), false);
    if (target == AltBotExitTarget::None)
        RelocateMisplacedAlt(bot, AltBotExitTarget::LevelZone);
    else
        RelocateMisplacedAlt(bot, target);

    Tell(owner, std::string(bot->GetName()) + " now plays on its own, .bot remove " + bot->GetName() +
        " to log it out.");
    // B14: upgrades it equips move the old piece to the bags; with no room it
    // keeps what it wears.
    if (!FreeBagSlots(bot))
        Tell(owner, std::string(bot->GetName()) + "'s bags are full: clear the bags first, or it will not equip any upgrades it finds.");
    sPlayerbotAIConfig.logEvent(ai, "AltBotStart", owner->GetName(), std::to_string(bot->GetLevel()));
    TB_LOG_BASIC("TortoiseBots: alt-bot %s (owner account %u) is autonomous, displaced pool bot %u",
        bot->GetName(), session.ownerAccountId, session.displacedPoolGuidLow);
}

void AltBotService::DisplacePoolBot(Session& session, Player* alt)
{
    std::vector<PoolDisplaceCandidate> candidates;
    std::vector<Player*> players;
    for (Player* bot : BotManager::Instance().GetAllBots())
    {
        if (!bot || bot == alt || !BotManager::Instance().IsRandomBot(bot->GetObjectGuid()) ||
            HireLifecycle::Instance().IsHired(bot->GetObjectGuid()) ||
            ClaimLifecycle::Instance().IsClaimed(bot->GetGUIDLow()))
            continue;
        BotRecord* record = BotManager::Instance().FindBot(bot->GetObjectGuid());
        if (!record || record->lifecycle != BotLifecycle::InWorld)
            continue;
        PoolDisplaceCandidate c;
        c.guidLow = bot->GetGUIDLow();
        c.level = bot->GetLevel();
        c.grouped = bot->GetGroup() != nullptr;
        c.inCombat = bot->IsInCombat();
        c.inInstance = bot->GetMap() && bot->GetMap()->Instanceable();
        c.hasMaster = !record->masterGuid.IsEmpty();
        candidates.push_back(c);
        players.push_back(bot);
    }

    uint32_t const pick = PickPoolBotToDisplace(candidates, urand(0, UINT32_MAX - 1));
    if (!pick)
        return;
    for (Player* bot : players)
    {
        if (bot->GetGUIDLow() != pick)
            continue;
        session.displacedPoolGuidLow = pick;
        session.displacedPoolAccountId = bot->GetSession() ? bot->GetSession()->GetAccountId() : 0;
        RandomBotService::Instance().ForgetDeliberateLogout(bot->GetObjectGuid());
        BotManager::Instance().RemoveBot(bot->GetObjectGuid(), true);
        return;
    }
}

void AltBotService::RestoreDisplacedPoolBot(Session const& session)
{
    if (!session.displacedPoolGuidLow || !session.displacedPoolAccountId || sWorld.IsShutdowning())
        return;
    ObjectGuid const guid(HIGHGUID_PLAYER, session.displacedPoolGuidLow);
    if (BotManager::Instance().IsBot(guid) || sObjectAccessor.FindPlayer(guid))
        return;
    // A failed login is not retried here: the pool's own top-up refills the
    // slot with another bot.
    BotManager::Instance().AddRandomBot(session.displacedPoolAccountId, guid);
}

void AltBotService::End(uint32_t guidLow, Player* bot, AltBotEnd why)
{
    Session session;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        auto it = m_sessions.find(guidLow);
        if (it == m_sessions.end())
            return;
        session = it->second;
        m_sessions.erase(it);
    }

    if (bot)
    {
        session.counters.endLevel = bot->GetLevel();
        session.counters.endMoney = bot->GetMoney();
        std::string const summary = FormatAltBotSummary(bot->GetName(), session.counters);
        // E34: the player sees the summary where they are: on the alt itself
        // after logging in on it, otherwise on the owner character.
        Player* reader = why == AltBotEnd::PlayerLogin ? bot : OnlineOwner(session.ownerGuid);
        Tell(reader, summary);
        if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
            sPlayerbotAIConfig.logEvent(ai, "AltBotEnd", std::to_string(static_cast<uint32_t>(why)), summary);
        TB_LOG_BASIC("TortoiseBots: alt-bot session ended (%u): %s", static_cast<uint32_t>(why), summary.c_str());
    }
    RestoreDisplacedPoolBot(session);
}

bool AltBotService::TakeBack(Player* requester, Player* bot)
{
    if (!requester || !bot || !IsAutonomous(bot->GetObjectGuid()))
        return false;
    // BindBotMaster ends the session through OnMasterBound.
    if (!BotManager::Instance().BindBotMaster(bot->GetObjectGuid(), requester->GetObjectGuid()))
        return false;

    if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
    {
        ai->Reset(true);
        bot->StopMoving();
        ai->ResetStrategies();
        if (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT))
        {
            ai::Event followEvent("follow", "", requester);
            ai->DoSpecificAction("follow chat shortcut", followEvent, true);
        }
    }
    return true;
}

void AltBotService::OnMasterBound(Player* bot, Player* master)
{
    if (!bot || !master || !master->GetSession() || !master->GetSession()->HasNetworkTransport())
        return;
    // B8: another player's group gives it a master too; only the owner's
    // account takes the alt back.
    if (GetOwnerAccount(bot->GetGUIDLow()) != master->GetSession()->GetAccountId())
        return;
    End(bot->GetGUIDLow(), bot, AltBotEnd::TakenBack);
}

void AltBotService::OnBotLogout(Player* bot)
{
    if (bot)
        End(bot->GetGUIDLow(), bot, AltBotEnd::Removed);
}

void AltBotService::OnReclaimed(Player* player)
{
    if (player)
        End(player->GetGUIDLow(), player, AltBotEnd::PlayerLogin);
}

void AltBotService::OnOwnerLogout(Player* owner)
{
    if (!sPlayerbotAIConfig.altBotsLogoutWithOwner || !owner || !owner->GetSession() ||
        !owner->GetSession()->HasNetworkTransport())
        return;
    uint32_t const accountId = owner->GetSession()->GetAccountId();
    std::vector<uint32_t> leaving;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        for (auto const& kv : m_sessions)
            if (kv.second.ownerAccountId == accountId)
                leaving.push_back(kv.first);
    }
    for (uint32_t guidLow : leaving)
        BotManager::Instance().RemoveBot(ObjectGuid(HIGHGUID_PLAYER, guidLow), true);
}

void AltBotService::OnQuestCompleted(Player* bot)
{
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    auto it = bot ? m_sessions.find(bot->GetGUIDLow()) : m_sessions.end();
    if (it != m_sessions.end())
        ++it->second.counters.quests;
}

void AltBotService::OnDeath(Player* bot)
{
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    auto it = bot ? m_sessions.find(bot->GetGUIDLow()) : m_sessions.end();
    if (it != m_sessions.end())
        ++it->second.counters.deaths;
}

void AltBotService::AddTakenQuest(Player* bot, uint32_t questId)
{
    if (bot && questId)
        AddTakenQuest(bot->GetGUIDLow(), questId);
}

void AltBotService::AddTakenQuest(uint32_t guidLow, uint32_t questId)
{
    if (!questId)
        return;
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    auto it = m_sessions.find(guidLow);
    if (it == m_sessions.end())
        return;
    std::vector<uint32_t>& taken = it->second.takenQuests;
    if (std::find(taken.begin(), taken.end(), questId) == taken.end())
        taken.push_back(questId);
}

bool AltBotService::IsTakenQuest(uint32_t guidLow, uint32_t questId) const
{
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    auto it = m_sessions.find(guidLow);
    if (it == m_sessions.end() || !questId)
        return false;
    std::vector<uint32_t> const& taken = it->second.takenQuests;
    return std::find(taken.begin(), taken.end(), questId) != taken.end();
}
} // namespace TortoiseBots
