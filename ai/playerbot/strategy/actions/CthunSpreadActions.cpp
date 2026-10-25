#include "playerbot/playerbot.h"
#include "CthunSpreadActions.h"
#include "playerbot/GroupMembers.h"
#include "../../../../runtime/PlayerbotAIStorage.h"
#include <map>
#include "AI/ScriptedInstance.h"
#include "Movement/spline/MoveSplineInit.h"
#include "playerbot/strategy/values/LastMovementValue.h"

using namespace ai;
namespace
{
bool Surface(Player* p)
{
    return p && p->IsInWorld() && p->IsAlive() && !p->IsBeingTeleported() &&
        CthunSurface(p->GetMapId(),p->GetPositionX(),p->GetPositionY(),p->GetPositionZ());
}
bool Enabled(PlayerbotAI* ai)
{
    return ai && (ai->HasStrategy("cthun spread",BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("cthun spread",BotState::BOT_STATE_NON_COMBAT));
}
Unit* Boss(PlayerbotAI* ai)
{
    // Core UnitShouldPull starts the fight at 95 yd/LOS. The ordinary
    // possible-target cache is only 60 yd and cannot gate entry tactics.
    auto* instance=dynamic_cast<ScriptedInstance*>(ai->GetBot()->GetMap()->GetInstanceData());
    if(!instance) return nullptr;
    for(uint32 entry:{15589u,15727u})
    {
        Creature* boss=instance->GetSingleCreatureFromStorage(entry,true);
        if(boss && boss->IsAlive() && boss->IsInMap(ai->GetBot()) &&
            CthunSurface(boss->GetMapId(),boss->GetPositionX(),boss->GetPositionY(),boss->GetPositionZ())) return boss;
    }
    return nullptr;
}
CthunPoint Point(WorldObject* p) {return {p->GetPositionX(),p->GetPositionY()};}
bool Los(Player* player, CthunPoint p,float z,Unit* target)
{
    return player->GetMap()->isInLineOfSight(p.x,p.y,z+player->GetCollisionHeight(),
        target->GetPositionX(),target->GetPositionY(),target->GetPositionZ()+target->GetCollisionHeight(),true);
}
}
bool ai::CthunSpreadEnabled(PlayerbotAI* ai)
{
    if(!Enabled(ai) || !Surface(ai->GetBot()) || !ai->GetBot()->GetGroup()) return false;
    // The room boundary, not bot combat state, activates entry spreading.
    // No attack is issued here; an idle Eye is only an encounter identity.
    Unit* boss=Boss(ai);
    // Before engagement, use the same LOS condition as the core pull check.
    // Do not walk a waiting group through a wall/door to start a fresh pull.
    return boss && (boss->IsInCombat() || ai->GetBot()->IsWithinLOSInMap(boss));
}

std::vector<CthunPlacement> const& CthunSpreadAction::GroupPlan()
{
    uint32 now=WorldTimer::getMSTime();
    if(lastPlan && now-lastPlan<1000) return plan;
    lastPlan=now;
    auto previous=plan;
    plan.clear();
    Group* group=bot->GetGroup();
    if(!group) return plan;
    std::vector<CthunSlot> slots;
    std::map<uint32,Player*> players;
    std::map<uint32,Unit*> duties;
    std::map<uint32,float> ranges;
    for(auto const& member:group->GetMemberSlots())
    {
        Player* player=ObjectAccessor::FindPlayer(member.guid);
        if(!player || !player->IsInMap(bot)) continue;
        PlayerbotAI* peer=PlayerbotAIStorage::Instance().GetAI(player);
        bool surface=Surface(player);
        unsigned role=ai->IsHeal(player) ? 2 : ai->IsRanged(player) && !ai->IsTank(player) ? 1 : 0;
        // Keep unavailable roster slots, but do not treat downstairs/dead
        // players as surface obstacles or attempt to move them upstairs.
        CthunPoint here=surface ? Point(player) : CthunPoint{100000.0f+player->GetGUIDLow(),100000};
        slots.push_back({player->GetGUIDLow(),member.group,role,here,surface && Enabled(peer),player->GetObjectBoundingRadius()});
        players[player->GetGUIDLow()]=player;
        if(peer && surface)
        {
            float range=peer->GetRange(role==2 ? "heal":"spell");
            // Use known primary spells, not an arbitrary universal caster
            // radius. Core spell range is measured after combat reaches.
            std::vector<std::string> spells;
            if(role==2) spells={"flash heal","healing wave","holy light","healing touch"};
            else spells={"frostbolt","fireball","shadow bolt","smite","lightning bolt","starfire","wrath","shoot"};
            for(auto const& spell:spells)
            {
                float knownRange=0;
                if(peer->GetSpellRange(spell,&knownRange) && knownRange>range && knownRange<=45) range=knownRange;
            }
            ranges[player->GetGUIDLow()]=range;
            AiObjectContext* context=peer->GetAiObjectContext();
            Unit* target=role==2 ? AI_VALUE(Unit*,"party member to heal") : AI_VALUE(Unit*,"current target");
            if(target && target->IsAlive() && target->IsInMap(player) &&
                CthunSurface(target->GetMapId(),target->GetPositionX(),target->GetPositionY(),target->GetPositionZ()))
                duties[player->GetGUIDLow()]=target;
        }
    }
    plan=CthunPlan(slots,[&](CthunSlot const& slot,CthunPoint p) {
        Player* player=players[slot.guid];
        float z=player->GetMap()->GetHeight(p.x,p.y,player->GetPositionZ(),true);
        if(!CthunSurface(531,p.x,p.y,z)) return false;
        Unit* target=duties[slot.guid];
        if(!target || target==player) return true;
        CthunDuty duty;
        duty.healer=slot.role==2;
        duty.clawTank=ai->IsTank(player) && target->GetEntry()==15728;
        duty.hasOtherTarget=true;
        duty.lineOfSight=Los(player,p,z,target);
        duty.meleeContact=player->CanReachWithMeleeAutoAttackAtPosition(target,p.x,p.y,z);
        float reach=player->GetCombatReach(false)+target->GetCombatReach(false);
        duty.distance=std::hypot(CthunDistance(p,Point(target)),z-target->GetPositionZ());
        duty.range=ranges[slot.guid]+reach;
        return CthunDutyCompatible(duty);
    },previous);
    return plan;
}

bool CthunSpreadAction::Assigned(CthunPoint& point)
{
    if(!CthunSpreadEnabled(ai)) return false;
    PlayerbotAI* leader=ai;
    for(Player* member:LiveGroupMembers(bot->GetGroup()))
    {
        if(!Surface(member) || !member->IsInMap(bot)) continue;
        PlayerbotAI* peer=PlayerbotAIStorage::Instance().GetAI(member);
        if(Enabled(peer) && member->GetGUIDLow()<leader->GetBot()->GetGUIDLow()) leader=peer;
    }
    auto* coordinator=static_cast<CthunSpreadAction*>(leader->GetAiObjectContext()->GetAction("cthun spread move"));
    for(auto const& assignment:coordinator->GroupPlan())
        if(assignment.guid==bot->GetGUIDLow() && assignment.assigned) {point=assignment.point;return true;}
    return false;
}

bool CthunSpreadAction::isUseful()
{
    if(!CthunSpreadEnabled(ai)) return false;
    if(lastMove && WorldTimer::getMSTime()-lastMove<500) return false;
    // Validate before the reaction engine interrupts a heal/cast. A failed
    // planner is not a successful emergency and must not cancel heals.
    uint32 now=WorldTimer::getMSTime();
    // Trigger and engine both ask isUseful before Execute. Cache the answer;
    // only actual dispatch may consume the movement cooldown.
    if(lastCheck && now-lastCheck<500) return preparedUseful;
    lastCheck=now;
    preparedUseful=Prepare();
    return preparedUseful;
}

bool CthunSpreadAction::Prepare()
{
    preparedPath.clear();
    if(!CthunSpreadEnabled(ai) || !ai->CanMove() || bot->GetTransport() || bot->IsTaxiFlying()) return false;
    uint32 now=WorldTimer::getMSTime();
    auto blocked=[&]() {
        routeBlocked=true;
        if(!lastReport || now-lastReport>30000)
        {
            lastReport=now;
            sPlayerbotAIConfig.logEvent(ai,"CthunSpreadBlocked","no safe sector route or duty-compatible slot","check roster and manual positions");
        }
        return false;
    };
    CthunPoint destination;
    if(!Assigned(destination)) return blocked();
    CthunPoint here=Point(bot);
    if(CthunDistance(here,destination)<=.75f)
    {
        routeBlocked=false;
        return bot->GetMotionMaster()->GetCurrentMovementGeneratorType()==CHASE_MOTION_TYPE;
    }
    // Do not step into an unassigned human/bot, or through another settled
    // member. Existing doorway overlaps must be allowed to fan out.
    std::vector<CthunObstacle> obstacles;
    for(Player* member:LiveGroupMembers(bot->GetGroup()))
        if(member!=bot && Surface(member) && member->IsInMap(bot))
            obstacles.push_back({Point(member),10.5f+bot->GetObjectBoundingRadius()+member->GetObjectBoundingRadius()});
    auto route=CthunRoute(here,destination,obstacles);
    if(route.size()<2) return blocked();
    CthunPoint next=route[1];
    float distance=CthunDistance(here,next);
    if(distance>4) {next.x=here.x+(next.x-here.x)*4/distance;next.y=here.y+(next.y-here.y)*4/distance;}
    WorldPosition start(bot),end(531,next.x,next.y,bot->GetPositionZ());
    end.setZ(bot->GetMap()->GetHeight(next.x,next.y,bot->GetPositionZ(),true));
    if(!CthunSurface(531,end.getX(),end.getY(),end.getZ()) || IsHazardNearPosition(end)) return blocked();
    auto path=start.GetPathTo(end,bot);
    if(!end.isPathTo(path)) return blocked();
    CthunPoint previous=here;
    for(auto const& waypoint:path)
    {
        CthunPoint p{waypoint.getX(),waypoint.getY()};
        if(!CthunSurface(531,p.x,p.y,waypoint.getZ()) || IsHazardNearPosition(waypoint)) return blocked();
        if(CthunDistance(previous,p)<.01f) continue;
        for(auto eye:kCthunEyes)
            if(!CthunSegmentClear(previous,p,eye,10.75f)) return blocked();
        for(Player* member:LiveGroupMembers(bot->GetGroup()))
        {
            if(member==bot || !Surface(member) || !member->IsInMap(bot)) continue;
            CthunPoint occupied=Point(member);
            float clearance=10.5f+bot->GetObjectBoundingRadius()+member->GetObjectBoundingRadius();
            if(CthunDistance(here,occupied)>=clearance && CthunSegmentDistance(previous,p,occupied)<clearance) return blocked();
        }
        previous=p;
    }
    preparedPath=path;
    routeBlocked=false;
    return true;
}

bool CthunSpreadAction::Execute(Event& /*event*/)
{
    // Recheck after selection: a member, target or order may have changed.
    lastCheck=0;
    preparedUseful=false;
    lastMove=WorldTimer::getMSTime();
    if(!Prepare()) return false;
    ai->StopMoving();
    if(preparedPath.empty()) return true;
    // Launch exactly the checked navmesh route. A second MoveTo planner can
    // choose a different path or hazard detour after validation.
    bot->InterruptNonMeleeSpells(false);
    Movement::MoveSplineInit init(*bot,"CthunSpread");
    init.MovebyPath(WorldPosition().toPointsArray(preparedPath));
    init.SetWalk(false);
    if(!init.Launch()) {routeBlocked=true;return false;}
    AI_VALUE(LastMovement&,"last movement").lastPath.clear();
    ClearIdleState();
    SetDuration(500);
    return true;
}
