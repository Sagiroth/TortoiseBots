#include "playerbot/playerbot.h"
#include "CthunSpreadActions.h"
#include "playerbot/GroupMembers.h"
#include "../../../../runtime/PlayerbotAIStorage.h"
#include <map>
#include "AI/ScriptedInstance.h"
#include "Movement/spline/MoveSplineInit.h"
#include "Movement/spline/MoveSpline.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
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
    plan.clear();
    Group* group=bot->GetGroup();
    if(!group) return plan;
    std::vector<CthunSlot> slots;
    for(auto const& member:group->GetMemberSlots())
    {
        Player* player=ObjectAccessor::FindPlayer(member.guid);
        PlayerbotAI* peer=player ? PlayerbotAIStorage::Instance().GetAI(player) : nullptr;
        unsigned role=player && ai->IsHeal(player) ? 2 : player && ai->IsRanged(player) && !ai->IsTank(player) ? 1 : 0;
        // Offline members retain their previous role reservation until they
        // return; deaths and swallowing do not alter subgroup order.
        auto old=roles.find(member.guid.GetCounter());
        if(!player && old!=roles.end()) role=old->second;
        roles[member.guid.GetCounter()]=role;
        slots.push_back({member.guid.GetCounter(),member.group,role,{},
            player && player->IsInMap(bot) && Surface(player) && Enabled(peer)});
    }
    plan=CthunPlan(slots,[](CthunSlot const&,CthunPoint){return true;});
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
    Unit* current=AI_VALUE(Unit*,"current target");
    if(ai->IsTank(bot) && current && current->GetEntry()==15728 && current->IsAlive() && current->IsInCombat())
    {routeBlocked=true;return false;} // keep the existing tank pickup/contact job
    uint32 now=WorldTimer::getMSTime();
    auto blocked=[&]() {
        routeBlocked=true;
        if(!lastReport || now-lastReport>30000)
        {
            lastReport=now;
            sPlayerbotAIConfig.logEvent(ai,"CthunSpreadBlocked","fixed chart route incomplete or hazardous","check roster and manual positions");
        }
        return false;
    };
    CthunPoint destination;
    if(!Assigned(destination)) return blocked();
    CthunPoint here=Point(bot);
    if(!routeBlocked && lastSpline && bot->movespline->GetId()==lastSpline && !bot->movespline->Finalized() &&
        bot->GetMotionMaster()->GetCurrentMovementGeneratorType()!=CHASE_MOTION_TYPE &&
        CthunDistance(destination,lastDestination)<.1f) return false;
    if(CthunDistance(here,destination)<=kCthunArrival)
    {
        routeBlocked=false;
        return bot->GetMotionMaster()->GetCurrentMovementGeneratorType()==CHASE_MOTION_TYPE;
    }
    // A fixed sector lane replaces the old per-bot visibility graph. Do
    // not make every moving player an impassable 10-yard wall: that closed
    // all entry lanes as the raid spread. Final slots provide the spacing.
    unsigned sector=bot->GetGroup()->GetMemberGroup(bot->getObjectGuid());
    CthunPoint next=CthunChartStep(here,destination,sector);
    WorldPosition start(bot),end(531,next.x,next.y,bot->GetPositionZ());
    end.setZ(bot->GetMap()->GetHeight(next.x,next.y,bot->GetPositionZ(),true));
    if(!CthunSurface(531,end.getX(),end.getY(),end.getZ()) || IsHazardNearPosition(end)) return blocked();
    auto path=start.GetPathTo(end,bot);
    if(!end.isPathTo(path)) return blocked();
    for(auto const& waypoint:path)
        if(!CthunSurface(531,waypoint.getX(),waypoint.getY(),waypoint.getZ()) || IsHazardNearPosition(waypoint)) return blocked();
    preparedDestination=destination;
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
    lastDestination=preparedDestination;
    lastSpline=bot->movespline->GetId();
    AI_VALUE(LastMovement&,"last movement").lastPath.clear();
    ClearIdleState();
    SetDuration(500);
    return true;
}

bool ai::SelectCthunSpreadTarget(PlayerbotAI* ai,Unit*& target)
{
    target=nullptr;
    if(!CthunSpreadEnabled(ai)) return false;
    Player* bot=ai->GetBot();
    if(ai->IsHeal(bot) || ai->IsTank(bot)) return false; // preserve healing and tank pickup
    Unit* boss=Boss(ai);
    if(!boss || !boss->IsInCombat()) return false; // position on entry, never order a fresh pull
    AiObjectContext* context=ai->GetAiObjectContext();
    Unit* current=AI_VALUE(Unit*,"current target");
    unsigned sector=bot->GetGroup()->GetMemberGroup(bot->getObjectGuid());
    bool ranged=ai->IsRanged(bot);
    std::vector<std::pair<float,float>> ranges;
    if(ranged)
    {
        // Use trained offensive spells and the actual hunter minimum range.
        // Do not make cooldown or current mana a target-switching signal.
        std::vector<std::string> spells;
        switch(bot->GetClass())
        {
            case CLASS_HUNTER: spells={"auto shot"};break;
            case CLASS_MAGE: spells={"frostbolt","fireball","scorch","shoot"};break;
            case CLASS_WARLOCK: spells={"shadow bolt","immolate","shoot"};break;
            case CLASS_PRIEST: spells={"mind flay","smite","shoot"};break;
            case CLASS_SHAMAN: spells={"lightning bolt"};break;
            case CLASS_DRUID: spells={"wrath","starfire"};break;
            default: break;
        }
        for(auto const& spell:spells)
        {
            float maximum=0,minimum=0;
            if(ai->GetSpellRange(spell,&maximum,&minimum) && maximum>0)
                ranges.push_back({minimum,maximum});
        }
    }
    bool shelled=boss->GetEntry()==15727 && boss->HasAura(26156);
    int best=100;unsigned bestSector=8;float bestDistance=0;
    auto candidates=AI_VALUE(std::list<ObjectGuid>,"possible attack targets");
    candidates.push_back(boss->getObjectGuid());
    for(auto guid:candidates)
    {
        Unit* unit=ai->GetUnit(guid);
        if(!unit || !unit->IsInMap(bot) || !unit->IsInCombat() ||
            !CthunSurface(unit->GetMapId(),unit->GetPositionX(),unit->GetPositionY(),unit->GetPositionZ()) ||
            !PossibleAttackTargetsValue::IsValid(unit,bot,95,false,false) ||
            PossibleAttackTargetsValue::IsImmuneToDamage(unit,bot) || !bot->IsWithinLOSInMap(unit)) continue;
        bool feasible=bot->CanReachWithMeleeAutoAttack(unit);
        if(ranged)
        {
            feasible=false;
            float distance=bot->GetCombatDistance(unit);
            for(auto range:ranges) if(distance>=range.first && distance<range.second-.5f) {feasible=true;break;}
        }
        unsigned own=CthunTargetSector(Point(unit));
        int rank=CthunDamageRank(unit->GetEntry(),shelled,feasible,CthunNearbySector(sector,own));
        if(rank<0) continue;
        unsigned sectorRank=own==sector ? 0 : 1;
        float distance=CthunDistance(Point(bot),Point(unit));
        // Keep an equal-ranked current target; otherwise deterministic near/GUID tie.
        bool replace=!target || rank<best || (rank==best && sectorRank<bestSector);
        if(target && rank==best && sectorRank==bestSector)
            replace=unit==current || (target!=current && (distance<bestDistance ||
                (distance==bestDistance && unit->GetGUIDLow()<target->GetGUIDLow())));
        if(replace) {target=unit;best=rank;bestSector=sectorRank;bestDistance=distance;}
    }
    // Handled even when no target fits: do not fall through to an unreachable
    // tank target. Extra melee wait at their chart slot, as requested.
    return true;
}
