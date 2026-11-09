#include <cmath>
#include "playerbot/playerbot.h"
#include "FollowActions.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/Formations.h"
#include "playerbot/strategy/values/FreeMoveValues.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/LootObjectStack.h"
#include "playerbot/JumpPolicy.h"
#include "playerbot/CastKeepsRunningPolicy.h"
#include "playerbot/DungeonFollowHoldPolicy.h"
#include "playerbot/DungeonFollowStallPolicy.h"
using namespace ai;

bool FollowAction::Execute(Event& event)
{
    bool moved = false;
    Unit* followTarget = AI_VALUE(Unit*, "follow target");
    Formation* formation = AI_VALUE(Formation*, "formation");
    // Follow-first: a catch-up follow breaks downtime at once (drink/food
    // auras drop on standing, casts stop below). Follow() stands the bot up
    // and interrupts itself, but only after the rescue/safety branches; do
    // it here so every follow path starts moving this tick.
    if (followTarget && followTarget != bot && !bot->IsInCombat() &&
        bot->GetStandState() != UNIT_STAND_STATE_STAND)
        bot->SetStandState(UNIT_STAND_STATE_STAND);

    // BoostFollow: match the mount state instead of speed-hacking. A mounted
    // master outruns an unmounted bot at any speed multiplier, and UpdateSpeed
    // cheats leak into combat. The mount action already owns mounting rules;
    // here we only make sure a far-behind bot is eligible to mount up.
    if (sPlayerbotAIConfig.boostFollow && followTarget && followTarget != bot &&
        !bot->IsMounted() && !bot->IsInCombat() &&
        sServerFacade.IsDistanceGreaterThan(sServerFacade.getDistance2d(bot, followTarget), sPlayerbotAIConfig.reactDistance))
        ai->DoSpecificAction("check mount state", Event(), true);

    if (FollowHoldingBeforePacks(followTarget, moved))
        return moved;

    if (RescueStalledOpenFollow(followTarget, moved))
        return moved;
    if (RescueStalledDungeonFollow(followTarget, moved))
        return moved;

    // A far follower in a raid or dungeon walks the leader's own path: the
    // direct route to its formation slot cuts the inside of stairs and corners
    // and crosses what the leader's route avoided.
    if (WalkLeadTrail(followTarget, moved))
        return moved;

    if (ai->IsSafe(followTarget))
    {
        if (formation)
        {
            WorldLocation loc = formation->GetLocation();
            if (!Formation::IsNullLocation(loc) && loc.mapId != -1)
            {
                moved = Follow(followTarget, formation->getOffset(), formation->GetAngle());
            }
        }
    }
    else
        moved = Follow(followTarget, 0, 0);

    // Beyond sight range in a dungeon Follow's MoveTo can fail without a
    // word; arm the stall rescue then (DungeonFollowStallWatch::ArmFar).
    if (!moved && ArmFarDungeonFollowRescue(followTarget))
        RescueStalledDungeonFollow(followTarget, moved);

    return moved;
}

// Records the leader's positions and, when the follower is far behind with
// nobody fighting, walks the recorded trail (FollowTrailPolicy.h). Returns true
// only when a trail walk launched, so the normal follow keeps the tick
// otherwise. Off outside the raid/dungeon wedge, and the trail is dropped when
// the pair leaves the instance or the leader stops being a valid follow target.
bool FollowAction::WalkLeadTrail(Unit* followTarget, bool& moved)
{
    moved = false;
    if (!UsesRaidFollowWedge(ai) || !followTarget || followTarget == bot || !followTarget->IsPlayer())
        return false;
    if (!bot->IsAlive() || !followTarget->IsAlive() || !ai->IsSafe(followTarget) ||
        bot->GetMapId() != followTarget->GetMapId() || bot->GetTransport() ||
        followTarget->GetTransport() || bot->IsTaxiFlying())
        return false;
    if (bot->IsInCombat() || followTarget->IsInCombat())
        return false;
    if (!sServerFacade.IsDistanceGreaterThan(sServerFacade.getDistance2d(bot, followTarget),
            ai::kRaidFollowCatchUpDistanceYd))
        return false;

    std::vector<ai::FollowTrailPoint> const& trail = ai->GetLeadTrail();
    int const index = ai::FollowTrailWaypoint(trail, bot->GetPositionX(), bot->GetPositionY());
    if (index < 0 || static_cast<std::size_t>(index) >= trail.size())
        return false;

    ai::FollowTrailPoint const& point = trail[static_cast<std::size_t>(index)];
    moved = MoveTo2(WorldPosition(bot->GetMapId(), point.x, point.y, point.z));
    return moved;
}

// A follow past sight range that did not start moving, inside a dungeon and
// out of combat: latch the stall rescue (ledge step-down, then the paced
// MoveTo2 walks, bounded by the watch's give-up). Cheap: one distance.
bool FollowAction::ArmFarDungeonFollowRescue(Unit* followTarget)
{
    Map* map = bot->GetMap();
    if (!followTarget || followTarget == bot || !map || !followTarget->IsPlayer() || !bot->IsAlive() ||
        !followTarget->IsAlive() || !ai->IsSafe(followTarget) || bot->GetTransport() ||
        followTarget->GetTransport() || bot->IsTaxiFlying() || bot->GetMapId() != followTarget->GetMapId())
        return false;
    bool const inCombat = bot->IsInCombat() || ai->IsStateActive(BotState::BOT_STATE_COMBAT);
    bool const held = bot->IsRooted() || bot->HasUnitState(UNIT_STAT_NO_FREE_MOVE) ||
        bot->GetStandState() != UNIT_STAND_STATE_STAND || bot->IsNonMeleeSpellCasted(true, false, true);
    if (!UseFarFollowRescue(map->IsDungeon(), inCombat, bot->GetDistance(followTarget),
            sPlayerbotAIConfig.sightDistance, false, held))
        return false;
    return followStallWatch.ArmFar(WorldTimer::getMSTime());
}

bool FollowAction::isUseful()
{
    // A follow step breaks the Mining / Herb Gathering cast, and even a no-op
    // Follow() stops the cast before checking whether the bot needs to move.
    // Station-keeping ("update follow") fired every idle tick beside a node
    // and broke the 3.2 s cast it just started, so the node animated but never
    // opened (#1062). Finish the cast first; the next tick follows.
    if (ai::FollowBlockedByCast(bot->IsNonMeleeSpellCasted(false, true, true)))
        return false;
    if (!ai->CanMove())
        return false;
    // The portal-follow episode owns movement while it runs: it walks to the
    // portal, not to the master, and both would re-issue a MovePoint each tick.
    if (ai->PortalFollowActive())
        return false;
    if (ai->PartyFlightActive())
        return false;
    float distance = 0;
    Unit* followTarget = AI_VALUE(Unit*, "follow target");
    Formation* formation = AI_VALUE(Formation*, "formation");

    if (followTarget && followTarget->IsPlayer())
    {
        if (AI_VALUE(GuidPosition, "rpg target") && CanFreeMoveValue::CanFreeMoveTo(ai, AI_VALUE(GuidPosition, "rpg target")))
        {
            return false;
        }
    }

    if (followTarget)
    {
        if (followTarget->IsTaxiFlying() || !CanDeadFollow(followTarget) || followTarget->GetGUIDLow() == bot->GetGUIDLow())
        {
            return false;
        }
    }

    if (followTarget)
    {
        distance = sServerFacade.getDistance2d(bot, followTarget);
    }
    else
    {
        WorldLocation loc = formation->GetLocation();
        if (Formation::IsNullLocation(loc) || bot->GetMapId() != loc.mapId)
        {
            return false;
        }

        distance = sServerFacade.getDistance2d(bot, loc.x, loc.y);
    }

    if (sServerFacade.IsDistanceGreaterThan(distance, sPlayerbotAIConfig.sightDistance))
    {
        return true;
    }

    // maybe jump is faster?
    if (ai->HasStrategy("follow jump", BotState::BOT_STATE_NON_COMBAT) && ai->AllowActivity() &&
        ai::JumpAllowedOnMap(bot->GetMap() && bot->GetMap()->IsDungeon(), false))
    {
        return true;
    }

    // The core follow never re-checks its route: while the hold-before-pack walk
    // applies, keep the action live so it can take over from a running core follow.
    if (UseHoldBeforePacks(followTarget))
        return true;

    if (followTarget && bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE &&
        sServerFacade.GetChaseTarget(bot) &&
        sServerFacade.GetChaseTarget(bot)->getObjectGuid() == followTarget->getObjectGuid())
    {
        // A running core follow is trusted, except where it can stand still
        // for good: inside a dungeon (partial path, DungeonFollowStallPolicy.h)
        // or in the open world on a navmesh gap (OpenFollowStepPolicy.h).
        if (ObserveDungeonFollowStall(followTarget))
            return true;
        return ObserveOpenFollowStall(followTarget);
    }

    return true;
}

// Feeds the dungeon stall watch; true while the rescue is on. Cheap: member
// reads and one distance, and only inside a dungeon out of combat.
bool FollowAction::ObserveDungeonFollowStall(Unit* followTarget)
{
    Map* map = bot->GetMap();
    bool const inCombat = bot->IsInCombat() || ai->IsStateActive(BotState::BOT_STATE_COMBAT);
    if (!followTarget || followTarget == bot || !map || !followTarget->IsPlayer() ||
        !UseDungeonFollowWatch(map->IsDungeon(), inCombat) ||
        !bot->IsAlive() || !followTarget->IsAlive() || !ai->IsSafe(followTarget) ||
        bot->GetTransport() || followTarget->GetTransport() || bot->IsTaxiFlying())
    {
        followStallWatch.Reset();
        return false;
    }

    bool const held = bot->IsRooted() || bot->HasUnitState(UNIT_STAT_NO_FREE_MOVE) ||
        bot->GetStandState() != UNIT_STAND_STATE_STAND || bot->IsNonMeleeSpellCasted(true, false, true);

    return followStallWatch.Observe(WorldTimer::getMSTime(), bot->GetPositionX(), bot->GetPositionY(),
        bot->GetDistance(followTarget), ai->GetRange("follow"), held);
}

// Inside a dungeon the core follow can stand still for good: with a partial
// path (a navmesh island, a raised step the mesh does not link to the floor)
// it stops while the leader is in sight and otherwise walks to the end of the
// partial path, which is where the bot already stands. Uldaman 2026-10-12:
// four bots stood 50 s at the edge of such an island, "moving" with no motion,
// 34-36 yd from the leader. Once the watch has seen that (DungeonFollowStallPolicy.h),
// the rescue tries a short step up when the leader is close and in sight on a
// nearby floor, then the ledge step-down, else the navmesh walk below.
bool FollowAction::RescueStalledDungeonFollow(Unit* followTarget, bool& moved)
{
    if (!ObserveDungeonFollowStall(followTarget))
        return false;

    if (StepUpToLeaderInDungeon(followTarget, moved))
        return true;

    // Own clock and throttle (LedgeStepDownPolicy.h): only fires on an island edge.
    if (StepDownOffLedge(followTarget))
    {
        moved = true;
        return true;
    }

    // Between walks the normal follow runs (it does not re-issue a core
    // follow that is already running).
    uint32 const nowMs = WorldTimer::getMSTime();
    if (!followStallWatch.WalkDue(nowMs))
        return false;

    // MoveTo2 walks the navmesh route, complete or partial, and clears the
    // stalled core follow when it launches. The destination is the leader's
    // follow spot, not the leader's feet: MoveTo2 reuses the cached route
    // unless the destination moved 10% of the way, so feet would be re-walked
    // every 2 s for a moving leader; the spot only moves with it.
    WorldPosition destination(followTarget);
    if (Formation* formation = AI_VALUE(Formation*, "formation"))
    {
        WorldLocation loc = formation->GetLocation();
        if (!Formation::IsNullLocation(loc) && loc.mapId != -1)
            destination = WorldPosition(loc);
    }
    moved = MoveTo2(destination);
    followStallWatch.Walked(nowMs, moved);
    if (moved)
        ai->TellDebug(GetMaster(), "Core follow stood still in a dungeon - walking the path to you", "debug move");
    return true;
}

// Same shape as the open-world step, with dungeon caps
// (OpenFollowStepPolicy.h): toward a close sighted leader whose height
// differs by a walkable stair, and only when floor runs along the whole
// segment near the walked line. A launched step counts as a walk of the
// rescue watch, so its pacing and give-up bound the episode. A refused step
// is only re-checked every kDungeonFollowStepRecheckMs and leaves the watch
// alone: the ledge step-down and the navmesh walk below still run. True
// only when a step launched; `moved` mirrors it.
bool FollowAction::StepUpToLeaderInDungeon(Unit* followTarget, bool& moved)
{
    moved = false;
    Map* map = bot->GetMap();
    if (!followTarget || followTarget == bot || !map || !map->IsDungeon() ||
        !followTarget->IsPlayer() || !followTarget->IsInWorld() ||
        !bot->IsAlive() || !followTarget->IsAlive() || !ai->IsSafe(followTarget) ||
        bot->GetTransport() || followTarget->GetTransport() || bot->IsTaxiFlying())
        return false;

    uint32 const nowMs = WorldTimer::getMSTime();
    if (!followStallWatch.WalkDue(nowMs) || nowMs - dungeonStepCheckedMs < ai::kDungeonFollowStepRecheckMs)
        return false;
    dungeonStepCheckedMs = nowMs;

    float const dx = followTarget->GetPositionX() - bot->GetPositionX();
    float const dy = followTarget->GetPositionY() - bot->GetPositionY();
    float const dz = followTarget->GetPositionZ() - bot->GetPositionZ();
    float const horizontal = std::sqrt(dx * dx + dy * dy);
    if (!ai::DungeonFollowStepLeaderNear(horizontal, dz))
        return false;

    ai::OpenFollowStep step = ai::DungeonFollowStepToward(bot->GetPositionX(), bot->GetPositionY(),
        followTarget->GetPositionX(), followTarget->GetPositionY());
    float const h = 2.0f;
    bool const seesLeader = map->isInLineOfSight(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ() + h,
        followTarget->GetPositionX(), followTarget->GetPositionY(), followTarget->GetPositionZ() + h);
    if (!ai::DungeonFollowStepAllowed(horizontal, dz, seesLeader))
        return false;

    float groundZ = map->GetHeight(step.x, step.y, followTarget->GetPositionZ() + 0.5f);
    if (!MaNGOS::IsValidMapCoord(step.x, step.y, groundZ) ||
        !ai::DungeonFollowStepGroundNearLeader(groundZ, followTarget->GetPositionZ()))
        return false;

    // Floor must run along the whole straight segment: sample ground every
    // ~1.5 yd and require each sample near the line from the bot's feet to
    // the step target. A stair hugs the line; a pit or the void between
    // balconies drops off it, and the step is refused.
    float const botZ = bot->GetPositionZ();
    float const sdx = step.x - bot->GetPositionX();
    float const sdy = step.y - bot->GetPositionY();
    int const samples = (int)std::ceil(std::sqrt(sdx * sdx + sdy * sdy) / ai::kDungeonFollowStepSampleYd);
    for (int i = 1; i < samples; ++i)
    {
        float const t = (float)i / (float)samples;
        float const sx = bot->GetPositionX() + sdx * t;
        float const sy = bot->GetPositionY() + sdy * t;
        float const sampleZ = map->GetHeight(sx, sy, ai::DungeonFollowStepLineZ(botZ, groundZ, t) + 0.5f);
        if (!MaNGOS::IsValidMapCoord(sx, sy, sampleZ) ||
            !ai::DungeonFollowStepSampleOk(true, sampleZ, ai::DungeonFollowStepLineZ(botZ, groundZ, t)))
            return false;
    }

    float const startDist = bot->GetDistance(followTarget);
    bot->GetMotionMaster()->MovePoint(bot->GetMapId(), step.x, step.y, groundZ, MOVE_RUN_MODE);
    WaitForReach(bot->GetDistance(step.x, step.y, groundZ));
    moved = true;
    followStallWatch.Walked(nowMs, true);
    if (WorldPosition(bot->GetMapId(), step.x, step.y, groundZ).distance(WorldPosition(followTarget)) >
        startDist - ai::kDungeonFollowStepHeadwayYd)
        followStallWatch.Reset();
    return true;
}

// Feeds the open-world stall watch; true while the rescue is on. Cheap:
// member reads and one distance, and only outside instances out of combat.
// Unlike the dungeon watch the leader may be a bot (a hire following its
// lead bot), and a mounted leader outruns steps: only an unmounted pair.
bool FollowAction::ObserveOpenFollowStall(Unit* followTarget)
{
    Map* map = bot->GetMap();
    bool const inCombat = bot->IsInCombat() || ai->IsStateActive(BotState::BOT_STATE_COMBAT);
    if (!followTarget || followTarget == bot || !map || map->IsDungeon() || inCombat ||
        bot->InBattleGround() || !followTarget->IsInWorld() ||
        !bot->IsAlive() || !followTarget->IsAlive() || !ai->IsSafe(followTarget) ||
        bot->GetTransport() || followTarget->GetTransport() || bot->IsTaxiFlying() ||
        bot->IsMounted() || followTarget->IsMounted())
    {
        openFollowStallWatch.Reset();
        openFollowStepHeadway.Reset();
        return false;
    }

    bool const held = bot->IsRooted() || bot->HasUnitState(UNIT_STAT_NO_FREE_MOVE) ||
        bot->GetStandState() != UNIT_STAND_STATE_STAND || bot->IsNonMeleeSpellCasted(true, false, true);

    return openFollowStallWatch.Observe(WorldTimer::getMSTime(), bot->GetPositionX(), bot->GetPositionY(),
        bot->GetDistance(followTarget), ai->GetRange("follow"), held);
}

// In the open world the core follow can stand still for good on a navmesh gap
// (Windhorn Canyon entrance 2026-10: a missing vmap tile leaves NOPATH while
// the leader is in sight, so no spline launches). Once the watch has seen 2 s
// of that, walk short straight steps toward the leader the way `.bot cmd move
// ... direct` does: a MovePoint without pathfinding, at most 25 yd per step
// while the leader is seen and on the same floor. A navmesh walk would fail
// the same way the core follow did, so none is planned. Each step must close
// the gap by 5 yd or the rescue stops after three flat steps; the watch's
// give-up still bounds the whole episode. Returns false when the normal
// follow should run.
bool FollowAction::RescueStalledOpenFollow(Unit* followTarget, bool& moved)
{
    if (!ObserveOpenFollowStall(followTarget))
        return false;

    // Between steps the normal follow runs (it does not re-issue a core
    // follow that is already running).
    uint32 const nowMs = WorldTimer::getMSTime();
    if (!openFollowStallWatch.WalkDue(nowMs))
        return false;

    float const dx = followTarget->GetPositionX() - bot->GetPositionX();
    float const dy = followTarget->GetPositionY() - bot->GetPositionY();
    float const dz = followTarget->GetPositionZ() - bot->GetPositionZ();
    float const horizontal = std::sqrt(dx * dx + dy * dy);
    if (!ai::OpenFollowStepLeaderNear(horizontal, dz))
    {
        openFollowStallWatch.Reset();
        openFollowStepHeadway.Reset();
        return false;
    }

    Map* map = bot->GetMap();
    float const h = 2.0f; // both ends raised, as in `.bot cmd move ... direct`
    bool const seesLeader = map && map->isInLineOfSight(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ() + h,
        followTarget->GetPositionX(), followTarget->GetPositionY(), followTarget->GetPositionZ() + h);
    if (!ai::OpenFollowStepAllowed(horizontal, dz, seesLeader))
        return true; // stalled but no step fits: hold the rescue, keep watching

    ai::OpenFollowStep step = ai::OpenFollowStepToward(bot->GetPositionX(), bot->GetPositionY(),
        followTarget->GetPositionX(), followTarget->GetPositionY());
    float const startDist = bot->GetDistance(followTarget);
    bot->GetMotionMaster()->MovePoint(bot->GetMapId(), step.x, step.y, bot->GetPositionZ(), MOVE_RUN_MODE);
    WaitForReach(bot->GetDistance(step.x, step.y, bot->GetPositionZ()));
    moved = true;
    openFollowStallWatch.Walked(nowMs, true);
    if (!openFollowStepHeadway.Stepped(startDist, WorldPosition(bot->GetMapId(), step.x, step.y, bot->GetPositionZ()).distance(WorldPosition(followTarget))))
    {
        openFollowStallWatch.Reset();
        openFollowStepHeadway.Reset();
    }
    return true;
}

bool FollowAction::UseHoldBeforePacks(Unit* followTarget)
{
    if (!sPlayerbotAIConfig.dungeonFollowHoldBeforePacks)
    {
        holdGateOpen = false;
        holdStanding = false;
        return false;
    }

    bool open = followTarget && followTarget != bot && followTarget->IsPlayer() &&
        bot->IsAlive() && followTarget->IsAlive() && followTarget->GetMapId() == bot->GetMapId() &&
        bot->IsInWorld() && !bot->IsBeingTeleported() && !bot->IsTaxiFlying() && !bot->GetTransport() &&
        !followTarget->GetTransport();

    // On beyond 30 yd, kept on until the leader is within 25 yd.
    open = open && UseDungeonFollowHold(true, bot->GetMap()->IsDungeon(), bot->IsInCombat(),
        followTarget->IsInCombat(), bot->GetDistance(followTarget), holdGateOpen);

    holdGateOpen = open;
    if (!open)
        holdStanding = false;
    return open;
}

// Inside a dungeon, out of combat, with the leader far ahead: walk the route to the
// leader with the module's MoveTo (its route clip sees packs) instead of the core
// follow (which does not), and hold just outside the aggro range of the first
// unpulled pack on that route. Returns false when the normal follow should run.
bool FollowAction::FollowHoldingBeforePacks(Unit* followTarget, bool& moved)
{
    if (!UseHoldBeforePacks(followTarget))
    {
        if (holdFollowActive)
        {
            // Back to the core follow: drop the short re-check wait set below.
            holdFollowActive = false;
            holdToldPack = ObjectGuid();
            SetDuration(sPlayerbotAIConfig.reactDelay);
        }
        return false;
    }

    holdFollowActive = true;

    PackHoldResult packHold;
    packHold.reuseTargets = holdStanding;
    // The destination is the leader's follow spot, not the leader's feet:
    // MoveTo2 reuses the cached route unless the destination moved 10% of
    // the way, so feet would re-plan every second for a moving leader.
    WorldPosition destination(followTarget);
    if (Formation* formation = AI_VALUE(Formation*, "formation"))
    {
        WorldLocation loc = formation->GetLocation();
        if (!Formation::IsNullLocation(loc) && loc.mapId != -1)
            destination = WorldPosition(loc);
    }
    moved = MoveTo2(destination, false, false, false, false, &packHold);

    // MoveTo2 can return without launching a move (path collapsed, no route, a
    // clip that left nothing). A core follow started before the gate opened would
    // then keep walking past the margin: stop it, the module walk owns the bot now.
    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
        ai->StopMoving();

    if (!packHold.hold)
    {
        holdStanding = false;
        holdToldPack = ObjectGuid();
        // Re-check the route at least once a second: a pack that only comes into
        // sight mid-walk must still stop the bot before it is in aggro range.
        if (!moved || GetDuration() > kDungeonFollowHoldRecheckMs)
            SetDuration(kDungeonFollowHoldRecheckMs);
        return true;
    }

    if (moved)
    {
        // Walking to the hold point.
        holdStanding = false;
        if (GetDuration() > kDungeonFollowHoldRecheckMs)
            SetDuration(kDungeonFollowHoldRecheckMs);
    }
    else
    {
        // At the hold point (nothing left to walk): stand still and re-check the
        // route once a second. The follow action keeps the bot while it holds;
        // it rests through the normal out-of-combat flow once the hold ends.
        if (sServerFacade.isMoving(bot))
            ai->StopMoving();
        holdStanding = true;
        SetDuration(kDungeonFollowHoldRecheckMs);
        moved = true;
    }

    Unit* pack = ai->GetUnit(packHold.pack);
    time_t const now = time(nullptr);
    if (pack && packHold.pack != holdToldPack &&
        (!holdToldAt || now - holdToldAt >= (time_t)kDungeonFollowHoldTellCooldownSec))
    {
        // Whisper the master, not party chat: only they can act on it (pull the
        // pack or come back), and four bots saying it in party would be noise.
        if (Player* master = GetMaster())
        {
            std::ostringstream out;
            out << "Pack in the way: " << pack->GetName() << ". Waiting here - pull it or come back for me.";
            ai->TellPlayerNoFacing(master, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false);
        }
        holdToldPack = packHold.pack;
        holdToldAt = now;
    }

    return true;
}

bool FollowAction::CanDeadFollow(Unit* target)
{
    //Move to corpse when dead and player is alive or not a ghost.
    if (!sServerFacade.IsAlive(bot) && (sServerFacade.IsAlive(target) || !target->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST)))
    {
        return false;
    }

    return true;
}

bool StopFollowAction::isUseful()
{
    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
        return false;

    if (sServerFacade.GetChaseTarget(bot) && !sServerFacade.GetChaseTarget(bot)->IsPlayer() && sServerFacade.GetChaseTarget(bot)->IsMoving())
        return false;

    return true;
}

bool FleeToMasterAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    Unit* fTarget = AI_VALUE(Unit*, "master target");
    bool canFollow = Follow(fTarget);
    if (!canFollow)
    {
        //SetDuration(5000);
        return false;
    }

    WorldPosition targetPos(fTarget);
    WorldPosition bosPos(bot);
    float distance = bosPos.fDist(targetPos);


    uint32 scale = 5;
    if (bot->GetGroup() && bot->GetGroup()->GetMembersCount() > 5)
        scale = bot->GetGroup()->GetMembersCount();


    if (distance > sPlayerbotAIConfig.reactDistance && bot->IsInCombat())
    {
        if (!urand(0, scale))
            ai->TellPlayerNoFacing(requester, "I'm heading to your location but I'm in combat", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            //ai->TellPlayer(BOT_TEXT("wait_travel_combat"), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    }
    else if (distance < sPlayerbotAIConfig.reactDistance * 3)
    {
        if (!urand(0, scale))
            ai->TellPlayerNoFacing(requester, BOT_TEXT("wait_travel_close"), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    }
    else if (distance < 1000)
    {
        if (!urand(0, scale*4))
            ai->TellPlayerNoFacing(requester, BOT_TEXT("wait_travel_medium"), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    }
    else
        if (!urand(0, scale*6))
            ai->TellPlayerNoFacing(requester, BOT_TEXT("wait_travel_medium"), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

    SetDuration(3000U);
    return true;
}

bool FormationStationAction::Execute(Event& event)
{
    Formation* formation = AI_VALUE(Formation*, "formation");
    if (!formation)
        return false;
    WorldLocation loc = formation->GetLocation();
    if (Formation::IsNullLocation(loc) || bot->GetMapId() != loc.mapId)
        return false;
    return MoveTo(loc.mapId, loc.x, loc.y, loc.z, true);
}

bool FormationStationAction::isUseful()
{
    if (ai::FollowBlockedByCast(bot->IsNonMeleeSpellCasted(false, true, true)))
        return false;
    return ai->CanMove();
}

bool FleeToMasterAction::isUseful()
{
    if (!ai->CanMove())
        return false;

    if (!ai->GetGroupMaster())
        return false;

    if (ai->GetGroupMaster() == bot)
        return false;

    Unit* target = AI_VALUE(Unit*, "current target");

    if (target && ai->GetGroupMaster()->GetTargetGuid() == target->getObjectGuid())
        return false;

    if (!(ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
      ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)))
        return false;

    Unit* fTarget = AI_VALUE(Unit*, "master target");

    if (!CanDeadFollow(fTarget))
        return false;

    if (fTarget && fTarget->IsPlayer())
    {
        if (AI_VALUE(GuidPosition, "rpg target") && CanFreeMoveValue::CanFreeMoveTo(ai, AI_VALUE(GuidPosition, "rpg target")))
            return false;
    }

    LootObject loot = AI_VALUE(LootObject, "loot target");
    if (loot.IsLootPossible(bot))
        return false;

    return true;
}
