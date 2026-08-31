#include "PlayerConvenience.h"

#include "../runtime/BotManager.h"
#include "../runtime/PlayerbotAIStorage.h"
#include "../ai/playerbot/PlayerbotAI.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "Creature.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "MotionMaster.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Unit.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldSession.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"

#include <cmath>
#include <cstdlib>

namespace TortoiseBots {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr uint32 kPullbackTimeoutMs = 30000;
constexpr uint32 kSummonDelayMs = 3000;
constexpr uint32 kSummonArrivalTimeoutMs = 10000;
}

PlayerConvenience& PlayerConvenience::Instance()
{
    static PlayerConvenience instance;
    return instance;
}

bool PlayerConvenience::RequestPullback(Player* requester, Player* tank, Unit* target, bool isRanged, float desiredDist)
{
    if (!requester || !tank || !target)
        return false;
    if (!requester->IsInWorld() || !requester->IsAlive() || requester->IsBeingTeleported() ||
        requester->IsTaxiFlying() || !tank->IsInWorld() || !tank->IsAlive() ||
        tank->IsBeingTeleported() || tank->IsTaxiFlying() || tank->IsInCombat() ||
        !tank->GetSession() || !tank->GetSession()->IsHeadless() || !target->IsInWorld() ||
        !target->IsAlive() || target->IsInCombat() || tank->GetMap() != target->GetMap() ||
        requester->GetMap() != tank->GetMap())
        return false;

    uint32 key = tank->GetObjectGuid().GetCounter();
    if (IsBusy(tank->GetObjectGuid()))
        return false;

    PullbackState state;
    state.tankGuid = tank->GetObjectGuid();
    state.masterGuid = requester->GetObjectGuid();
    state.targetGuid = target->getObjectGuid();
    state.targetEntry = target->GetTypeId() == TYPEID_UNIT ? static_cast<Creature*>(target)->GetEntry() : 0;
    state.anchorX = requester->getPositionX();
    state.anchorY = requester->getPositionY();
    state.anchorZ = requester->getPositionZ();
    state.anchorMap = requester->GetMapId();
    state.desiredDist = desiredDist > 0.1f ? desiredDist : (isRanged ? 28.0f : 12.0f);
    state.isRanged = isRanged;
    m_pullbacks.emplace(key, state);

    sLog.outString("TortoiseBots: Pullback requested tank %s -> target %s entry %u ranged %u dist %.1f anchor %.1f,%.1f,%.1f",
        tank->GetName(), target->GetName(), state.targetEntry, isRanged ? 1 : 0, state.desiredDist,
        state.anchorX, state.anchorY, state.anchorZ);
    return true;
}

bool PlayerConvenience::RequestSummon(Player* requester, Player* bot)
{
    if (!requester || !bot || !requester->IsInWorld() || !requester->IsAlive() ||
        requester->IsBeingTeleported() || requester->IsTaxiFlying())
        return false;
    if (!bot->IsInWorld() || !bot->GetSession() || !bot->GetSession()->IsHeadless() ||
        bot->IsBeingTeleported() || !bot->IsAlive() || bot->IsInCombat() || bot->IsTaxiFlying())
        return false;

    uint32 key = bot->GetObjectGuid().GetCounter();
    if (IsBusy(bot->GetObjectGuid()))
        return false;

    float angle = requester->getOrientation() + (std::rand() % 60 - 30) * kPi / 180.0f;
    constexpr float distance = 5.0f;

    SummonState state;
    state.botGuid = bot->GetObjectGuid();
    state.masterGuid = requester->GetObjectGuid();
    state.destX = requester->getPositionX() + std::cos(angle) * distance;
    state.destY = requester->getPositionY() + std::sin(angle) * distance;
    state.destZ = requester->getPositionZ() + 0.5f;
    state.destO = requester->getOrientation();
    state.destMap = requester->GetMapId();
    m_summons.emplace(key, state);

    sLog.outString("TortoiseBots: Summon requested bot %s to %.1f,%.1f,%.1f map %u by %s",
        bot->GetName(), state.destX, state.destY, state.destZ, state.destMap, requester->GetName());
    return true;
}

bool PlayerConvenience::IsBusy(ObjectGuid botGuid) const
{
    uint32 key = botGuid.GetCounter();
    return m_pullbacks.find(key) != m_pullbacks.end() || m_summons.find(key) != m_summons.end();
}

void PlayerConvenience::Update(uint32 diff)
{
    UpdatePullbacks(diff);
    UpdateSummons(diff);
}

void PlayerConvenience::UpdatePullbacks(uint32 diff)
{
    for (auto it = m_pullbacks.begin(); it != m_pullbacks.end(); )
    {
        PullbackState& state = it->second;
        state.elapsedMs += diff;
        state.phaseElapsedMs += diff;
        if (state.elapsedMs > kPullbackTimeoutMs)
        {
            sLog.outString("TortoiseBots: Pullback timeout tank %s", state.tankGuid.GetString().c_str());
            it = m_pullbacks.erase(it);
            continue;
        }

        Player* tank = sObjectAccessor.FindPlayer(state.tankGuid);
        if (!tank || !tank->IsInWorld() || !tank->IsAlive() || tank->IsBeingTeleported() ||
            tank->IsTaxiFlying() || !tank->GetSession() || !tank->GetSession()->IsHeadless())
        {
            it = m_pullbacks.erase(it);
            continue;
        }

        Player* master = sObjectAccessor.FindPlayer(state.masterGuid);
        BotRecord const* record = BotManager::Instance().FindBot(state.tankGuid);
        if (!master || !master->IsInWorld() || !master->IsAlive() || master->IsBeingTeleported() ||
            master->IsTaxiFlying() || master->GetMapId() != state.anchorMap ||
            master->GetMap() != tank->GetMap() ||
            !record || record->masterGuid != state.masterGuid)
        {
            sLog.outString("TortoiseBots: Pullback cancelled for tank %s because its master is no longer eligible",
                state.tankGuid.GetString().c_str());
            it = m_pullbacks.erase(it);
            continue;
        }

        Unit* target = nullptr;
        if (!state.targetGuid.IsEmpty())
        {
            if (state.targetGuid.IsPlayer())
                target = sObjectAccessor.FindPlayer(state.targetGuid);
            else if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(tank))
                target = ai->GetUnit(state.targetGuid);
        }

        if (!target || !target->IsAlive() || !target->IsInWorld() || tank->GetMap() != target->GetMap())
        {
            if (state.phase == PullbackState::Phase::Holding && state.phaseElapsedMs > 3000)
            {
                sLog.outString("TortoiseBots: Pullback holding done (target gone) tank %s", tank->GetName());
                it = m_pullbacks.erase(it);
                continue;
            }
            if (state.phase != PullbackState::Phase::Approaching || state.elapsedMs > 8000)
            {
                sLog.outString("TortoiseBots: Pullback lost target tank %s", tank->GetName());
                it = m_pullbacks.erase(it);
                continue;
            }
        }

        bool tankInCombat = tank->IsInCombat();
        bool targetInCombat = target && target->IsInCombat();
        switch (state.phase)
        {
            case PullbackState::Phase::Approaching:
            {
                if (!target)
                {
                    if (state.phaseElapsedMs > 10000)
                        it = m_pullbacks.erase(it);
                    else
                        ++it;
                    continue;
                }

                float distanceToTarget = tank->GetDistance(target);
                if (tankInCombat || targetInCombat)
                {
                    state.phase = PullbackState::Phase::Returning;
                    state.phaseElapsedMs = 0;
                    sLog.outString("TortoiseBots: Pullback approaching -> returning (combat) tank %s dist %.1f",
                        tank->GetName(), distanceToTarget);
                    ++it;
                    continue;
                }
                if (distanceToTarget <= state.desiredDist + 2.0f)
                {
                    state.phase = state.isRanged ? PullbackState::Phase::Pulling : PullbackState::Phase::Returning;
                    state.phaseElapsedMs = 0;
                    sLog.outString("TortoiseBots: Pullback approaching -> %s tank %s",
                        state.isRanged ? "pulling (ranged)" : "returning (melee)", tank->GetName());
                    ++it;
                    continue;
                }
                if (state.phaseElapsedMs < 400 && tank->GetMotionMaster() &&
                    tank->GetMotionMaster()->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE)
                {
                    ++it;
                    continue;
                }

                state.phaseElapsedMs = 0;
                float angle = target->GetAngle(tank);
                float x = target->getPositionX() + std::cos(angle) * state.desiredDist;
                float y = target->getPositionY() + std::sin(angle) * state.desiredDist;
                float z = target->getPositionZ() + 0.3f;
                if (MotionMaster* movement = tank->GetMotionMaster())
                {
                    movement->Clear();
                    movement->MovePoint(0, x, y, z);
                }
                ++it;
                continue;
            }
            case PullbackState::Phase::Pulling:
            {
                if (!target || targetInCombat || tankInCombat)
                {
                    state.phase = PullbackState::Phase::Returning;
                    state.phaseElapsedMs = 0;
                    ++it;
                    continue;
                }
                if (state.phaseElapsedMs > 800 || state.phaseElapsedMs == diff)
                {
                    bool didCast = false;
                    if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(tank))
                    {
                        char const* attempts[] = { "shoot", "shoot bow", "shoot gun", "shoot crossbow", "throw", nullptr };
                        for (char const** attempt = attempts; *attempt; ++attempt)
                        {
                            if (!ai->CanCastSpell(*attempt, target, true, nullptr, true))
                                continue;

                            ai::Event event("pullback", target->getObjectGuid());
                            if (ai->DoSpecificAction(*attempt, event, true))
                            {
                                didCast = true;
                                sLog.outString("TortoiseBots: Pullback ranged cast %s tank %s -> %s",
                                    *attempt, tank->GetName(), target->GetName());
                                break;
                            }
                        }
                    }
                    if (didCast)
                    {
                        state.phase = PullbackState::Phase::Returning;
                        state.phaseElapsedMs = 0;
                        ++it;
                        continue;
                    }
                    if (state.phaseElapsedMs > 3000)
                    {
                        sLog.outString("TortoiseBots: Pullback pulling timeout tank %s, fallback return", tank->GetName());
                        state.phase = PullbackState::Phase::Returning;
                        state.phaseElapsedMs = 0;
                    }
                }
                if (state.phaseElapsedMs > 8000)
                {
                    state.phase = PullbackState::Phase::Returning;
                    state.phaseElapsedMs = 0;
                }
                ++it;
                continue;
            }
            case PullbackState::Phase::Returning:
            {
                float distanceToAnchor = tank->GetDistance(state.anchorX, state.anchorY, state.anchorZ);
                if (distanceToAnchor <= 4.0f)
                {
                    if (MotionMaster* movement = tank->GetMotionMaster())
                        movement->Clear();
                    tank->StopMoving();
                    if (target)
                        tank->SetFacingTo(tank->GetAngle(target));
                    state.phase = PullbackState::Phase::Holding;
                    state.phaseElapsedMs = 0;
                    sLog.outString("TortoiseBots: Pullback returning -> holding tank %s at anchor", tank->GetName());
                    ++it;
                    continue;
                }
                if (state.phaseElapsedMs < 300 && tank->GetMotionMaster() &&
                    tank->GetMotionMaster()->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE)
                {
                    ++it;
                    continue;
                }
                if (state.phaseElapsedMs > 500 || distanceToAnchor > 5.0f)
                {
                    state.phaseElapsedMs = 0;
                    if (MotionMaster* movement = tank->GetMotionMaster())
                    {
                        movement->Clear();
                        movement->MovePoint(1, state.anchorX, state.anchorY, state.anchorZ);
                    }
                }
                ++it;
                continue;
            }
            case PullbackState::Phase::Holding:
            {
                if (target && target->IsAlive() && target->IsInWorld())
                {
                    tank->SetFacingTo(tank->GetAngle(target));
                    if (state.phaseElapsedMs % 1000 < diff)
                        tank->SendHeartBeat();
                }
                float distanceToAnchor = tank->GetDistance(state.anchorX, state.anchorY, state.anchorZ);
                if (distanceToAnchor > 5.0f && state.phaseElapsedMs > 1000)
                {
                    state.phase = PullbackState::Phase::Returning;
                    state.phaseElapsedMs = 0;
                    ++it;
                    continue;
                }
                if ((!tankInCombat && !targetInCombat && state.phaseElapsedMs > 2000) ||
                    ((!target || !target->IsAlive()) && state.phaseElapsedMs > 2000))
                {
                    sLog.outString("TortoiseBots: Pullback holding done tank %s", tank->GetName());
                    it = m_pullbacks.erase(it);
                    continue;
                }
                ++it;
                continue;
            }
        }
    }
}

void PlayerConvenience::UpdateSummons(uint32 diff)
{
    for (auto it = m_summons.begin(); it != m_summons.end(); )
    {
        SummonState& state = it->second;
        state.elapsedMs += diff;

        Player* bot = sObjectAccessor.FindPlayer(state.botGuid);
        Player* master = sObjectAccessor.FindPlayer(state.masterGuid);
        if (!bot || !bot->GetSession() || !bot->GetSession()->IsHeadless() ||
            !master || !master->IsInWorld() || !master->IsAlive() || master->IsBeingTeleported() ||
            master->IsTaxiFlying() ||
            master->GetMapId() != state.destMap)
        {
            it = m_summons.erase(it);
            continue;
        }

        BotRecord const* record = BotManager::Instance().FindBot(state.botGuid);
        if (!record || record->masterGuid != state.masterGuid)
        {
            sLog.outString("TortoiseBots: Summon cancelled for bot %s because its master changed",
                state.botGuid.GetString().c_str());
            it = m_summons.erase(it);
            continue;
        }

        if (state.phase == SummonState::Phase::AwaitingArrival)
        {
            if (!bot->IsInWorld() || bot->IsBeingTeleported())
            {
                if (state.elapsedMs > kSummonArrivalTimeoutMs)
                {
                    sLog.outError("TortoiseBots: Summon arrival timed out for bot %s",
                        state.botGuid.GetString().c_str());
                    it = m_summons.erase(it);
                }
                else
                    ++it;
                continue;
            }

            if (!bot->IsAlive() || bot->IsTaxiFlying() || bot->IsInCombat())
            {
                sLog.outString("TortoiseBots: Summon cancelled bot %s became incompatible before follow restore",
                    bot->GetName());
                it = m_summons.erase(it);
                continue;
            }

            if (bot->GetMapId() != state.destMap || !BotManager::Instance().SetBotFollow(state.botGuid, state.masterGuid))
            {
                sLog.outError("TortoiseBots: Summon follow restore failed for bot %s",
                    state.botGuid.GetString().c_str());
                it = m_summons.erase(it);
                continue;
            }

            sLog.outString("TortoiseBots: Summon completed for bot %s with follow restored to %s",
                bot->GetName(), master->GetName());
            it = m_summons.erase(it);
            continue;
        }

        if (!bot->IsInWorld())
        {
            it = m_summons.erase(it);
            continue;
        }

        if (!bot->IsAlive() || bot->IsBeingTeleported() || bot->IsTaxiFlying() || bot->IsInCombat())
        {
            sLog.outString("TortoiseBots: Summon cancelled bot %s became incompatible before teleport",
                bot->GetName());
            it = m_summons.erase(it);
            continue;
        }

        if (!state.portalSpawned)
        {
            state.portalSpawned = true;
            if (Creature* portal = master->SummonCreature(1, state.destX, state.destY, state.destZ, state.destO,
                TEMPSUMMON_TIMED_DESPAWN, kSummonDelayMs))
            {
                state.portalGuid = portal->getObjectGuid();
                portal->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
            }
            else if (Creature* fallbackPortal = master->SummonCreature(20206, state.destX, state.destY, state.destZ, state.destO,
                TEMPSUMMON_TIMED_DESPAWN, kSummonDelayMs))
            {
                state.portalGuid = fallbackPortal->getObjectGuid();
            }
            sLog.outString("TortoiseBots: Summon portal spawned for bot %s at %.1f,%.1f",
                bot->GetName(), state.destX, state.destY);
        }

        if (state.elapsedMs < kSummonDelayMs)
        {
            ++it;
            continue;
        }
        if (bot->IsInCombat())
        {
            sLog.outString("TortoiseBots: Summon aborted bot %s still in combat", bot->GetName());
            it = m_summons.erase(it);
            continue;
        }

        if (MotionMaster* movement = bot->GetMotionMaster())
            movement->Clear();
        bot->StopMoving();
        bot->TeleportTo(state.destMap, state.destX, state.destY, state.destZ, state.destO, 0);
        state.phase = SummonState::Phase::AwaitingArrival;
        state.elapsedMs = 0;
        sLog.outString("TortoiseBots: Summon teleport requested for bot %s to master %s",
            bot->GetName(), master->GetName());
        ++it;
    }
}

} // namespace TortoiseBots
