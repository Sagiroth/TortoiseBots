#include "PlayerConvenience.h"

#include "../runtime/BotManager.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Unit.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "GameObject.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Map.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldSession.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"

#include <cmath>
#include <cstdlib>

namespace TortoiseBots {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr uint32 kSummonDelayMs = 3000;
constexpr uint32 kSummonArrivalTimeoutMs = 10000;
constexpr float kMaxSummonGroundDrop = 4.0f;
}

PlayerConvenience& PlayerConvenience::Instance()
{
    static PlayerConvenience instance;
    return instance;
}

bool PlayerConvenience::RequestSummon(Player* requester, Player* bot)
{
    if (!requester || !bot || !requester->IsInWorld() || !requester->IsAlive() ||
        requester->IsBeingTeleported() || requester->IsTaxiFlying())
        return false;
    if (!bot->IsInWorld() || !bot->GetSession() || !bot->GetSession()->IsHeadless() ||
        bot->IsBeingTeleported() || !bot->IsAlive() || bot->IsInCombat() || bot->IsTaxiFlying())
        return false;

    BotRecord const* record = BotManager::Instance().FindBot(bot->GetObjectGuid());
    if (!record || record->masterGuid != requester->GetObjectGuid())
        return false;

    uint32 key = bot->GetObjectGuid().GetCounter();
    if (IsBusy(bot->GetObjectGuid()))
        return false;

    float angle = requester->getOrientation() + (std::rand() % 60 - 30) * kPi / 180.0f;
    constexpr float distance = 5.0f;
    float destX = requester->getPositionX() + std::cos(angle) * distance;
    float destY = requester->getPositionY() + std::sin(angle) * distance;
    float destZ = requester->getPositionZ();

    // The visual portal used by the first implementation was not part of the
    // Turtle data contract. Reuse the mature summon action's conservative
    // ground/line-of-sight policy instead, then fall back to the master's
    // exact position when there is no safe nearby point.
    requester->UpdateGroundPositionZ(destX, destY, destZ);
    if (std::fabs(destZ - requester->getPositionZ()) > kMaxSummonGroundDrop ||
        !requester->IsWithinLOS(destX, destY, destZ + bot->GetCollisionHeight(), true))
    {
        destX = requester->getPositionX();
        destY = requester->getPositionY();
        destZ = requester->getPositionZ();
    }

    constexpr uint32 kSummonPortalEntry = 179944; // Meeting Stone Summoning Portal (Spells\Ritual_Portal.mdx)
    GameObject* portal = requester->SummonGameObject(kSummonPortalEntry, destX, destY, destZ, requester->getOrientation(), 0.0f, 0.0f, 0.0f, 0.0f, 5, false);

    SummonState state;
    state.botGuid = bot->GetObjectGuid();
    state.masterGuid = requester->GetObjectGuid();
    state.destX = destX;
    state.destY = destY;
    state.destZ = destZ;
    state.destO = requester->getOrientation();
    state.destMap = requester->GetMapId();
    if (portal)
        state.portalGuid = portal->GetObjectGuid();
    m_summons.emplace(key, state);

    sLog.outString("TortoiseBots: Summon requested bot %s to %.1f,%.1f,%.1f map %u by %s",
        bot->GetName(), state.destX, state.destY, state.destZ, state.destMap, requester->GetName());
    return true;
}

bool PlayerConvenience::IsBusy(ObjectGuid botGuid) const
{
    uint32 key = botGuid.GetCounter();
    return m_summons.find(key) != m_summons.end();
}

void PlayerConvenience::Update(uint32 diff)
{
    UpdateSummons(diff);
}

void PlayerConvenience::UpdateSummons(uint32 diff)
{
    auto cleanupPortal = [](SummonState& s, Player* master) {
        if (!s.portalGuid.IsEmpty())
        {
            Map* map = master ? master->GetMap() : nullptr;
            if (map)
            {
                if (GameObject* go = map->GetGameObject(s.portalGuid))
                    go->Delete();
            }
            s.portalGuid.Clear();
        }
    };

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
            cleanupPortal(state, master);
            it = m_summons.erase(it);
            continue;
        }

        BotRecord const* record = BotManager::Instance().FindBot(state.botGuid);
        if (!record || record->masterGuid != state.masterGuid)
        {
            sLog.outString("TortoiseBots: Summon cancelled for bot %s because its master changed",
                state.botGuid.GetString().c_str());
            cleanupPortal(state, master);
            it = m_summons.erase(it);
            continue;
        }

        if (state.phase == SummonState::Phase::AwaitingArrival)
        {
            if (bot->GetSession() && bot->GetSession()->IsHeadless())
            {
                if (bot->IsBeingTeleportedNear())
                    bot->ExecuteTeleportNear();
                else if (bot->IsBeingTeleportedFar())
                    bot->GetSession()->HandleMoveWorldportAckOpcode();
            }

            if (!bot->IsInWorld() || bot->IsBeingTeleported())
            {
                if (state.elapsedMs > kSummonArrivalTimeoutMs)
                {
                    sLog.outError("TortoiseBots: Summon arrival timed out for bot %s",
                        state.botGuid.GetString().c_str());
                    cleanupPortal(state, master);
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
                cleanupPortal(state, master);
                it = m_summons.erase(it);
                continue;
            }

            if (bot->GetMapId() != state.destMap || !BotManager::Instance().SetBotFollow(state.botGuid, state.masterGuid))
            {
                sLog.outError("TortoiseBots: Summon follow restore failed for bot %s",
                    state.botGuid.GetString().c_str());
                cleanupPortal(state, master);
                it = m_summons.erase(it);
                continue;
            }

            sLog.outString("TortoiseBots: Summon completed for bot %s with follow restored to %s",
                bot->GetName(), master->GetName());
            cleanupPortal(state, master);
            it = m_summons.erase(it);
            continue;
        }

        if (!bot->IsInWorld())
        {
            cleanupPortal(state, master);
            it = m_summons.erase(it);
            continue;
        }

        if (!bot->IsAlive() || bot->IsBeingTeleported() || bot->IsTaxiFlying() || bot->IsInCombat())
        {
            sLog.outString("TortoiseBots: Summon cancelled bot %s became incompatible before teleport",
                bot->GetName());
            cleanupPortal(state, master);
            it = m_summons.erase(it);
            continue;
        }

        if (state.elapsedMs < kSummonDelayMs)
        {
            ++it;
            continue;
        }
        if (bot->IsInCombat())
        {
            sLog.outString("TortoiseBots: Summon aborted bot %s still in combat", bot->GetName());
            cleanupPortal(state, master);
            it = m_summons.erase(it);
            continue;
        }

        cleanupPortal(state, master);
        bot->GetMotionMaster()->Clear(false);
        bot->StopMoving();
        if (!bot->TeleportTo(state.destMap, state.destX, state.destY, state.destZ, state.destO, 0))
        {
            // Do not enter the arrival phase when the core rejected the
            // teleport. Otherwise the next tick would treat the unchanged
            // in-world position as a successful summon and restore Follow.
            sLog.outError("TortoiseBots: Summon teleport rejected for bot %s",
                bot->GetName());
            it = m_summons.erase(it);
            continue;
        }
        if (bot->GetSession() && bot->GetSession()->IsHeadless())
        {
            if (bot->IsBeingTeleportedNear())
                bot->ExecuteTeleportNear();
            else if (bot->IsBeingTeleportedFar())
                bot->GetSession()->HandleMoveWorldportAckOpcode();
        }
        state.phase = SummonState::Phase::AwaitingArrival;
        state.elapsedMs = 0;
        sLog.outString("TortoiseBots: Summon teleport requested for bot %s to master %s",
            bot->GetName(), master->GetName());
        ++it;
    }
}

} // namespace TortoiseBots
