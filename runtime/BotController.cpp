#include "BotController.h"
#include "../behavior/Config.h"
#include "../behavior/Movement.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "MotionMaster.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Map.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectGuid.h"

namespace TortoiseBots {

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename
BotController::BotController(ObjectGuid botGuid, ObjectGuid masterGuid)
    : m_botGuid(botGuid)
    , m_masterGuid(masterGuid)
    , m_intent(BotIntent::Follow)
    , m_nextUpdateMs(0)
{
}

// pi-lens-ignore: clang:incomplete_member_access
void BotController::SetMaster(ObjectGuid masterGuid)
{
    if (m_masterGuid == masterGuid)
        return;
    m_masterGuid = masterGuid;
    // Reset follow guard so next tick re-evaluates with new master.
    m_lastFollowTarget.Clear();
    m_lastFollowDist = -1.0f;
    m_lastFollowAngle = -1000.0f;
    m_loggedFollowing = false;
}

// pi-lens-ignore: clang:incomplete_member_access
void BotController::SetIntent(BotIntent intent)
{
    if (m_intent == intent)
        return;
    m_intent = intent;
    // Reset guard on intent change
    m_lastFollowTarget.Clear();
    m_lastFollowDist = -1.0f;
    m_lastFollowAngle = -1000.0f;
    m_loggedFollowing = false;
    // Minimal diagnostics for state transition
    if (intent == BotIntent::Follow)
        sLog.outString("TortoiseBots: Bot %s intent -> Follow master %s",
            m_botGuid.GetString().c_str(), m_masterGuid.GetString().c_str());
    else if (intent == BotIntent::None)
        sLog.outString("TortoiseBots: Bot %s intent -> None", m_botGuid.GetString().c_str());
}

// pi-lens-ignore: clang:incomplete_member_access
const char* BotController::GetIntentName() const
{
    switch (m_intent)
    {
        case BotIntent::Follow: return "Follow";
        case BotIntent::None: return "None";
        default: return "Unknown";
    }
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename
bool BotController::CanFollow(::Player* bot, ::Player* master) const
{
    if (!bot || !master)
        return false;
    if (!bot->IsInWorld() || !master->IsInWorld())
        return false;
    if (bot->IsBeingTeleported() || master->IsBeingTeleported())
        return false;
    if (bot->IsTaxiFlying() || master->IsTaxiFlying())
        return false;
    if (!bot->IsAlive())
        return false;
    // Player::CanFreeMove is not on Unit? Use HasUnitState check.
    // HasUnitState(UNIT_STAT_NO_FREE_MOVE) covers stun/root/fear etc.
    // For simplicity, check stunned/root via HasUnitState.
    if (bot->HasUnitState(UNIT_STAT_STUNNED | UNIT_STAT_ROOT | UNIT_STAT_FLEEING | UNIT_STAT_CONFUSED))
        return false;
    if (bot->HasUnitState(UNIT_STAT_TAXI_FLIGHT))
        return false;
    // Same map check — safe no-op if mismatch (task safety)
    if (bot->GetMap() != master->GetMap())
        return false;
    if (bot->GetMapId() != master->GetMapId())
        return false;
    // Self-follow guard
    if (bot->GetObjectGuid() == master->GetObjectGuid())
        return false;
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename,clang:undeclared_var_use
void BotController::DoFollow(::Player* bot, ::Player* master)
{
    const float dist = Config::FollowDistance;
    const float angle = Config::FollowAngle;

    // Distance check — dead-zone (cmangos FollowAction dead-zone 1.5y)
    float curDist = bot->GetDistance2d(master);
    if (curDist <= dist)
    {
        // Inside dead-zone: do not restart movement, no jitter.
        // Keep existing follow generator idle; just reset logged flag so next
        // time we leave the zone we log again.
        m_loggedFollowing = false;
        return;
    }

    // Restart guard: if already following same master with same dist/angle, no-op.
    // Mirrors cmangos FollowAction::isUseful guard:
    //   GetChaseTarget()==master && GetChaseAngle()==angle && GetChaseOffset()==dist
    if (m_lastFollowTarget == m_masterGuid &&
        m_lastFollowDist == dist &&
        m_lastFollowAngle == angle &&
        Movement::IsFollowing(bot, m_masterGuid, dist, angle))
    {
        return;
    }

    // Issue follow via typed movement boundary
    if (Movement::Follow(bot, master, dist, angle))
    {
        m_lastFollowTarget = m_masterGuid;
        m_lastFollowDist = dist;
        m_lastFollowAngle = angle;
        if (!m_loggedFollowing)
        {
            sLog.outString("TortoiseBots: Bot %s following %s dist %.1f angle %.2f curDist %.1f",
                bot->GetName(), master->GetName(), dist, angle, curDist);
            m_loggedFollowing = true;
        }
    }
    else
    {
        // Movement rejected (e.g., map mismatch, teleport) — safe no-op
        // Log at most once per failure to avoid spam
        if (!m_loggedFollowing)
        {
            sLog.outString("TortoiseBots: Bot %s follow target unavailable master %s",
                bot->GetName(), master->GetName());
            m_loggedFollowing = true;
        }
    }
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename
void BotController::Update(uint32 diff)
{
    // Throttle — mirrors PlayerbotAI::aiInternalUpdateDelay / reactDelay
    if (m_nextUpdateMs > diff)
    {
        m_nextUpdateMs -= diff;
        return;
    }
    // Reset throttle: follow tick 200ms, idle 1000ms will be set at end based on state,
    // but for now fixed 200ms as per Config::FollowUpdateIntervalMs
    m_nextUpdateMs = Config::FollowUpdateIntervalMs;

    if (m_intent == BotIntent::None)
        return;

    // Resolve bot and master each tick via ObjectAccessor (no long-lived Player*)
    ::Player* bot = sObjectAccessor.FindPlayer(m_botGuid);
    if (!bot)
        return;
    if (!bot->IsInWorld())
        return;

    ::Player* master = sObjectAccessor.FindPlayer(m_masterGuid);
    if (!master)
    {
        // Master disappeared/logged out — safe no-op, no crash (task safety)
        // Do not clear intent; will resume when master reappears.
        return;
    }

    if (m_intent == BotIntent::Follow)
    {
        if (!CanFollow(bot, master))
            return;
        DoFollow(bot, master);
    }
}

} // namespace TortoiseBots
