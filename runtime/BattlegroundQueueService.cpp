#include "BattlegroundQueueService.h"
#include "AutonomousBgPolicy.h"
#include "BotActivityLease.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "BotManager.h"
#include "RandomBotService.h"
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
#include "../host/ModuleLog.h"

#include <algorithm>
#include <vector>

namespace TortoiseBots
{

namespace
{
struct HumanBgDemand
{
    BattleGroundQueueTypeId queueType;
    BattleGroundTypeId bgType;
    BattleGroundBracketId bracket;
    Team underrepresentedTeam = TEAM_NONE;
};

bool IsHumanWaitingParticipant(BattleGroundQueue::QueuedParticipantInfo const& info)
{
    if (!info.online || info.isInvited)
        return false;

    ::Player* player = sObjectAccessor.FindPlayer(info.guid);
    if (!player || !player->IsInWorld() || !player->GetSession())
        return false;
    if (player->GetSession()->IsHeadless())
        return false;
    return !BotManager::Instance().IsRandomBot(info.guid);
}

std::vector<HumanBgDemand> GetHumanBgDemands()
{
    std::vector<HumanBgDemand> demands;
    for (uint32 queueIndex = 1; queueIndex < MAX_BATTLEGROUND_QUEUE_TYPES; ++queueIndex)
    {
        BattleGroundQueueTypeId queueType = BattleGroundQueueTypeId(queueIndex);
        BattleGroundTypeId bgType = sServerFacade.BGTemplateId(queueType);
        if (bgType != BATTLEGROUND_WS && bgType != BATTLEGROUND_AB && bgType != BATTLEGROUND_AV)
            continue;

        if (!sBattleGroundMgr.GetBattleGroundTemplate(bgType))
            continue;

        for (uint32 bracketIndex = 0; bracketIndex < MAX_BATTLEGROUND_BRACKETS; ++bracketIndex)
        {
            BattleGroundBracketId bracket = BattleGroundBracketId(bracketIndex);
            std::vector<BattleGroundQueue::QueuedParticipantInfo> participants =
                sBattleGroundMgr.GetQueuedParticipants(queueType, bracket);
            uint32 alliance = 0;
            uint32 horde = 0;
            for (auto const& participant : participants)
            {
                if (participant.bgTypeId != bgType || participant.bracketId != bracket ||
                    !IsHumanWaitingParticipant(participant))
                    continue;
                if (participant.team == ALLIANCE)
                    ++alliance;
                else if (participant.team == HORDE)
                    ++horde;
            }

            if (!alliance && !horde)
                continue;

            HumanBgDemand demand{queueType, bgType, bracket};
            if (alliance < horde)
                demand.underrepresentedTeam = ALLIANCE;
            else if (horde < alliance)
                demand.underrepresentedTeam = HORDE;
            demands.push_back(demand);
        }
    }
    return demands;
}
}

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
        TB_LOG_BASIC("TortoiseBots: BG auto-queue disabled (ai %u bg %u)",
            sPlayerbotAIConfig.enabled, sPlayerbotAIConfig.randomBotBgEnabled);
        return;
    }

    TB_LOG_BASIC("TortoiseBots: demand-aware BG auto-queue enabled interval %u max %u (WSG/AB/AV, guid 1337 bypass, core BattleGroundMgr ownership)",
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
    // Lease arbitration (issue #89): host guards above stay authoritative.
    if (!BotActivityLeaseManager::Instance().IsAvailableForBackground(bot->GetGUIDLow()))
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
    std::vector<uint64_t> pruned;
    for (auto it = m_ownedQueuedGuids.begin(); it != m_ownedQueuedGuids.end(); )
    {
        if (liveQueued.find(*it) == liveQueued.end())
        {
            pruned.push_back(*it);
            it = m_ownedQueuedGuids.erase(it);
        }
        else
            ++it;
    }
    // Release the BgQueued lease only when the guid holds no other live
    // owned queue entry (leases are per-bot, ownership is per queueType).
    for (uint64_t key : pruned)
    {
        uint32_t guidLow = uint32_t(key >> 32);
        bool stillOwned = false;
        for (uint64_t live : m_ownedQueuedGuids)
            if (uint32_t(live >> 32) == guidLow) { stillOwned = true; break; }
        if (!stillOwned)
            BotActivityLeaseManager::Instance().Release(guidLow, BotActivity::BgQueued);
    }
}

bool BattlegroundQueueService::TryQueue(::Player* bot, uint32 queueTypeValue)
{
    if (!IsEligible(bot) || queueTypeValue >= MAX_BATTLEGROUND_QUEUE_TYPES)
        return false;

    BattleGroundQueueTypeId queueType = BattleGroundQueueTypeId(queueTypeValue);
    BattleGroundTypeId bgType = sServerFacade.BGTemplateId(queueType);
    if (bgType != BATTLEGROUND_WS && bgType != BATTLEGROUND_AB && bgType != BATTLEGROUND_AV)
        return false;

    BattleGround* bg = sBattleGroundMgr.GetBattleGroundTemplate(bgType);
    if (!bg || !bot->GetBGAccessByLevel(bgType) ||
        bot->InBattleGroundQueueForBattleGroundQueueType(queueType))
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
                TB_LOG_DEBUG("TortoiseBots: BG auto-queue %s group has live non-bot/human member -> skip (would silently pull)", bot->GetName());
                return false;
            }
            TB_LOG_DEBUG("TortoiseBots: BG auto-queue %s group not all Headless random bots (offline/non-live) -> fallback solo", bot->GetName());
            joinAsGroup = 0;
        }
    }

    // Atomic all-or-nothing BgQueued acquisition (30-minute lease, issue #89).
    // A Trading/LFT/PlayerMaster member aborts the whole group queue with
    // rollback so no partial queueing occurs.
    std::vector<uint32_t> leaseGuids;
    leaseGuids.push_back(bot->GetObjectGuid().GetCounter());
    if (joinAsGroup)
    {
        if (Group* grp = bot->GetGroup())
            for (auto const& slot : grp->GetMemberSlots())
                if (slot.guid.GetCounter() != bot->GetObjectGuid().GetCounter())
                    leaseGuids.push_back(slot.guid.GetCounter());
    }
    struct LeaseAttempt
    {
        uint32_t guidLow;
        BotActivity previousActivity;
    };
    std::vector<LeaseAttempt> acquired;
    auto rollbackLeases = [&]()
    {
        for (LeaseAttempt const& attempt : acquired)
        {
            // A same-activity re-acquire did not create ownership for this
            // attempt; keep the existing BgQueued lease intact.
            if (attempt.previousActivity == BotActivity::BgQueued)
                continue;
            BotActivity restore = attempt.previousActivity == BotActivity::Grinding
                ? BotActivity::Grinding : BotActivity::Idle;
            BotActivityLeaseManager::Instance().Release(attempt.guidLow, BotActivity::BgQueued, restore);
        }
    };
    for (uint32_t guidLow : leaseGuids)
    {
        BotActivity previousActivity = BotActivityLeaseManager::Instance().GetActivity(guidLow);
        if (!BotActivityLeaseManager::Instance().TryAcquire(guidLow, BotActivity::BgQueued, 1800000))
        {
            rollbackLeases();
            return false;
        }
        acquired.push_back(LeaseAttempt{guidLow, previousActivity});
    }
    // Leases held for the atomic group attempt; core rejection below rolls back.

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
        TB_LOG_DETAIL("TortoiseBots: BG auto-queue no map for bgType %u for bot %s", bgType, bot->GetName());
        rollbackLeases();
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
        TB_LOG_DETAIL("TortoiseBots: BG auto-queue %s (%s) not queued (core rejected joinAsGroup=%u)", bot->GetName(), name, joinAsGroup);
        rollbackLeases();
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
        // Release leases for group members the core did not actually queue
        // (excluded bracket/offline) so no ghost BgQueued lingers to timeout.
        for (LeaseAttempt const& attempt : acquired)
        {
            bool owned = false;
            for (uint64_t key : m_ownedQueuedGuids)
                if (uint32_t(key >> 32) == attempt.guidLow) { owned = true; break; }
            if (!owned)
            {
                if (attempt.previousActivity == BotActivity::BgQueued)
                    continue;
                BotActivity restore = attempt.previousActivity == BotActivity::Grinding
                    ? BotActivity::Grinding : BotActivity::Idle;
                BotActivityLeaseManager::Instance().Release(attempt.guidLow, BotActivity::BgQueued, restore);
            }
        }
    }

    TB_LOG_DETAIL("TortoiseBots: BG auto-queue %s (%s) level %u team %u guid %s%s",
        bot->GetName(), name, bot->GetLevel(), bot->GetTeam(), bot->GetObjectGuid().GetString().c_str(),
        joinAsGroup ? " as group" : " solo");
    return true;
}

void BattlegroundQueueService::ReconcileMasterQueue()
{
    if (!sPlayerbotAIConfig.enabled || !sPlayerbotAIConfig.randomBotBgEnabled)
        return;
    if (m_ownedQueuedGuids.empty())
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
            {
                m_ownedQueuedGuids.erase(key);
                // Defensive lease release; no-op if ClaimForMaster already
                // moved the bot to PlayerMaster via active eviction.
                BotActivityLeaseManager::Instance().Release(bot->GetObjectGuid().GetCounter(), BotActivity::BgQueued);
            }
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
    // Issue #265: no pool bot is queued while the managed pool is being reset.
    if (!RandomBotService::Instance().IsPoolAvailable())
        return;

    // Reconcile once a second: if a bot queued by this service now has an
    // active human master, leave queue via native BattleGroundQueue before
    // invite. Every tick it scanned the whole pool twice (~4% of the world
    // thread at 2000 bots) for a queue that is almost always empty.
    m_reconcileElapsedMs += diff;
    if (m_reconcileElapsedMs >= 1000)
    {
        m_reconcileElapsedMs = 0;
        ReconcileMasterQueue();
    }

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
    // Observe actual human demand through the core's copy-only snapshot.
    std::vector<HumanBgDemand> demands = GetHumanBgDemands();
    if (demands.empty())
    {
        // No human demand: autonomous bot-only WSG seeding (default on,
        // opt out with AiPlayerbot.RandomBotBgAutonomous = 0) runs on the
        // same cadence/budget instead of idling.
        UpdateAutonomousSeeding();
        return;
    }

    // In-memory live Headless/random candidate selection: no DB or world scan.
    std::vector<Player*> candidates;
    for (Player* p : BotManager::Instance().GetAllBots())
        if (IsEligible(p))
            candidates.push_back(p);

    if (candidates.empty())
        return;

    // Randomize order so the same low GUIDs are not always selected.
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

        for (HumanBgDemand const& demand : demands)
        {
            if (bot->GetBattleGroundBracketIdFromLevel(demand.bgType) != demand.bracket)
                continue;
            if (demand.underrepresentedTeam != TEAM_NONE &&
                bot->GetTeam() != demand.underrepresentedTeam)
                continue;
            if (TryQueue(bot, uint32(demand.queueType)))
            {
                ++queued;
                break;
            }
        }
    }

    if (queued)
        TB_LOG_DEBUG("TortoiseBots: BG auto-queue tick queued %u/%u for human demand (eligible %u, demand buckets %u, interval %u ms)",
            queued, maxPerInterval, static_cast<uint32>(candidates.size()),
            static_cast<uint32>(demands.size()), interval);
}

uint32_t BattlegroundQueueService::CountRunningBotOnlyWsg(uint32_t bracketIndex) const
{
    // A FULL bot-only instance of THIS bracket: running WSG whose players are
    // all service Headless random bots and whose sides are both at max, so
    // the core's free-slot path no longer invites queued seeds into it.
    // A still-forming match (either side below max) absorbs queued seeds
    // through FillPlayersToBG, so it must NOT count — otherwise top-up would
    // stall at core min (4v4) instead of reaching 10v10. Humans, owned/party
    // bots, other brackets, or an empty map fail the check so mixed matches
    // and parallel brackets never count against this bracket's cap.
    uint32_t count = 0;
    for (auto it = sBattleGroundMgr.GetBattleGroundsBegin(BATTLEGROUND_WS);
        it != sBattleGroundMgr.GetBattleGroundsEnd(BATTLEGROUND_WS); ++it)
    {
        BattleGround* bg = it->second;
        if (!bg || bg->GetStatus() == STATUS_WAIT_LEAVE)
            continue;
        if (bracketIndex < MAX_BATTLEGROUND_BRACKETS &&
            bg->GetBracketId() != BattleGroundBracketId(bracketIndex))
            continue;
        if (bg->GetStatus() != STATUS_WAIT_JOIN && bg->GetStatus() != STATUS_IN_PROGRESS)
            continue;
        uint32_t maxPerTeam = bg->GetMaxPlayersPerTeam();
        if (!maxPerTeam)
            maxPerTeam = AutonomousBgTeamTarget();
        BattleGround::BattleGroundPlayerMap const& players = bg->GetPlayers();
        if (players.empty())
            continue;
        bool botOnly = true;
        uint32_t inAlliance = 0;
        uint32_t inHorde = 0;
        for (BattleGround::BattleGroundPlayerMap::value_type const& pair : players)
        {
            Player* member = sObjectAccessor.FindPlayer(pair.first);
            if (!member || !member->IsInWorld() || !member->GetSession() ||
                !member->GetSession()->IsHeadless() ||
                !BotManager::Instance().IsRandomBot(pair.first))
            {
                botOnly = false;
                break;
            }
            if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(member))
                if (ai->HasActivePlayerMaster())
                {
                    botOnly = false;
                    break;
                }
            if (uint32(member->GetTeam()) == uint32(ALLIANCE))
                ++inAlliance;
            else if (uint32(member->GetTeam()) == uint32(HORDE))
                ++inHorde;
        }
        if (!botOnly)
            continue;
        // Both sides at max: full, closed to top-up. Anything less is still
        // forming and keeps absorbing queued seeds.
        if (inAlliance >= maxPerTeam && inHorde >= maxPerTeam)
            ++count;
    }
    return count;
}

uint32_t BattlegroundQueueService::CountOwnedQueuedFor(uint32_t queueTypeValue, uint32_t bracketIndex) const
{
    if (queueTypeValue >= MAX_BATTLEGROUND_QUEUE_TYPES || bracketIndex >= MAX_BATTLEGROUND_BRACKETS)
        return 0;
    BattleGroundQueueTypeId queueType = BattleGroundQueueTypeId(queueTypeValue);
    BattleGroundTypeId bgType = sServerFacade.BGTemplateId(queueType);
    uint32_t count = 0;
    // Service-owned seeds only: entries this service queued (m_ownedQueuedGuids
    // as (guidLow, queueType) pairs). Demand backfill and party/manual queues
    // belong to other paths and must not steer the seed bracket.
    for (uint64_t key : m_ownedQueuedGuids)
    {
        if (uint32_t(key & 0xFFFFFFFFu) != queueTypeValue)
            continue;
        ObjectGuid guid(HIGHGUID_PLAYER, uint32_t(key >> 32));
        Player* p = sObjectAccessor.FindPlayer(guid);
        if (!p || !p->InBattleGroundQueueForBattleGroundQueueType(queueType))
            continue;
        if (p->GetBattleGroundBracketIdFromLevel(bgType) != BattleGroundBracketId(bracketIndex))
            continue;
        ++count;
    }
    return count;
}

void BattlegroundQueueService::CountSeededForTeams(uint32_t queueTypeValue, uint32_t bracketIndex,
    uint32_t& seededAlliance, uint32_t& seededHorde) const
{
    seededAlliance = 0;
    seededHorde = 0;
    if (queueTypeValue >= MAX_BATTLEGROUND_QUEUE_TYPES || bracketIndex >= MAX_BATTLEGROUND_BRACKETS)
        return;
    BattleGroundQueueTypeId queueType = BattleGroundQueueTypeId(queueTypeValue);
    BattleGroundTypeId bgType = sServerFacade.BGTemplateId(queueType);
    // Queued seeds for this bracket, both teams in one pass.
    for (Player* p : BotManager::Instance().GetAllBots())
    {
        if (!p || !p->InBattleGroundQueueForBattleGroundQueueType(queueType))
            continue;
        if (p->GetBattleGroundBracketIdFromLevel(bgType) != BattleGroundBracketId(bracketIndex))
            continue;
        if (uint32(p->GetTeam()) == uint32(ALLIANCE))
            ++seededAlliance;
        else if (uint32(p->GetTeam()) == uint32(HORDE))
            ++seededHorde;
    }
    // In-match bots of a still-forming bot-only instance count toward the
    // target: the queued seeds plus the players already inside must reach
    // 10v10 together, and the core's free-slot path keeps inviting the queued
    // seeds in while either side is below max. A FULL instance (both sides at
    // max) absorbs nothing more, so it contributes nothing here — the
    // bracket-scoped cap in UpdateAutonomousSeeding stops new seeds.
    for (auto it = sBattleGroundMgr.GetBattleGroundsBegin(BATTLEGROUND_WS);
        it != sBattleGroundMgr.GetBattleGroundsEnd(BATTLEGROUND_WS); ++it)
    {
        BattleGround* bg = it->second;
        if (!bg || bg->GetStatus() == STATUS_WAIT_LEAVE)
            continue;
        if (bg->GetBracketId() != BattleGroundBracketId(bracketIndex))
            continue;
        if (bg->GetStatus() != STATUS_WAIT_JOIN && bg->GetStatus() != STATUS_IN_PROGRESS)
            continue;
        uint32_t maxPerTeam = bg->GetMaxPlayersPerTeam();
        if (!maxPerTeam)
            maxPerTeam = AutonomousBgTeamTarget();
        BattleGround::BattleGroundPlayerMap const& players = bg->GetPlayers();
        if (players.empty())
            continue;
        // Bot-only instance: every player a service Headless random bot.
        bool botOnly = true;
        uint32_t inAlliance = 0;
        uint32_t inHorde = 0;
        for (BattleGround::BattleGroundPlayerMap::value_type const& pair : players)
        {
            Player* inMember = sObjectAccessor.FindPlayer(pair.first);
            if (!inMember || !inMember->IsInWorld() || !inMember->GetSession() ||
                !inMember->GetSession()->IsHeadless() ||
                !BotManager::Instance().IsRandomBot(pair.first))
            {
                botOnly = false;
                break;
            }
            if (PlayerbotAI* inAi = PlayerbotAIStorage::Instance().GetAI(inMember))
                if (inAi->HasActivePlayerMaster())
                {
                    botOnly = false;
                    break;
                }
            if (uint32(inMember->GetTeam()) == uint32(ALLIANCE))
                ++inAlliance;
            else if (uint32(inMember->GetTeam()) == uint32(HORDE))
                ++inHorde;
        }
        if (!botOnly)
            continue;
        // Full on both sides: absorbs nothing more; the cap path stops seeds.
        if (inAlliance >= maxPerTeam && inHorde >= maxPerTeam)
            continue;
        seededAlliance += inAlliance;
        seededHorde += inHorde;
    }
}

void BattlegroundQueueService::UpdateAutonomousSeeding()
{
    if (!sPlayerbotAIConfig.randomBotBgAutonomous)
        return;
    // Pool reset in progress: same guard as the demand path (issue #265).
    if (!RandomBotService::Instance().IsPoolAvailable())
        return;
    uint32_t maxPerInterval = sPlayerbotAIConfig.randomBotBgMaxQueuePerInterval;
    if (!maxPerInterval)
        return;
    if (maxPerInterval > 10)
        maxPerInterval = 10;

    // Single in-memory snapshot for the whole tick: per-bracket eligible
    // counts per team plus the reusable candidate list (no 8x pool scans,
    // no per-candidate recounts). Readiness counts solo bots only — the queue
    // loop never seeds grouped bots, so grouped leaders must not make a
    // bracket look ready.
    uint32_t wsgQueue = uint32(BATTLEGROUND_QUEUE_WS);
    std::vector<Player*> snapshot = BotManager::Instance().GetAllBots();
    uint32_t eligible[MAX_BATTLEGROUND_BRACKETS][2] = {};
    std::vector<Player*> wsgEligible;
    wsgEligible.reserve(snapshot.size());
    for (Player* p : snapshot)
    {
        if (!IsEligible(p))
            continue;
        if (!p->GetBGAccessByLevel(BATTLEGROUND_WS))
            continue;
        BattleGroundBracketId bracket = p->GetBattleGroundBracketIdFromLevel(BATTLEGROUND_WS);
        if (bracket < 0 || bracket >= MAX_BATTLEGROUND_BRACKETS)
            continue;
        if (p->GetGroup())
            continue;
        uint32 team = uint32(p->GetTeam());
        uint32 side = (team == uint32(ALLIANCE)) ? 0 : (team == uint32(HORDE) ? 1 : 2);
        if (side > 1)
            continue;
        ++eligible[uint32(bracket)][side];
        wsgEligible.push_back(p);
    }

    // Sticky seed bracket: while service-owned seeds wait in a bracket, keep
    // filling it so 1-per-tick batches converge instead of scattering.
    // Only when no seeds are queued anywhere pick the most populated ready
    // bracket (both teams >= target, so a 19/1 bracket never seeds).
    uint32_t bestBracket = 0;
    bool found = false;
    for (uint32_t b = 0; b < MAX_BATTLEGROUND_BRACKETS; ++b)
    {
        if (CountOwnedQueuedFor(wsgQueue, b) > 0)
        {
            bestBracket = b;
            found = true;
            break;
        }
    }
    if (!found)
    {
        uint32_t bestTotal = 0;
        for (uint32_t b = 0; b < MAX_BATTLEGROUND_BRACKETS; ++b)
        {
            if (!BracketCanSeed(eligible[b][0], eligible[b][1]))
                continue;
            uint32_t total = eligible[b][0] + eligible[b][1];
            if (total > bestTotal)
            {
                bestTotal = total;
                bestBracket = b;
                found = true;
            }
        }
    }
    if (!found)
        return;

    // Bracket-scoped cap: a FULL bot-only instance in THIS bracket blocks new
    // seeds for it; a still-forming match keeps absorbing queued seeds, so it
    // does not count. Other brackets are unaffected.
    uint32_t running = CountRunningBotOnlyWsg(bestBracket);
    uint32_t seededAlliance = 0;
    uint32_t seededHorde = 0;
    CountSeededForTeams(wsgQueue, bestBracket, seededAlliance, seededHorde);
    bool needAlliance = TeamNeedsSeedBots(seededAlliance);
    bool needHorde = TeamNeedsSeedBots(seededHorde);
    if (!ShouldSeedAutonomousBg(true, false, running, needAlliance || needHorde,
        sPlayerbotAIConfig.randomBotBgAutonomousMaxInstances))
        return;

    // Candidates for the seed bracket, shuffled so the same bots are not
    // always picked. Solo-only: groups stay out of autonomous seeds (a group
    // leader queueing would silently pull members into a bot-only match).
    std::vector<Player*> candidates;
    candidates.reserve(wsgEligible.size());
    for (Player* p : wsgEligible)
    {
        if (p->GetBattleGroundBracketIdFromLevel(BATTLEGROUND_WS) != BattleGroundBracketId(bestBracket))
            continue;
        if (p->GetGroup())
            continue;
        candidates.push_back(p);
    }
    if (candidates.empty())
        return;
    for (size_t i = candidates.size() - 1; i > 0; --i)
    {
        size_t j = urand(0, static_cast<uint32>(i));
        std::swap(candidates[i], candidates[j]);
    }

    // Local per-team counters: computed once above, incremented on each
    // success — no per-candidate recount over the pool and BG maps.
    uint32_t queued = 0;
    for (Player* bot : candidates)
    {
        if (queued >= maxPerInterval)
            break;
        if (!IsEligible(bot) || bot->GetGroup())
            continue;
        bool isAlliance = uint32(bot->GetTeam()) == uint32(ALLIANCE);
        bool isHorde = uint32(bot->GetTeam()) == uint32(HORDE);
        if (!isAlliance && !isHorde)
            continue;
        uint32_t seeded = isAlliance ? seededAlliance : seededHorde;
        if (!TeamNeedsSeedBots(seeded))
            continue;
        if (TryQueue(bot, wsgQueue))
        {
            ++queued;
            if (isAlliance)
                ++seededAlliance;
            else
                ++seededHorde;
        }
    }

    if (queued)
        TB_LOG_DEBUG("TortoiseBots: BG autonomous seed queued %u/%u WSG bracket %u (running %u)",
            queued, maxPerInterval, bestBracket, running);
}

void BattlegroundQueueService::Shutdown()
{
    if (!m_initialized)
        return;
    for (uint64_t key : m_ownedQueuedGuids)
        BotActivityLeaseManager::Instance().Release(uint32_t(key >> 32), BotActivity::BgQueued);
    m_ownedQueuedGuids.clear();
    m_initialized = false;
    m_elapsedMs = 0;
}

void BattlegroundQueueService::OnLeaseEvicted(uint32_t guidLow)
{
    if (!guidLow)
        return;
    ObjectGuid guid(HIGHGUID_PLAYER, guidLow);
    Player* bot = sObjectAccessor.FindPlayer(guid);
    // Cancel every owned native queue entry for this guid via the existing
    // native leave path, then prune ownership. Never touches the lease map.
    for (uint32 i = 1; i < MAX_BATTLEGROUND_QUEUE_TYPES; ++i)
    {
        BattleGroundQueueTypeId q = BattleGroundQueueTypeId(i);
        uint64_t key = EncodeOwnedKey(guidLow, uint32_t(q));
        if (m_ownedQueuedGuids.find(key) == m_ownedQueuedGuids.end())
            continue;
        BattleGroundTypeId bgType = sServerFacade.BGTemplateId(q);
        if (bgType != BATTLEGROUND_WS && bgType != BATTLEGROUND_AB && bgType != BATTLEGROUND_AV)
            continue;
        if (bot && bot->InBattleGroundQueueForBattleGroundQueueType(q))
        {
            BattleGround* bg = sBattleGroundMgr.GetBattleGroundTemplate(bgType);
            uint32 mapId = bg ? bg->GetMapId() : 0;
            if (mapId)
            {
                WorldPacket packet(CMSG_BATTLEFIELD_PORT, 8);
                packet << mapId << uint8(0);
                if (WorldSession* sess = bot->GetSession())
                    sess->HandleBattleFieldPortOpcode(packet);
            }
        }
        m_ownedQueuedGuids.erase(key);
    }
}

} // namespace TortoiseBots
