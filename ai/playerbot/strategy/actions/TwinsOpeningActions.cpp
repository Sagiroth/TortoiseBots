#include "playerbot/playerbot.h"
#include "TwinsOpeningActions.h"
#include "GenericSpellActions.h"
#include "playerbot/strategy/values/RaidDpsTargetPolicy.h"
#include "ReachTargetActions.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/strategy/generic/PullStrategy.h"
#include "playerbot/strategy/values/LastMovementValue.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "../../../../runtime/PlayerbotAIStorage.h"
#include "AI/ScriptedInstance.h"
#include "Movement/spline/MoveSpline.h"
#include "Movement/spline/MoveSplineInit.h"

using namespace ai;
namespace
{
bool Has(PlayerbotAI* ai,char const* name) {return ai->HasStrategy(name,BotState::BOT_STATE_COMBAT);}
TwinsPoint Point(WorldObject* unit) {return {unit->GetPositionX(),unit->GetPositionY(),unit->GetPositionZ()};}
int Side(PlayerbotAI* ai) {return TwinsSide(Has(ai,"twins west"),Has(ai,"twins east"));}
unsigned TankEntry(PlayerbotAI* ai)
{
    bool caster=Has(ai,"twins veklor tank"),physical=Has(ai,"twins veknilash tank");
    if(caster==physical || Side(ai)<0) return 0;
    return caster && ai->GetBot()->GetClass()==CLASS_WARLOCK ? 15276 :
        physical && ai->GetBot()->GetClass()==CLASS_WARRIOR ? 15275 : 0;
}
Unit* Boss(PlayerbotAI* ai,unsigned entry)
{
    auto* instance=dynamic_cast<ScriptedInstance*>(ai->GetBot()->GetMap()->GetInstanceData());
    auto* boss=instance ? instance->GetSingleCreatureFromStorage(entry,true) : nullptr;
    return boss && boss->IsAlive() && boss->IsInWorld() && boss->IsInMap(ai->GetBot()) &&
        !boss->IsInEvadeMode() && TwinsRoom(531,Point(boss)) ? boss : nullptr;
}
bool GroupEngaged(PlayerbotAI* ai,Unit* boss)
{
    if(!boss || !boss->IsInCombat()) return false;
    Group* group=ai->GetBot()->GetGroup();
    Unit* victim=boss->GetVictim();
    if(victim && victim->IsPlayer() && static_cast<Player*>(victim)->GetGroup()==group) return true;
    for(Player* member:LiveGroupMembers(group))
        if(member->IsAlive() && member->IsInMap(ai->GetBot()) &&
            (member->GetVictim()==boss || member->GetAttackers().count(boss))) return true;
    return false;
}
bool PullOrdered(PlayerbotAI* ai,Unit* first,Unit* second)
{
    for(Player* member:LiveGroupMembers(ai->GetBot()->GetGroup()))
    {
        if(!member->IsAlive() || !member->IsInMap(ai->GetBot())) continue;
        auto* peer=PlayerbotAIStorage::Instance().GetAI(member);
        auto* pull=peer ? PullStrategy::Get(peer) : nullptr;
        if(pull && pull->IsCommandActive() && !pull->IsCommandPullback() &&
            ((first && pull->GetTarget()==first) || (second && pull->GetTarget()==second))) return true;
    }
    return false;
}
bool OtherOrder(PlayerbotAI* ai,Unit* target)
{
    AiObjectContext* context=ai->GetAiObjectContext();
    for(Unit* order:{ai->GetUnit(AI_VALUE(ObjectGuid,"explicit attack target")),AI_VALUE(Unit*,"rti target")})
        if(order && order!=target && PossibleAttackTargetsValue::IsValid(order,ai->GetBot(),
            sPlayerbotAIConfig.sightDistance,false,false)) return true;
    return false;
}
Unit* LocalTankBoss(PlayerbotAI* ai)
{
    unsigned entry=TankEntry(ai);
    Unit* boss=entry ? Boss(ai,entry) : nullptr;
    return boss && TwinsOnSide(Point(boss),Side(ai)) && !OtherOrder(ai,boss) ? boss : nullptr;
}
float BurstRadius(PlayerbotAI* ai,Unit* boss)
{ return 12.f+boss->GetObjectBoundingRadius()+ai->GetBot()->GetObjectBoundingRadius(); }
bool PetOnCaster(PlayerbotAI* ai)
{
    Pet* pet=ai->GetBot()->GetPet();
    return pet && pet->GetVictim() && pet->GetVictim()->GetEntry()==15276;
}
void RecallPet(PlayerbotAI* ai)
{
    Pet* pet=ai->GetBot()->GetPet();
    if(!pet) return;
    WorldPacket data(CMSG_PET_ACTION);
    data<<pet->GetObjectGuid()<<uint32((ACT_COMMAND<<24)|COMMAND_FOLLOW)<<ObjectGuid();
    ai->GetBot()->GetSession()->HandlePetAction(data);
}
bool DesiredTankPosition(PlayerbotAI* ai,TwinsPoint& destination)
{
    if(!TankEntry(ai)) return false;
    int side=Side(ai);
    Unit* local=LocalTankBoss(ai);
    if(local && OtherOrder(ai,local)) return false;
    Unit* caster=Boss(ai,15276);
    if(TankEntry(ai)==15275 && local)
    { destination=TwinsTankCamp(side);return true; }
    TwinsPoint home=side==0 ? kTwinsWest : kTwinsEast;
    if(caster && TwinsOnSide(Point(caster),side))
    {
        // The caster may stop at spell range. Keep 18 yards of edge-to-edge
        // clearance when separated, never follow its identity across the room.
        Unit* brother=Boss(ai,15275);
        bool separated=brother && TwinsSeparated(Point(caster),Point(brother),
            caster->GetObjectBoundingRadius()+brother->GetObjectBoundingRadius());
        float range=18.f+caster->GetObjectBoundingRadius()+ai->GetBot()->GetObjectBoundingRadius();
        destination=TwinsOutward(separated ? Point(caster) : home,side,range);
        return true;
    }
    destination=TwinsOutward(home,side,24.f); // spare team waits outside the caster landing zone
    return true;
}
void ClearWrongTarget(PlayerbotAI* ai,Unit* desired)
{
    Player* bot=ai->GetBot();AiObjectContext* context=ai->GetAiObjectContext();
    Unit* current=AI_VALUE(Unit*,"current target");
    if(current && current!=desired && (TankEntry(ai) || current->GetEntry()==15275 || current->GetEntry()==15276))
    {
        for(int type=CURRENT_MELEE_SPELL;type<CURRENT_MAX_SPELL;++type)
            if(Spell* spell=bot->GetCurrentSpell(CurrentSpellTypes(type)))
                if(spell->m_targets.getUnitTargetGuid()==current->GetObjectGuid()) bot->InterruptSpell(CurrentSpellTypes(type));
        bot->AttackStop();SET_AI_VALUE(Unit*,"current target",nullptr);
    }
}
}

bool ai::TwinsOpeningEnabled(PlayerbotAI* ai)
{
    Player* bot=ai ? ai->GetBot() : nullptr;
    if(!bot || !bot->IsAlive() || !bot->IsInWorld() || bot->IsBeingTeleported() || !bot->GetGroup() ||
        !TwinsRoom(bot->GetMapId(),Point(bot))) return false;
    if(!TankEntry(ai) && !Has(ai,"twins physical dps") && !Has(ai,"twins spell dps")) return false;
    Unit* physical=Boss(ai,15275),*caster=Boss(ai,15276);
    return GroupEngaged(ai,physical) || GroupEngaged(ai,caster) || PullOrdered(ai,physical,caster);
}
bool ai::TwinsOpeningTank(PlayerbotAI* ai)
{ return TwinsOpeningEnabled(ai) && TankEntry(ai); }
bool ai::TwinsOpeningRouteAllowed(PlayerbotAI* ai,std::vector<WorldPosition> const& path)
{
    if(!TwinsOpeningEnabled(ai)) return true;
    Unit* caster=Boss(ai,15276);
    if(!caster) return true;
    TwinsPoint previous=Point(ai->GetBot());
    float radius=BurstRadius(ai,caster);
    for(auto const& point:path)
    {
        TwinsPoint end{point.getX(),point.getY(),point.getZ()};
        unsigned count=std::max(1u,static_cast<unsigned>(std::ceil(TwinsDistance(previous,end))));
        TwinsPoint before=previous;
        for(unsigned i=1;i<=count;++i)
        {
            float f=float(i)/count;
            TwinsPoint sample{previous.x+f*(end.x-previous.x),previous.y+f*(end.y-previous.y),previous.z+f*(end.z-previous.z)};
            if(!TwinsSafeStep(before,sample,Point(caster),radius)) return false;
            before=sample;
        }
        previous=end;
    }
    return true;
}
bool ai::TwinsOpeningChaseAllowed(PlayerbotAI* ai,WorldObject* target,float range)
{
    if(!TwinsOpeningEnabled(ai) || !target) return true;
    TwinsPoint from=Point(ai->GetBot()),to=Point(target);
    float length=TwinsDistance(from,to);
    if(length<.1f) return false;
    float fraction=std::max(0.f,length-std::max(0.f,range))/length;
    std::vector<WorldPosition> route{WorldPosition(531,from.x+fraction*(to.x-from.x),from.y+fraction*(to.y-from.y),to.z)};
    return TwinsOpeningRouteAllowed(ai,route);
}
bool ai::SelectTwinsTankTarget(PlayerbotAI* ai,Unit*& target)
{
    target=nullptr;
    if(!TwinsOpeningEnabled(ai) || !TankEntry(ai)) return false;
    Unit* own=Boss(ai,TankEntry(ai));
    if(own && OtherOrder(ai,own)) return false;
    Unit* local=LocalTankBoss(ai);
    if(local && GroupEngaged(ai,local)) target=local;
    return true; // side is empty: do not fall through to the opposite twin/adds
}
bool ai::SelectTwinsDpsTarget(PlayerbotAI* ai,Unit*& target)
{
    target=nullptr;
    if(!TwinsOpeningEnabled(ai) || TankEntry(ai) || ai->IsHeal(ai->GetBot())) return false;
    unsigned entry=TwinsDamageEntry(Has(ai,"twins physical dps"),Has(ai,"twins spell dps"));
    if(!entry) return false;
    Unit* boss=Boss(ai,entry);
    if(!boss || !GroupEngaged(ai,boss) || OtherOrder(ai,boss)) return false;
    target=boss;return true;
}
bool ai::IsTwinsOpeningTargetExcluded(PlayerbotAI* ai,Unit* target)
{
    if(!target || (target->GetEntry()!=15275 && target->GetEntry()!=15276) || !TwinsOpeningEnabled(ai)) return false;
    Unit* assigned=nullptr;
    if(TankEntry(ai)) return SelectTwinsTankTarget(ai,assigned) && target!=assigned;
    return SelectTwinsDpsTarget(ai,assigned) && target!=assigned;
}
bool ai::TwinsPetAttackAllowed(PlayerbotAI* ai,Unit* target)
{ return !target || target->GetEntry()!=15276 || !TwinsOpeningEnabled(ai); }
std::list<HazardPosition> ai::GetTwinsOpeningHazards(PlayerbotAI* ai)
{
    std::list<HazardPosition> hazards;
    if(!TwinsOpeningEnabled(ai)) return hazards;
    if(Unit* caster=Boss(ai,15276)) hazards.emplace_back(WorldPosition(caster),BurstRadius(ai,caster));
    return hazards;
}
bool ai::TwinsOpeningMoveAllowed(PlayerbotAI* ai,float x,float y,float z)
{
    if(!TwinsOpeningEnabled(ai)) return true;
    TwinsPoint end{x,y,z};
    Unit* own=TankEntry(ai) ? Boss(ai,TankEntry(ai)) : nullptr;
    if(own && !OtherOrder(ai,own) && (!TwinsRoom(531,end) || !TwinsOnSide(end,Side(ai)))) return false;
    Unit* caster=Boss(ai,15276);
    return !caster || TwinsSafeStep(Point(ai->GetBot()),end,Point(caster),BurstRadius(ai,caster));
}

bool TwinsPositionAction::Prepare()
{
    path.clear();
    if(!TwinsOpeningEnabled(ai) || !ai->CanMove() || bot->GetTransport() || bot->IsTaxiFlying()) return false;
    Unit* caster=Boss(ai,15276);
    float radius=caster ? BurstRadius(ai,caster) : 0;
    bool danger=caster && TwinsDistance(Point(bot),Point(caster))<radius;
    bool tank=TankEntry(ai)!=0;
    Unit* dpsTarget=nullptr;
    bool approach=!tank && SelectTwinsDpsTarget(ai,dpsTarget) && dpsTarget &&
        bot->GetDistance(dpsTarget)>sPlayerbotAIConfig.sightDistance*.8f;
    if(!tank && !danger && !approach) return false;
    if(tank)
    {
        Unit* own=Boss(ai,TankEntry(ai));
        if(own && OtherOrder(ai,own)) return false;
        if(!DesiredTankPosition(ai,destination)) return false;
        if(TankEntry(ai)==15275 && own && GroupEngaged(ai,own) && bot->HasSpell(71) &&
            !ai->HasAura("defensive stance",bot)) return false;
        // Establish ranged threat before repositioning, if already safely in
        // Searing Pain range on this side. Do not cancel that opening cast.
        if(!danger && TankEntry(ai)==15276 && caster==LocalTankBoss(ai) && GroupEngaged(ai,caster) &&
            caster->GetVictim()!=bot && TwinsOnSide(Point(bot),Side(ai)) &&
            bot->GetDistance(caster)<=28.f && bot->GetDistance(caster)>=15.f && bot->IsWithinLOSInMap(caster,true))
            return false;
    }
    else if(!danger && approach)
    {
        // The opposite platform is outside the ordinary target scan. Walk into
        // that scan first; do not manufacture a permanent explicit attack order.
        TwinsPoint here=Point(bot),goal=Point(dpsTarget);
        float distance=TwinsDistance(here,goal);
        float standOff=std::max(20.f,sPlayerbotAIConfig.sightDistance*.6f);
        float fraction=std::max(0.f,distance-standOff)/distance;
        destination={here.x+fraction*(goal.x-here.x),here.y+fraction*(goal.y-here.y),goal.z};
    }
    else
    {
        float dx=bot->GetPositionX()-caster->GetPositionX(),dy=bot->GetPositionY()-caster->GetPositionY();
        float length=std::hypot(dx,dy);
        if(length<.1f){dx=1;dy=0;length=1;}
        destination={caster->GetPositionX()+dx/length*(radius+3),caster->GetPositionY()+dy/length*(radius+3),bot->GetPositionZ()};
    }
    if(!TwinsRoom(531,destination)) return false;
    if(lastSpline && bot->movespline->GetId()==lastSpline && !bot->movespline->Finalized() &&
        bot->GetMotionMaster()->GetCurrentMovementGeneratorType()!=CHASE_MOTION_TYPE &&
        TwinsDistance(destination,lastDestination)<1.f) return false;
    if(TwinsDistance(Point(bot),destination)<1.5f)
        return tank && bot->GetMotionMaster()->GetCurrentMovementGeneratorType()==CHASE_MOTION_TYPE;
    WorldPosition start(bot),end(531,destination.x,destination.y,destination.z);
    end.setZ(bot->GetMap()->GetHeight(destination.x,destination.y,destination.z,true));
    auto blocked=[&]() {
        uint32 now=WorldTimer::getMSTime();
        if(!lastReport || now-lastReport>10000)
        {lastReport=now;sPlayerbotAIConfig.logEvent(ai,"TwinsPositionBlocked","incomplete, unsafe or wrong-side route","stage tank teams on their sides before pulling");}
        return false;
    };
    if(!TwinsRoom(531,{end.getX(),end.getY(),end.getZ()})) return blocked();
    auto route=start.GetPathTo(end,bot);
    if(!end.isPathTo(route,1.f,1.f)) return blocked();
    auto hazards=AI_VALUE(std::list<HazardPosition>,"hazards");
    TwinsPoint previous=Point(bot);
    for(auto const& point:route)
    {
        TwinsPoint next{point.getX(),point.getY(),point.getZ()};
        unsigned steps=std::max(1u,static_cast<unsigned>(std::ceil(TwinsDistance(previous,next))));
        TwinsPoint sampleBefore=previous;
        for(unsigned i=1;i<=steps;++i)
        {
            float f=float(i)/steps;
            TwinsPoint sample{previous.x+f*(next.x-previous.x),previous.y+f*(next.y-previous.y),previous.z+f*(next.z-previous.z)};
            if(!TwinsRoom(531,sample) || (tank && TwinsOnSide(previous,Side(ai)) && !TwinsOnSide(sample,Side(ai))) ||
                (caster && !TwinsSafeStep(sampleBefore,sample,Point(caster),radius))) return blocked();
            WorldPosition position(531,sample.x,sample.y,sample.z);
            for(auto const& hazard:hazards)
            {
                // The burst circle above allows monotonic escape when inside.
                if(caster && hazard.first.distance(WorldPosition(caster))<.1f && std::abs(hazard.second-radius)<.1f) continue;
                if(position.distance(hazard.first)<=hazard.second) return blocked();
            }
            sampleBefore=sample;
        }
        previous=next;
    }
    path=route;return true;
}
bool TwinsPositionAction::isUseful()
{
    uint32 now=WorldTimer::getMSTime();
    if(lastCheck && now-lastCheck<250) return prepared;
    lastCheck=now;return prepared=Prepare();
}
bool TwinsPositionAction::Execute(Event&)
{
    lastCheck=0;prepared=false;
    if(!Prepare()) return false;
    ai->StopMoving();
    if(path.empty()) return true;
    bot->InterruptNonMeleeSpells(false);
    Movement::MoveSplineInit init(*bot,"TwinsSide");
    init.MovebyPath(WorldPosition().toPointsArray(path));init.SetWalk(false);
    if(!init.Launch()) return false;
    lastSpline=bot->movespline->GetId();lastDestination=destination;
    AI_VALUE(LastMovement&,"last movement").lastPath.clear();ClearIdleState();SetDuration(500);return true;
}
bool TwinsEngageAction::isUseful()
{
    if(!TwinsOpeningEnabled(ai)) return false;
    Unit* target=nullptr;
    bool controlled=SelectTwinsTankTarget(ai,target) || SelectTwinsDpsTarget(ai,target);
    if(PetOnCaster(ai)) return true;
    if(!controlled) return false;
    Unit* current=AI_VALUE(Unit*,"current target");
    if(current!=target) return true;
    if(!target) return false;
    if(TankEntry(ai)==15276 && (bot->HasUnitState(UNIT_STAT_MELEE_ATTACKING) ||
        bot->GetMotionMaster()->GetCurrentMovementGeneratorType()==CHASE_MOTION_TYPE)) return true;
    if(TankEntry(ai)==15275 && !ai->HasAura("defensive stance",bot) && bot->HasSpell(71)) return true;
    return TankEntry(ai)!=15276 && (bot->GetVictim()!=target ||
        (!ai->IsRanged(bot) && !bot->HasUnitState(UNIT_STAT_MELEE_ATTACKING)));
}
bool TwinsEngageAction::Execute(Event& event)
{
    if(!TwinsOpeningEnabled(ai)) return false;
    if(PetOnCaster(ai)) RecallPet(ai);
    Unit* target=nullptr;
    if(!SelectTwinsTankTarget(ai,target) && !SelectTwinsDpsTarget(ai,target)) return false;
    ClearWrongTarget(ai,target);
    if(!target) return true;
    if(TankEntry(ai)==15276)
    {
        if(bot->GetMotionMaster()->GetCurrentMovementGeneratorType()==CHASE_MOTION_TYPE) ai->StopMoving();
        bot->AttackStop();bot->SetSelectionGuid(target->GetObjectGuid());SET_AI_VALUE(Unit*,"current target",target);
        return true;
    }
    if(TankEntry(ai)==15275 && !ai->HasAura("defensive stance",bot) && bot->HasSpell(71))
        return ai->DoSpecificAction("defensive stance",event,true);
    return Attack(GetMaster(),target);
}
bool TwinsSearingPainAction::isUseful()
{
    Unit* target=nullptr;
    return TankEntry(ai)==15276 && SelectTwinsTankTarget(ai,target) && target &&
        ai->GetBot()->GetDistance(target)>12.f && ai->CanCastSpell("searing pain",target);
}
bool TwinsSearingPainAction::Execute(Event&)
{
    Unit* target=nullptr;
    return isUseful() && SelectTwinsTankTarget(ai,target) && target && ai->CastSpell("searing pain",target);
}
namespace
{
class TwinsOpeningMultiplier : public Multiplier
{
public:
    TwinsOpeningMultiplier(PlayerbotAI* ai) : Multiplier(ai,"twins assigned side") {}
    float GetValue(Action* action) override
    {
        if(!action) return 1;
        std::string name=action->getName();
        // Preserve the older physical-DPS exclusion/retarget behavior too,
        // including its explicit attack/RTI and crowd-control exceptions.
        if(!action->IsCrowdControlAction() && name!="attack my target" && name!="attack rti target" &&
            (dynamic_cast<AttackAction*>(action) || dynamic_cast<CastSpellAction*>(action) || name=="pet attack") &&
            IsRaidDpsTargetExcluded(ai,action->GetTarget())) return 0;
        if(!TwinsOpeningEnabled(ai)) return 1;
        if(name=="twins position" || name=="twins engage" || name=="twins searing pain" ||
            name=="set facing" || name=="move away from hazard" || name=="flee" || name=="runaway") return 1;
        if(name=="pet attack" && !TwinsPetAttackAllowed(ai,action->GetTarget())) return 0;
        if(dynamic_cast<CastHealingSpellAction*>(action)) return 1;
        if((dynamic_cast<AttackAction*>(action) || dynamic_cast<CastSpellAction*>(action)) &&
            IsTwinsOpeningTargetExcluded(ai,action->GetTarget())) return 0;
        if(!TankEntry(ai)) return 1;
        Unit* own=Boss(ai,TankEntry(ai));
        if(own && OtherOrder(ai,own)) return 1;
        if(dynamic_cast<MovementAction*>(action) || dynamic_cast<CastReachTargetSpellAction*>(action)) return 0;
        // Ranged tank's ordinary DPS rotation/pet/melee must not replace its
        // threat spell. Self defenses and mana tools (Shadow Ward/Life Tap) stay.
        if(TankEntry(ai)==15276 && (name=="melee" || name=="shoot" || name=="pet attack" ||
            (dynamic_cast<CastSpellAction*>(action) && action->GetTarget()!=ai->GetBot()))) return 0;
        return 1;
    }
};
}
bool TwinsOpeningTrigger::IsActive()
{
    auto* a=ai->GetAiObjectContext()->GetAction(action);
    return a && a->isUseful();
}
void TwinsOpeningStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("raid dps target excluded",NextAction::array(0,new NextAction("raid dps retarget",ACTION_HIGH+5),nullptr)));
    triggers.push_back(new TriggerNode("twins engage needed",NextAction::array(0,new NextAction("twins engage",ACTION_HIGH+6),nullptr)));
    triggers.push_back(new TriggerNode("twins searing pain ready",NextAction::array(0,new NextAction("twins searing pain",ACTION_HIGH+5),nullptr)));
    triggers.push_back(new TriggerNode("twins position needed",NextAction::array(0,new NextAction("twins position",ACTION_EMERGENCY+3),nullptr)));
}
void TwinsOpeningStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& t) {InitCombatTriggers(t);}
void TwinsOpeningStrategy::InitReactionTriggers(std::list<TriggerNode*>& t)
{t.push_back(new TriggerNode("twins position needed",NextAction::array(0,new NextAction("twins position",ACTION_EMERGENCY+3),nullptr)));}
void TwinsOpeningStrategy::InitCombatMultipliers(std::list<Multiplier*>& m) {m.push_back(new TwinsOpeningMultiplier(ai));}
void TwinsOpeningStrategy::InitNonCombatMultipliers(std::list<Multiplier*>& m) {InitCombatMultipliers(m);}
void TwinsOpeningStrategy::InitReactionMultipliers(std::list<Multiplier*>& m) {InitCombatMultipliers(m);}
