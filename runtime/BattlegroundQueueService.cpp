#include "BattlegroundQueueService.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "BotManager.h"
#include "PlayerbotAIStorage.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldSession.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "World.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Database/DatabaseEnv.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Battlegrounds/BattleGroundMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Battlegrounds/BattleGround.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldPacket.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Opcodes.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Group/Group.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "LFT/LFTMgr.h"
#ifndef MANGOSSERVER_LFTMGR_H
#error "TortoiseBots BG auto-queue requires the existing Penqle LFT lifecycle API"
#endif
// pi-lens-ignore: clang:pp_file_not_found
#include "Maps/Map.h"

#include <algorithm>
#include <vector>

namespace TortoiseBots
{

BattlegroundQueueService& BattlegroundQueueService::Instance()
{
    static BattlegroundQueueService instance;
    return instance;
}

void BattlegroundQueueService::Initialize()
{
    if (m_initialized)
        return;
    m_initialized = true;
    m_elapsedMs = 0;

    if (!sPlayerbotAIConfig.enabled || !sPlayerbotAIConfig.randomBotBgEnabled)
    {
        sLog.outString("TortoiseBots: BG auto-queue disabled (ai %u bg %u)",
            sPlayerbotAIConfig.enabled, sPlayerbotAIConfig.randomBotBgEnabled);
        return;
    }

    sLog.outString("TortoiseBots: BG auto-queue enabled interval %u max %u (WSG/AB/AV, guid 1337 bypass, core BattleGroundMgr ownership)",
        sPlayerbotAIConfig.randomBotBgQueueInterval, sPlayerbotAIConfig.randomBotBgMaxQueuePerInterval);
}

bool BattlegroundQueueService::IsEligible(::Player* bot) const
{
    if (!bot)
        return false;
    if (!bot->IsInWorld())
        return false;
    if (!bot->GetSession() || !bot->GetSession()->IsHeadless())
        return false;
    // In-memory live Headless/random selection: only random bots discovered
    // via RandomBotService's RNDBOT pool (pinned included). Owned/manual bots
    // remain human-driven.
    if (!BotManager::Instance().IsRandomBot(bot->GetObjectGuid()))
        return false;
    if (bot->InBattleGround())
        return false;
    if (bot->InBattleGroundQueue())
        return false;
    if (!bot->HasFreeBattleGroundQueueId())
        return false;
    // Deserter debuff and generic BG join gate.
    if (!bot->CanJoinToBattleground())
        return false;
    if (bot->IsBeingTeleported())
        return false;
    if (bot->IsTaxiFlying())
        return false;
    if (bot->IsInCombat())
        return false;
    if (!bot->IsAlive())
        return false;
    if (bot->GetLevel() < 10)
        return false;
    // Solo or group leader only; non-leader group members cannot queue solo
    // without splitting the group (donor crash fix).
    if (bot->GetGroup() && !bot->GetGroup()->IsLeader(bot->GetObjectGuid()))
        return false;
    // Do not queue bots actively piloted by a real player master.
    if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
        if (ai->HasActivePlayerMaster())
            return false;
    // Cross-feature arbitration without new core seam: reject bots already
    // queued/in-offer in native LFT and bots currently in a dungeon/instance.
    if (sLFTMgr.IsQueued(bot->GetObjectGuid()) || sLFTMgr.IsInOffer(bot->GetObjectGuid()))
        return false;
    if (Map* map = bot->GetMap())
        if (map->IsDungeon() || map->IsBattleGround())
            return false;
    // Must have at least one WSG/AB/AV type accessible by level.
    bool hasEligibleType = false;
    for (uint32 i = 1; i < MAX_BATTLEGROUND_QUEUE_TYPES; ++i)
    {
        BattleGroundQueueTypeId q = BattleGroundQueueTypeId(i);
        BattleGroundTypeId bgType = sServerFacade.BGTemplateId(q);
        if (bgType != BATTLEGROUND_WS && bgType != BATTLEGROUND_AB && bgType != BATTLEGROUND_AV)
            continue;
        BattleGround* bg = sBattleGroundMgr.GetBattleGroundTemplate(bgType);
        if (!bg)
            continue;
        if (!bot->GetBGAccessByLevel(bgType))
            continue;
        if (bot->InBattleGroundQueueForBattleGroundQueueType(q))
            continue;
        hasEligibleType = true;
        break;
    }
    if (!hasEligibleType)
        return false;

    return true;
}

bool BattlegroundQueueService::IsGroupFullyBotOwned(::Group* group) const
{
    if (!group)
        return false;
    for (auto const& slot : group->GetMemberSlots())
    {
        Player* member = sObjectAccessor.FindPlayer(slot.guid);
        if (!member || !member->IsInWorld())
            return false;
        WorldSession* sess = member->GetSession();
        if (!sess || !sess->IsHeadless())
            return false;
        if (!BotManager::Instance().IsRandomBot(slot.guid))
            return false;
        if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(member))
            if (ai->HasActivePlayerMaster())
                return false;
    }
    return true;
}

bool BattlegroundQueueService::HasLiveNonBotMember(::Group* group) const
{
    if (!group)
        return false;
    for (auto const& slot : group->GetMemberSlots())
    {
        Player* member = sObjectAccessor.FindPlayer(slot.guid);
        if (!member || !member->IsInWorld())
            continue; // offline/non-live -> not considered live
        WorldSession* sess = member->GetSession();
        if (!sess || !sess->IsHeadless())
            return true;
        if (!BotManager::Instance().IsRandomBot(slot.guid))
            return true;
        if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(member))
            if (ai->HasActivePlayerMaster())
                return true;
    }
    return false;
}

void BattlegroundQueueService::PruneOwnedQueueSet()
{
    // Build live queued (guid, queueType) pairs from in-memory BotManager
    // snapshot (no DB, no world scan). Prune any owned pair no longer queued
    // or no longer live - covers native leave/logout/external removal.
    std::unordered_set<uint64_t> liveQueued;
    for (Player* p : BotManager::Instance().GetAllBots())
    {
        if (!p || !p->InBattleGroundQueue())
            continue;
        for (uint32 i = 1; i < MAX_BATTLEGROUND_QUEUE_TYPES; ++i)
        {
            BattleGroundQueueTypeId q = BattleGroundQueueTypeId(i);
            BattleGroundTypeId bgType = sServerFacade.BGTemplateId(q);
            if (bgType != BATTLEGROUND_WS && bgType != BATTLEGROUND_AB && bgType != BATTLEGROUND_AV)
                continue;
            if (p->InBattleGroundQueueForBattleGroundQueueType(q))
                liveQueued.insert(EncodeOwnedKey(p->GetObjectGuid().GetCounter(), uint32_t(q)));
        }
    }
    for (auto it = m_ownedQueuedGuids.begin(); it != m_ownedQueuedGuids.end(); )
    {
        if (liveQueued.find(*it) == liveQueued.end())
            it = m_ownedQueuedGuids.erase(it);
        else
            ++it;
    }
}

bool BattlegroundQueueService::TryQueue(::Player* bot)
{
    if (!IsEligible(bot))
        return false;

    // Collect eligible WSG/AB/AV queue types for this bot's level bracket.
    std::vector<BattleGroundQueueTypeId> eligible;
    for (uint32 i = 1; i < MAX_BATTLEGROUND_QUEUE_TYPES; ++i)
    {
        BattleGroundQueueTypeId q = BattleGroundQueueTypeId(i);
        BattleGroundTypeId bgType = sServerFacade.BGTemplateId(q);
        if (bgType != BATTLEGROUND_WS && bgType != BATTLEGROUND_AB && bgType != BATTLEGROUND_AV)
            continue;
        BattleGround* bg = sBattleGroundMgr.GetBattleGroundTemplate(bgType);
        if (!bg)
            continue;
        if (!bot->GetBGAccessByLevel(bgType))
            continue;
        if (bot->InBattleGroundQueueForBattleGroundQueueType(q))
            continue;
        eligible.push_back(q);
    }

    if (eligible.empty())
        return false;

    BattleGroundQueueTypeId queueType = eligible[urand(0, eligible.size() - 1)];
    BattleGroundTypeId bgType = sServerFacade.BGTemplateId(queueType);
    BattleGround* bg = sBattleGroundMgr.GetBattleGroundTemplate(bgType);
    if (!bg)
        return false;

    uint32 instanceId = 0;
    uint8 joinAsGroup = (bot->GetGroup() && bot->GetGroup()->IsLeader(bot->GetObjectGuid())) ? 1 : 0;
    if (bot->GetGroup() && !joinAsGroup)
        return false;
    // AV cannot queue as group in core (BattleGroundHandler returns early
    // for bgTypeId==BATTLEGROUND_AV && joinAsGroup). Force solo to avoid
    // silent failure and never report success for joinAsGroup AV.
    if (bgType == BATTLEGROUND_AV)
        joinAsGroup = 0;
    else if (joinAsGroup)
    {
        // WSG/AB group safety: fallback to solo only when non-bot members
        // are offline/non-live; skip rather than solo-queue if any live
        // non-bot/human member would be silently pulled.
        if (!IsGroupFullyBotOwned(bot->GetGroup()))
        {
            if (HasLiveNonBotMember(bot->GetGroup()))
            {
                sLog.outString("TortoiseBots: BG auto-queue %s group has live non-bot/human member -> skip (would silently pull)", bot->GetName());
                return false;
            }
            sLog.outString("TortoiseBots: BG auto-queue %s group not all Headless random bots (offline/non-live) -> fallback solo", bot->GetName());
            joinAsGroup = 0;
        }
    }

    // Native command path via WorldSession::HandleBattlemasterJoinOpcode
    // with guid 1337 bypass (core queued-via-command check) so no nearby
    // battlemaster unit is required. Uses direct handler to avoid headless
    // recvQueue drain (World::UpdateSessions never drains headless).
    ObjectGuid guid(uint64(1337));
    WorldPacket packet(CMSG_BATTLEMASTER_JOIN, 20);

#ifdef MANGOSBOT_ZERO
    uint32 mapId = bg->GetMapId();
    // Fallback if template map is zero (should not happen for Vanilla
    // WSG/AB/AV, but fail closed rather than sending 0).
    if (!mapId)
    {
        sLog.outString("TortoiseBots: BG auto-queue no map for bgType %u for bot %s", bgType, bot->GetName());
        return false;
    }
    packet << guid << mapId << instanceId << joinAsGroup;
#else
    packet << guid << uint32(bgType) << instanceId << joinAsGroup;
#endif

    // WorldSession::HandleBattlemasterJoinOpcode is the native handler that
    // owns queue/invite/cancellation state (BattleGroundMgr/BattleGroundQueue).
    // Invites are delivered via SMSG_BATTLEFIELD_STATUS and handled by the
    // existing PlayerbotAI BGStatusAction -> HandleBattleFieldPortOpcode path.
    bot->GetSession()->HandleBattlemasterJoinOpcode(packet);

    const char* name = "BG";
    if (bgType == BATTLEGROUND_WS) name = "WSG";
    else if (bgType == BATTLEGROUND_AB) name = "AB";
    else if (bgType == BATTLEGROUND_AV) name = "AV";

    // Verify native queue ownership: core silently drops AV joinAsGroup and
    // other invalid joins; never report success if not actually queued.
    if (!bot->InBattleGroundQueueForBattleGroundQueueType(queueType))
    {
        sLog.outString("TortoiseBots: BG auto-queue %s (%s) not queued (core rejected joinAsGroup=%u)", bot->GetName(), name, joinAsGroup);
        return false;
    }

    // In-memory ownership as (guidLow, queueType) pairs: track only GUIDs
    // the core actually queued for this queueType so an excluded
    // bracket/offline group member is never mistaken for a service-owned
    // entry and a separate manual queueType is not cancelled on master reclaim.
    m_ownedQueuedGuids.insert(EncodeOwnedKey(bot->GetObjectGuid().GetCounter(), uint32_t(queueType)));
    if (joinAsGroup)
    {
        if (Group* grp = bot->GetGroup())
            for (auto const& slot : grp->GetMemberSlots())
            {
                if (slot.guid.GetCounter() == bot->GetObjectGuid().GetCounter())
                    continue;
                Player* member = sObjectAccessor.FindPlayer(slot.guid);
                if (member && member->InBattleGroundQueueForBattleGroundQueueType(queueType))
                    m_ownedQueuedGuids.insert(EncodeOwnedKey(slot.guid.GetCounter(), uint32_t(queueType)));
            }
    }

    sLog.outString("TortoiseBots: BG auto-queue %s (%s) level %u team %u guid %s%s",
        bot->GetName(), name, bot->GetLevel(), bot->GetTeam(), bot->GetObjectGuid().GetString().c_str(),
        joinAsGroup ? " as group" : " solo");
    return true;
}

void BattlegroundQueueService::ReconcileMasterQueue()
{
    if (!sPlayerbotAIConfig.enabled || !sPlayerbotAIConfig.randomBotBgEnabled)
        return;
    PruneOwnedQueueSet();
    // Reconcile service-owned queued bots whose human master became active
    // before invite. Only touches (guid, queueType) pairs owned by this service
    // so manual/native queues are never cancelled. Uses the existing native
    // WorldSession::HandleBattleFieldPortOpcode action=0 leave path (no new core
    // seam, no queue internals from module, no second queue/thread). Fail-closed
    // map validation via GetBattleGroundTemplate/GetMapId before sending.
    std::vector<Player*> snapshot = BotManager::Instance().GetAllBots();
    for (Player* bot : snapshot)
    {
        if (!bot || !bot->InBattleGroundQueue())
            continue;
        if (bot->InBattleGround())
            continue;
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai || !ai->HasActivePlayerMaster())
            continue;
        WorldSession* sess = bot->GetSession();
        if (!sess)
            continue;
        for (uint32 i = 1; i < MAX_BATTLEGROUND_QUEUE_TYPES; ++i)
        {
            BattleGroundQueueTypeId q = BattleGroundQueueTypeId(i);
            if (!bot->InBattleGroundQueueForBattleGroundQueueType(q))
                continue;
            BattleGroundTypeId bgType = sServerFacade.BGTemplateId(q);
            if (bgType != BATTLEGROUND_WS && bgType != BATTLEGROUND_AB && bgType != BATTLEGROUND_AV)
                continue;
            uint64_t key = EncodeOwnedKey(bot->GetObjectGuid().GetCounter(), uint32_t(q));
            if (m_ownedQueuedGuids.find(key) == m_ownedQueuedGuids.end())
                continue;
            BattleGround* bg = sBattleGroundMgr.GetBattleGroundTemplate(bgType);
            if (!bg)
                continue;
            uint32 mapId = bg->GetMapId();
            if (!mapId)
                continue; // fail-closed: no valid map for this bg type
            WorldPacket packet(CMSG_BATTLEFIELD_PORT, 8);
            packet << mapId << uint8(0);
            sess->HandleBattleFieldPortOpcode(packet);
            if (!bot->InBattleGroundQueueForBattleGroundQueueType(q))
                m_ownedQueuedGuids.erase(key);
            sLog.outString("TortoiseBots: BG auto-queue reconcile %s queued but master %s active -> left queue %u (native port 0)", bot->GetName(), ai->GetMaster() ? ai->GetMaster()->GetName() : "?", q);
        }
    }
    PruneOwnedQueueSet();
}

void BattlegroundQueueService::Update(uint32_t diff)
{
    if (!m_initialized)
        return;
    if (!sPlayerbotAIConfig.enabled || !sPlayerbotAIConfig.randomBotBgEnabled)
        return;

    // Reconcile every tick: if a bot queued by this service now has an active
    // human master, leave queue via native BattleGroundQueue before invite.
    ReconcileMasterQueue();

    uint32 interval = sPlayerbotAIConfig.randomBotBgQueueInterval;
    if (interval < 5000)
        interval = 5000;
    if (interval > 600000)
        interval = 600000;

    m_elapsedMs += diff;
    if (m_elapsedMs < interval)
        return;
    m_elapsedMs = 0;

    uint32 maxPerInterval = sPlayerbotAIConfig.randomBotBgMaxQueuePerInterval;
    if (!maxPerInterval)
        return;
    if (maxPerInterval > 10)
        maxPerInterval = 10;

    // In-memory live Headless/random candidate selection: snapshot of
    // BotManager's live players, no DB scan, no world scan, no second queue.
    std::vector<Player*> candidates;
    for (Player* p : BotManager::Instance().GetAllBots())
        if (IsEligible(p))
            candidates.push_back(p);

    if (candidates.empty())
        return;

    // Randomize order so the same low guids are not always selected.
    for (size_t i = candidates.size() - 1; i > 0; --i)
    {
        size_t j = urand(0, static_cast<uint32>(i));
        std::swap(candidates[i], candidates[j]);
    }

    uint32 queued = 0;
    for (Player* bot : candidates)
    {
        if (queued >= maxPerInterval)
            break;
        if (!IsEligible(bot))
            continue;
        if (TryQueue(bot))
            ++queued;
    }

    if (queued)
        sLog.outString("TortoiseBots: BG auto-queue tick queued %u/%u (eligible %u, interval %u ms)",
            queued, maxPerInterval, static_cast<uint32>(candidates.size()), interval);
}

void BattlegroundQueueService::Shutdown()
{
    if (!m_initialized)
        return;
    m_ownedQueuedGuids.clear();
    m_initialized = false;
    m_elapsedMs = 0;
}

} // namespace TortoiseBots
