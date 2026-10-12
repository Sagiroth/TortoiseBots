
#include "playerbot/playerbot.h"
#include "FollowActions.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/Formations.h"
#include "playerbot/strategy/values/FreeMoveValues.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/LootObjectStack.h"
#include "playerbot/DungeonFollowHoldPolicy.h"

using namespace ai;

bool FollowAction::Execute(Event& event)
{
    bool moved = false;
    Unit* followTarget = AI_VALUE(Unit*, "follow target");
    Formation* formation = AI_VALUE(Formation*, "formation");

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

    return moved;
}

bool FollowAction::isUseful()
{
    if (!ai->CanMove())
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
    if (ai->HasStrategy("follow jump", BotState::BOT_STATE_NON_COMBAT) && ai->AllowActivity())
    {
        return true;
    }

    // The core follow never re-checks its route: while the hold-before-pack walk
    // applies, keep the action live so it can take over from a running core follow.
    // Standing at a hold point, step aside between route re-checks so the normal
    // rest triggers (eat, drink) and loot can run while the bot waits.
    if (UseHoldBeforePacks(followTarget))
        return !holdStanding ||
            WorldTimer::getMSTimeDiff(holdStandingCheckMs, WorldTimer::getMSTime()) >= kDungeonFollowHoldRecheckMs;

    if (followTarget && bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE &&
        sServerFacade.GetChaseTarget(bot) &&
        sServerFacade.GetChaseTarget(bot)->getObjectGuid() == followTarget->getObjectGuid())
    {
        return false;
    }

    return true;
}

bool FollowAction::UseHoldBeforePacks(Unit* followTarget)
{
    if (!sPlayerbotAIConfig.dungeonFollowHoldBeforePacks)
        return false;

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
    moved = MoveTo2(WorldPosition(followTarget), false, false, false, false, &packHold);

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
        // At the hold point (nothing left to walk): stand still and let the
        // rest triggers have the bot until the next re-check (see isUseful).
        if (sServerFacade.isMoving(bot))
            ai->StopMoving();
        holdStanding = true;
        holdStandingCheckMs = WorldTimer::getMSTime();
        SetDuration(sPlayerbotAIConfig.reactDelay);
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
