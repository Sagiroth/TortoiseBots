#include "playerbot/playerbot.h"
#include "TwinsOpeningActions.h"
#include "playerbot/TwinsTeleportPolicy.h"
#include <map>
#include <mutex>
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
// Map-thread calls may re-enter spell guards while dispatching the pair.
// Store GUIDs, never player pointers, across updates.
struct PairedPull
{
    ObjectGuid owner,target;
    uint32 started=0,launched=0;
    bool pending=false,dispatching=false;
};
std::recursive_mutex pairMutex;
std::map<std::pair<uint32,uint32>,PairedPull> pairedPulls;
PairedPull* Pair(PlayerbotAI* ai)
{
    if(!ai || !ai->GetBot()->GetGroup()) return nullptr;
    auto i=pairedPulls.find({ai->GetBot()->GetInstanceId(),ai->GetBot()->GetGroup()->GetId()});
    return i==pairedPulls.end() ? nullptr:&i->second;
}
bool PairWaiting(PlayerbotAI* ai)
{
    std::lock_guard<std::recursive_mutex> lock(pairMutex);
    auto* p=Pair(ai);return p && p->pending && !p->dispatching &&
        uint32(WorldTimer::getMSTime()-p->started)<=30000;
}
bool PairCasting(PlayerbotAI* ai)
{
    std::lock_guard<std::recursive_mutex> lock(pairMutex);
    auto* p=Pair(ai);
    return p && p->launched && uint32(WorldTimer::getMSTime()-p->launched)<3000 &&
        ai->GetBot()->IsNonMeleeSpellCasted(false,false,false);
}
bool Has(PlayerbotAI* ai,char const* name) {return ai->HasStrategy(name,BotState::BOT_STATE_COMBAT);}
TwinsPoint Point(WorldObject* unit) {return {unit->GetPositionX(),unit->GetPositionY(),unit->GetPositionZ()};}
int Side(PlayerbotAI* ai) {return TwinsSide(Has(ai,"twins west"),Has(ai,"twins east"));}
bool HealerRole(PlayerbotAI* ai)
{ return Has(ai,"twins healer") && Side(ai)>=0 && ai->IsHeal(ai->GetBot()); }
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
    // DoStopAttack clears both victims during the scripted two-second freeze.
    // ResetThreat keeps references (including zero threat); zone combat inserts
    // the raid. Membership, not a positive threat amount, bridges that interval.
    for(HostileReference const* ref:boss->GetThreatManager().getThreatList())
        if(ref->getUnitGuid().IsPlayer())
            if(Player* member=boss->GetMap()->GetPlayer(ref->getUnitGuid()))
                if(member->IsAlive() && member->GetGroup()==group) return true;
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
TwinsTeleportState ObserveTwins(PlayerbotAI* ai);
Unit* LocalTankBoss(PlayerbotAI* ai)
{
    unsigned entry=TankEntry(ai);
    Unit* boss=entry ? Boss(ai,entry) : nullptr;
    auto state=ObserveTwins(ai);
    if(state.opening || !state.active)
        return boss && Side(ai)==(state.epoch ? TwinsPointSide(entry==15275 ? state.physical:state.caster):(entry==15275 ? 0:1)) ? boss:nullptr;
    return boss && TwinsOnSide(Point(boss),Side(ai)) && !OtherOrder(ai,boss) ? boss : nullptr;
}
float BurstRadius(PlayerbotAI* ai,Unit* boss)
{ return 12.f+boss->GetObjectBoundingRadius()+ai->GetBot()->GetObjectBoundingRadius(); }
Player* SideTank(PlayerbotAI* ai,unsigned entry,int side)
{
    Player* result=nullptr;
    for(Player* member:LiveGroupMembers(ai->GetBot()->GetGroup()))
    {
        if(!member->IsAlive() || !member->IsInWorld() || !member->IsInMap(ai->GetBot())) continue;
        auto* peer=PlayerbotAIStorage::Instance().GetAI(member);
        if(peer && TankEntry(peer)==entry && Side(peer)==side &&
            (!result || member->GetObjectGuid()<result->GetObjectGuid())) result=member;
    }
    return result;
}
TwinsTeleportState ObserveTwins(PlayerbotAI* ai)
{
    struct Sample {TwinsTeleportState state;bool sampled=false;};
    static std::mutex mutex;
    static std::map<std::pair<uint32,uint32>,Sample> clocks;
    static uint32 lastCleanup=0;
    uint32 now=WorldTimer::getMSTime();
    TwinsTeleportState result;bool changed=false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if(uint32(now-lastCleanup)>60000)
        {
            lastCleanup=now;
            for(auto i=clocks.begin();i!=clocks.end();)
                if(uint32(now-i->second.state.sampledAt)>120000) i=clocks.erase(i);else ++i;
        }
        auto& sample=clocks[{ai->GetBot()->GetInstanceId(),ai->GetBot()->GetGroup()->GetId()}];
        auto& state=sample.state;
        // One bounded roster sample per raid per 100 ms, not per action/path point.
        if(sample.sampled && uint32(now-state.sampledAt)<100) return state;
        if(sample.sampled && uint32(now-state.sampledAt)>2000) state=TwinsTeleportState{};
        Unit* physical=Boss(ai,15275),*caster=Boss(ai,15276);
        bool engaged=GroupEngaged(ai,physical) || GroupEngaged(ai,caster),owners=false;
        if(physical && caster)
        {
            Player* warrior=SideTank(ai,15275,TwinsPointSide(Point(physical)));
            Player* warlock=SideTank(ai,15276,TwinsPointSide(Point(caster)));
            owners=warrior && warlock && physical->GetVictim()==warrior && caster->GetVictim()==warlock &&
                TwinsOnSide(Point(warrior),TwinsPointSide(Point(physical))) &&
                TwinsOnSide(Point(warlock),TwinsPointSide(Point(caster))) &&
                warrior->CanReachWithMeleeAutoAttack(physical) &&
                warlock->GetDistance(caster)>=15.f && warlock->GetDistance(caster)<=20.f &&
                warlock->IsWithinLOSInMap(caster,true);
        }
        bool configured=false;
        for(Player* member:LiveGroupMembers(ai->GetBot()->GetGroup()))
            if(member->IsAlive() && member->IsInMap(ai->GetBot()))
                if(auto* peer=PlayerbotAIStorage::Instance().GetAI(member))
                    if(TankEntry(peer)) {configured=true;break;}
        if((state.opening || !state.active) && physical && caster)
            owners=owners && TwinsPointSide(Point(physical))==(state.epoch ? TwinsPointSide(state.physical):0) &&
                TwinsPointSide(Point(caster))==(state.epoch ? TwinsPointSide(state.caster):1);
        unsigned phase=state.Phase(),epoch=state.epoch;
        state.Observe(now,engaged,physical ? Point(physical):TwinsPoint{},caster ? Point(caster):TwinsPoint{},owners);
        state.configured=configured;
        if(configured && !state.active) state.opening=true;
        changed=phase!=state.Phase() || epoch!=state.epoch;result=state;sample.sampled=true;
    }
    if(changed)
    {
        char const* phases[]={"idle","stable","prepare","handoff","opening"};
        sPlayerbotAIConfig.logEvent(ai,"TwinsTeleportPhase",phases[result.Phase()],std::to_string(result.epoch));
    }
    return result;
}
bool TwinsPreparing(PlayerbotAI* ai) {return ObserveTwins(ai).Preparing();}
bool DpsRole(PlayerbotAI* ai)
{ return !TankEntry(ai) && !ai->IsHeal(ai->GetBot()) && TwinsDamageEntry(Has(ai,"twins physical dps"),Has(ai,"twins spell dps")); }
bool DpsHolding(PlayerbotAI* ai) {return !ai->IsHeal(ai->GetBot()) && (TwinsOpeningHold(ai) || (DpsRole(ai) && ObserveTwins(ai).Holding()));}
float LandingRadius(PlayerbotAI* ai,Unit* boss)
{ return 28.f+boss->GetObjectBoundingRadius()+ai->GetBot()->GetObjectBoundingRadius(); }
bool PetOnCaster(PlayerbotAI* ai)
{
    Pet* pet=ai->GetBot()->GetPet();
    return pet && pet->GetVictim() && (pet->GetVictim()->GetEntry()==15276 ||
        (pet->GetVictim()->GetEntry()==15275 && ObserveTwins(ai).Holding()));
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
    if(local && OtherOrder(ai,local) && !ObserveTwins(ai).opening) return false;
    Unit* caster=Boss(ai,15276);
    auto state=ObserveTwins(ai);
    if(state.opening && local)
    {
        TwinsPoint home=Side(ai)==0 ? kTwinsWest:kTwinsEast;
        if(local->GetVictim()!=ai->GetBot() || PairWaiting(ai))
        {
            // Acquire from range before dragging. The caster may be beside the
            // physical boss, so a melee approach would enter Arcane Burst.
            float dx=home.x-local->GetPositionX(),dy=home.y-local->GetPositionY();
            float length=std::hypot(dx,dy);
            if(length<1.f){dx=Side(ai)==0 ? -1.f:1.f;dy=0;length=1;}
            for(float range:{24.f,28.f,30.f})
            {
                destination={local->GetPositionX()+dx/length*range,local->GetPositionY()+dy/length*range,home.z};
                if(!caster || TwinsDistance(destination,Point(caster))>BurstRadius(ai,caster)+1.f) return true;
            }
            return false;
        }
        Unit* brother=Boss(ai,TankEntry(ai)==15275 ? 15276:15275);
        bool separated=brother && TwinsSeparated(Point(local),Point(brother),3.f) && TwinsOnSide(Point(local),Side(ai));
        destination=TankEntry(ai)==15275 ? TwinsTankCamp(Side(ai)) :
            TwinsOutward(separated ? Point(local):home,Side(ai),18.f+local->GetObjectBoundingRadius()+ai->GetBot()->GetObjectBoundingRadius());
        return true;
    }
    if(TankEntry(ai)==15275 && local)
    { destination=TwinsTankCamp(side);return true; }
    TwinsPoint home=side==0 ? kTwinsWest : kTwinsEast;
    if(caster && TwinsOnSide(Point(caster),side))
    {
        if(TankEntry(ai)==15275)
        {
            // Warrior-first protocol: be nearest for incoming physical boss,
            // outside the current caster's burst. After receiving caster proximity
            // threat, withdraw while the warlock takes over with Searing Pain.
            float offset=TwinsPreparing(ai) ? 13.f+caster->GetObjectBoundingRadius()+ai->GetBot()->GetObjectBoundingRadius() : 24.f;
            destination=TwinsOutward(Point(caster),side,offset);return true;
        }
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

bool ai::TwinsPairedPullPending(PlayerbotAI* ai) {return PairWaiting(ai);}

void ai::TwinsBeginPairedPull(PlayerbotAI* ai,Unit* target)
{
    std::lock_guard<std::recursive_mutex> lock(pairMutex);
    uint32 now=WorldTimer::getMSTime();
    for(auto i=pairedPulls.begin();i!=pairedPulls.end();)
        if(uint32(now-i->second.started)>120000) i=pairedPulls.erase(i);else ++i;
    auto& pair=pairedPulls[{ai->GetBot()->GetInstanceId(),ai->GetBot()->GetGroup()->GetId()}];
    pair={ai->GetBot()->GetObjectGuid(),target->GetObjectGuid(),now,0,true,false};
    sPlayerbotAIConfig.logEvent(ai,"TwinsPairedPull","armed","west Shoot; east Searing Pain");
}
namespace
{
void UpdatePairedPull(PlayerbotAI* ai)
{
    std::lock_guard<std::recursive_mutex> lock(pairMutex);
    auto* pair=Pair(ai);
    if(!pair || !pair->pending || pair->dispatching) return;
    Player* owner=ai->GetBot()->GetMap()->GetPlayer(pair->owner);
    auto* ownerAI=owner ? PlayerbotAIStorage::Instance().GetAI(owner):nullptr;
    auto* pull=ownerAI ? PullStrategy::Get(ownerAI):nullptr;
    if(!owner || !owner->IsAlive() || owner->GetGroup()!=ai->GetBot()->GetGroup() ||
        !pull || !pull->IsCommandActive() || pull->IsCommandPullback() || !pull->GetTarget() ||
        pull->GetTarget()->GetObjectGuid()!=pair->target || !TwinsTeamsReady(ai) ||
        uint32(WorldTimer::getMSTime()-pair->started)>30000)
    {
        pair->pending=false;
        if(pull && pull->IsCommandActive() && pull->GetTarget() && pull->GetTarget()->GetObjectGuid()==pair->target)
            pull->OnPullEnded();
        sPlayerbotAIConfig.logEvent(ai,"TwinsPairedPull","cancelled","order ended, team unavailable or 30s timeout");
        return;
    }
    Player* west=SideTank(ai,15275,0),*east=SideTank(ai,15276,1);
    Unit* physical=Boss(ai,15275),*caster=Boss(ai,15276);
    if(!west || !east || !physical || !caster) return;
    auto* warrior=PlayerbotAIStorage::Instance().GetAI(west);
    auto* warlock=PlayerbotAIStorage::Instance().GetAI(east);
    if(west->HasSpell(71) && !warrior->HasAura("defensive stance",west))
    {Event event;warrior->DoSpecificAction("defensive stance",event,true);return;}
    // Both acquire ranged LOS before either is allowed to fire. Positioning
    // remains responsible for validated approach paths (never a body-pull fallback).
    for(auto const& entry:{std::make_pair(warrior,physical),std::make_pair(warlock,caster)})
    {
        Player* bot=entry.first->GetBot();Unit* target=entry.second;
        if(bot->GetDistance(target)<15.f || bot->GetDistance(target)>28.f ||
            !bot->IsWithinLOSInMap(target,true) || bot->IsNonMeleeSpellCasted(false,false,false)) return;
    }
    // Stop and face BOTH before checking either spell. CastSpell otherwise
    // spends its first attempt stopping/standing instead of starting the cast.
    for(auto const& entry:{std::make_pair(warrior,physical),std::make_pair(warlock,caster)})
    {
        auto* peer=entry.first;Player* bot=peer->GetBot();
        peer->StopMoving();bot->AttackStop();bot->SetStandState(UNIT_STAND_STATE_STAND);
        sServerFacade.SetFacingTo(bot,entry.second);
        peer->GetAiObjectContext()->GetValue<Unit*>("current target")->Set(entry.second);
        bot->SetSelectionGuid(entry.second->GetObjectGuid());
    }
    // The scoped guard permits only this synchronous preflight/dispatch to
    // pass the ordinary spell gate. No second bot decision or timer intervenes.
    pair->dispatching=true;
    CastShootAction shot(warrior);
    if(!shot.isPossible() || !warlock->CanCastSpell("searing pain",caster))
    {pair->dispatching=false;return;}
    Event event;
    uint32 first=WorldTimer::getMSTime();
    bool pain=warlock->CastSpell("searing pain",caster);
    bool shoot=pain && shot.Execute(event);
    pair->dispatching=false;
    pair->pending=false;pair->launched=WorldTimer::getMSTime();
    if(shoot) pull->NotePullActionCast();
    // Core projectile/cast times still differ. This measures cast requests,
    // not impacts, and records partial failure rather than claiming atomicity.
    sPlayerbotAIConfig.logEvent(ai,"TwinsPairedPull",shoot ? "launched":"cast-failed",
        "pain="+std::to_string(pain)+" shoot="+std::to_string(shoot)+
        " dispatchMs="+std::to_string(uint32(pair->launched-first)));
}
}

bool ai::TwinsRaidControlled(PlayerbotAI* ai)
{
    Player* bot=ai ? ai->GetBot():nullptr;
    return bot && bot->IsAlive() && bot->IsInWorld() && bot->GetGroup() &&
        TwinsRoom(bot->GetMapId(),Point(bot)) && Boss(ai,15275) && Boss(ai,15276) && ObserveTwins(ai).configured;
}
bool ai::TwinsOpeningHold(PlayerbotAI* ai)
{
    if(!TwinsRaidControlled(ai)) return false;
    auto state=ObserveTwins(ai);
    if(state.active && !state.opening) return false;
    return !TankEntry(ai) || !LocalTankBoss(ai);
}
bool ai::TwinsTeamsReady(PlayerbotAI* ai)
{
    for(int side:{0,1}) for(unsigned entry:{15275u,15276u})
    {
        Player* tank=SideTank(ai,entry,side);
        if(!tank || !TwinsRoom(tank->GetMapId(),Point(tank)) || !TwinsOnSide(Point(tank),side)) return false;
        auto* peer=PlayerbotAIStorage::Instance().GetAI(tank);
        if(entry==15276 && (!peer || !peer->HasSpell("searing pain"))) return false;
    }
    auto* warrior=PlayerbotAIStorage::Instance().GetAI(SideTank(ai,15275,0));
    auto* pull=warrior ? PullStrategy::Get(warrior):nullptr;
    return pull && pull->GetRangedBlocker()==PullRangedBlocker::None;
}
bool ai::TwinsPullCandidate(PlayerbotAI* ai,Unit* target)
{
    if(!TwinsRaidControlled(ai)) return true;
    if(!target || (target->GetEntry()!=15275 && target->GetEntry()!=15276)) return true;
    return TankEntry(ai)==target->GetEntry() && Side(ai)==(target->GetEntry()==15275 ? 0:1) &&
        SideTank(ai,target->GetEntry(),Side(ai))==ai->GetBot();
}
bool ai::TwinsMayAttack(PlayerbotAI* ai,Unit* target)
{
    if(!target || (target->GetEntry()!=15275 && target->GetEntry()!=15276) || !TwinsRaidControlled(ai)) return true;
    auto state=ObserveTwins(ai);
    if(PairWaiting(ai)) return false;
    if(!state.active && !PullOrdered(ai,Boss(ai,15275),Boss(ai,15276))) return false;
    if(TankEntry(ai)) return LocalTankBoss(ai)==target && SideTank(ai,TankEntry(ai),Side(ai))==ai->GetBot();
    if(!state.active || state.Holding()) return false;
    unsigned entry=TwinsDamageEntry(Has(ai,"twins physical dps"),Has(ai,"twins spell dps"));
    // A spare tank's damage assignment wins over its generic tank tournament.
    return entry ? target->GetEntry()==entry : !ai->IsTank(ai->GetBot()) && !ai->IsHeal(ai->GetBot());
}
bool ai::TwinsBlocksSpell(PlayerbotAI* ai,uint32 spellId,Unit* target)
{
    if(!TwinsRaidControlled(ai)) return false;
    SpellEntry const* spell=sServerFacade.LookupSpellInfo(spellId);
    if(!spell || IsPositiveSpell(spell)) return false;
    if(!TankEntry(ai) && (!target || target==ai->GetBot() || target->GetEntry()==15275 || target->GetEntry()==15276))
        for(unsigned effect=0;effect<MAX_EFFECT_INDEX;++effect)
            if(spell->Effect[effect]==SPELL_EFFECT_ATTACK_ME ||
                (spell->Effect[effect]==SPELL_EFFECT_APPLY_AURA && spell->EffectApplyAuraName[effect]==SPELL_AURA_MOD_TAUNT)) return true;
    if(target && target!=ai->GetBot()) return !TwinsMayAttack(ai,target);
    // Self-centred damage/taunts must not grab the other adjacent twin.
    auto state=ObserveTwins(ai);
    return !state.active || state.opening || (!TankEntry(ai) && state.Holding());
}
void ai::TwinsEnforceOpening(PlayerbotAI* ai)
{
    if(!TwinsRaidControlled(ai)) return;
    UpdatePairedPull(ai);
    Player* bot=ai->GetBot();
    if(TankEntry(ai) && !PairWaiting(ai))
        if(Unit* local=LocalTankBoss(ai))
            if(local->GetVictim()==bot)
                if(auto* pull=PullStrategy::Get(ai))
                    if(pull->IsCommandActive() && pull->GetTarget()==local && !pull->HasPullActionCompleted()) pull->OnPullActionCompleted();
    if(auto* pull=PullStrategy::Get(ai))
        if(pull->IsCommandActive() && pull->GetTarget())
            pull->NotePullApproach(bot->GetDistance(pull->GetTarget()),bot->IsMoving());
    if(TwinsOpeningHold(ai) && bot->GetMotionMaster()->GetCurrentMovementGeneratorType()==CHASE_MOTION_TYPE) ai->StopMoving();
    if(bot->GetVictim() && !TwinsMayAttack(ai,bot->GetVictim())) bot->AttackStop();
    for(uint32 slot:{uint32(CURRENT_GENERIC_SPELL),uint32(CURRENT_CHANNELED_SPELL),uint32(CURRENT_AUTOREPEAT_SPELL)})
        if(Spell* spell=bot->GetCurrentSpell(CurrentSpellTypes(slot)))
            if(TwinsBlocksSpell(ai,spell->m_spellInfo->Id,ai->GetUnit(spell->m_targets.getUnitTargetGuid())))
                bot->InterruptSpell(CurrentSpellTypes(slot));
    if(Pet* pet=bot->GetPet())
        if(pet->GetVictim() && !TwinsMayAttack(ai,pet->GetVictim())) RecallPet(ai);
}

bool ai::TwinsOpeningEnabled(PlayerbotAI* ai)
{
    Player* bot=ai ? ai->GetBot() : nullptr;
    if(!bot || !bot->IsAlive() || !bot->IsInWorld() || bot->IsBeingTeleported() || !bot->GetGroup() ||
        !TwinsRoom(bot->GetMapId(),Point(bot))) return false;
    if(!TankEntry(ai) && !HealerRole(ai) && !Has(ai,"twins physical dps") && !Has(ai,"twins spell dps")) return false;
    Unit* physical=Boss(ai,15275),*caster=Boss(ai,15276);
    auto phase=ObserveTwins(ai);
    return phase.active || PullOrdered(ai,physical,caster);
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
    bool hold=DpsHolding(ai);Unit* physical=hold ? Boss(ai,15275):nullptr;
    if(hold) radius=LandingRadius(ai,caster);
    for(auto const& point:path)
    {
        TwinsPoint end{point.getX(),point.getY(),point.getZ()};
        unsigned count=std::max(1u,static_cast<unsigned>(std::ceil(TwinsDistance(previous,end))));
        TwinsPoint before=previous;
        for(unsigned i=1;i<=count;++i)
        {
            float f=float(i)/count;
            TwinsPoint sample{previous.x+f*(end.x-previous.x),previous.y+f*(end.y-previous.y),previous.z+f*(end.z-previous.z)};
            if(!TwinsSafeStep(before,sample,Point(caster),radius) ||
                (physical && !TwinsSafeStep(before,sample,Point(physical),LandingRadius(ai,physical)))) return false;
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
    if(own && OtherOrder(ai,own) && !ObserveTwins(ai).opening) return false;
    Unit* local=LocalTankBoss(ai);
    if(local && (GroupEngaged(ai,local) || PullOrdered(ai,Boss(ai,15275),Boss(ai,15276)))) target=local;
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
    if(DpsHolding(ai)) return true; // Clear current damage/pets until actual ownership is confirmed.
    target=boss;return true;
}
bool ai::IsTwinsOpeningTargetExcluded(PlayerbotAI* ai,Unit* target)
{
    if(!TwinsMayAttack(ai,target)) return true;
    if(!target || (target->GetEntry()!=15275 && target->GetEntry()!=15276) || !TwinsOpeningEnabled(ai)) return false;
    Unit* assigned=nullptr;
    if(TankEntry(ai)) return SelectTwinsTankTarget(ai,assigned) && target!=assigned;
    return SelectTwinsDpsTarget(ai,assigned) && target!=assigned;
}
bool ai::TwinsPetAttackAllowed(PlayerbotAI* ai,Unit* target)
{
    if(!TwinsMayAttack(ai,target)) return false;
    if(!target || !TwinsOpeningEnabled(ai)) return true;
    return target->GetEntry()!=15276 && (target->GetEntry()!=15275 || !ObserveTwins(ai).Holding());
}
std::list<HazardPosition> ai::GetTwinsOpeningHazards(PlayerbotAI* ai)
{
    std::list<HazardPosition> hazards;
    if(!TwinsOpeningEnabled(ai)) return hazards;
    bool hold=DpsHolding(ai);
    if(Unit* caster=Boss(ai,15276)) hazards.emplace_back(WorldPosition(caster),hold ? LandingRadius(ai,caster):BurstRadius(ai,caster));
    if(hold) if(Unit* physical=Boss(ai,15275)) hazards.emplace_back(WorldPosition(physical),LandingRadius(ai,physical));
    return hazards;
}
bool ai::TwinsHealTargetAllowed(PlayerbotAI* ai,Unit* target)
{
    if(!HealerRole(ai) || !TwinsOpeningEnabled(ai)) return true;
    return target && (target==ai->GetBot() || TwinsOnSide(Point(target),Side(ai)));
}
bool ai::TwinsOpeningMoveAllowed(PlayerbotAI* ai,float x,float y,float z)
{
    if(!TwinsOpeningEnabled(ai)) return true;
    TwinsPoint end{x,y,z};
    Unit* own=TankEntry(ai) ? Boss(ai,TankEntry(ai)) : nullptr;
    if((HealerRole(ai) || (own && !ObserveTwins(ai).opening && !OtherOrder(ai,own))) && (!TwinsRoom(531,end) || !TwinsOnSide(end,Side(ai)))) return false;
    Unit* caster=Boss(ai,15276);
    if(!caster) return true;
    if(!TwinsSafeStep(Point(ai->GetBot()),end,Point(caster),DpsHolding(ai) ? LandingRadius(ai,caster):BurstRadius(ai,caster))) return false;
    Unit* physical=DpsHolding(ai) ? Boss(ai,15275):nullptr;
    return !physical || TwinsSafeStep(Point(ai->GetBot()),end,Point(physical),LandingRadius(ai,physical));
}

bool TwinsPositionAction::Prepare()
{
    path.clear();
    if(!TwinsOpeningEnabled(ai) || !ai->CanMove() || bot->GetTransport() || bot->IsTaxiFlying()) return false;
    Unit* caster=Boss(ai,15276);
    float radius=caster ? BurstRadius(ai,caster) : 0;
    bool danger=caster && TwinsDistance(Point(bot),Point(caster))<radius;
    bool tank=TankEntry(ai)!=0,healer=HealerRole(ai),hold=DpsHolding(ai);
    Unit* landing=nullptr;
    for(unsigned entry:{15275u,15276u})
        if(Unit* candidate=Boss(ai,entry))
            if(hold && TwinsDistance(Point(bot),Point(candidate))<LandingRadius(ai,candidate) &&
                (!landing || TwinsDistance(Point(bot),Point(candidate))<TwinsDistance(Point(bot),Point(landing)))) landing=candidate;
    Unit* dpsTarget=nullptr;
    bool approach=!tank && SelectTwinsDpsTarget(ai,dpsTarget) && dpsTarget &&
        bot->GetDistance(dpsTarget)>sPlayerbotAIConfig.sightDistance*.8f;
    if(!tank && !healer && !danger && !approach && !landing)
    {
        // A pre-window approach spline must not continue into the landing area.
        if(hold && OwnsMovement())
            for(unsigned entry:{15275u,15276u})
                if(Unit* boss=Boss(ai,entry))
                    if(TwinsDistance(lastDestination,Point(boss))<LandingRadius(ai,boss))
                    {destination=Point(bot);return true;}
        return false;
    }
    if(tank)
    {
        Unit* own=Boss(ai,TankEntry(ai));
        if(own && OtherOrder(ai,own) && !ObserveTwins(ai).opening) return false;
        if(PairCasting(ai)) return false;
        if(!DesiredTankPosition(ai,destination)) return false;
        if(TankEntry(ai)==15275 && own && GroupEngaged(ai,own) && bot->HasSpell(71) &&
            !ai->HasAura("defensive stance",bot)) return false;
        Unit* pickup=LocalTankBoss(ai);
        if(ObserveTwins(ai).opening && pickup && (pickup->GetVictim()!=bot || PairWaiting(ai)) &&
            bot->GetDistance(pickup)>=15.f && bot->GetDistance(pickup)<=28.f && bot->IsWithinLOSInMap(pickup,true) && !danger)
            return false; // fire the acquisition shot before further movement
        // Establish ranged threat before repositioning, if already safely in
        // Searing Pain range on this side. Do not cancel that opening cast.
        if(!danger && TankEntry(ai)==15276 && caster==LocalTankBoss(ai) && GroupEngaged(ai,caster) &&
            caster->GetVictim()!=bot && TwinsOnSide(Point(bot),Side(ai)) &&
            bot->GetDistance(caster)<=28.f && bot->GetDistance(caster)>=15.f && bot->IsWithinLOSInMap(caster,true))
            return false;
    }
    else if(healer && !danger)
    {
        Unit* local=Boss(ai,15275);
        if(!local || !TwinsOnSide(Point(local),Side(ai))) local=caster;
        if(!local) return false;
        if(TwinsOnSide(Point(bot),Side(ai)) && bot->GetDistance(local)>=22.f &&
            bot->GetDistance(local)<=40.f && bot->IsWithinLOSInMap(local,true)) return false;
        if(bot->IsNonMeleeSpellCasted(true,false,true)) return false; // finish essential heals
        destination=TwinsOutward(Side(ai)==0 ? kTwinsWest:kTwinsEast,Side(ai),35.f);
    }
    else if(landing)
    {
        float dx=bot->GetPositionX()-landing->GetPositionX(),dy=bot->GetPositionY()-landing->GetPositionY();
        float length=std::hypot(dx,dy);if(length<.1f){dx=1;dy=0;length=1;}
        float clear=LandingRadius(ai,landing)+2.f;
        destination={landing->GetPositionX()+dx/length*clear,landing->GetPositionY()+dy/length*clear,bot->GetPositionZ()};
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
    // The standby point is only one yard outside our burst margin.
    // A broad arrival tolerance must not leave the warrior inside that circle.
    float arrival=tank && TankEntry(ai)==15275 && TwinsPreparing(ai) ? .25f : 1.5f;
    if(!danger && TwinsDistance(Point(bot),destination)<arrival)
        return (tank || healer) && bot->GetMotionMaster()->GetCurrentMovementGeneratorType()==CHASE_MOTION_TYPE;
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
            if(!TwinsRoom(531,sample) || ((healer || (tank && !ObserveTwins(ai).opening)) && TwinsOnSide(previous,Side(ai)) && !TwinsOnSide(sample,Side(ai))) ||
                (caster && !TwinsSafeStep(sampleBefore,sample,Point(caster),radius))) return blocked();
            WorldPosition position(531,sample.x,sample.y,sample.z);
            for(auto const& hazard:hazards)
            {
                // The burst circle above allows monotonic escape when inside.
                bool ownHazard=false;
                for(unsigned entry:{15275u,15276u})
                    if(Unit* boss=Boss(ai,entry))
                        if(hazard.first.distance(WorldPosition(boss))<.1f &&
                            std::abs(hazard.second-(hold ? LandingRadius(ai,boss):BurstRadius(ai,boss)))<.1f)
                            ownHazard=true;
                if(ownHazard) continue;
                if(position.distance(hazard.first)<=hazard.second) return blocked();
            }
            sampleBefore=sample;
        }
        previous=next;
    }
    if(!TwinsOpeningRouteAllowed(ai,route)) return blocked();
    path=route;return true;
}
bool TwinsPositionAction::OwnsMovement() const
{
    return lastSpline && bot->movespline->GetId()==lastSpline && !bot->movespline->Finalized() &&
        bot->GetMotionMaster()->GetCurrentMovementGeneratorType()!=CHASE_MOTION_TYPE;
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
    if(PairWaiting(ai) || PairCasting(ai)) return false;
    if(!TwinsOpeningEnabled(ai)) return false;
    Unit* target=nullptr;
    bool controlled=SelectTwinsTankTarget(ai,target) || SelectTwinsDpsTarget(ai,target);
    if(PetOnCaster(ai)) return true;
    if(!controlled) return false;
    Unit* current=AI_VALUE(Unit*,"current target");
    if(current!=target) return true;
    if(TankEntry(ai)==15275 && !ai->HasAura("defensive stance",bot) && bot->HasSpell(71)) return true;
    if(!target) return false;
    if(TankEntry(ai)==15275 && ObserveTwins(ai).opening && target->GetVictim()!=bot)
    {CastShootAction shot(ai);return shot.isPossible() && shot.isUseful();}
    if(TankEntry(ai)==15276 && (bot->HasUnitState(UNIT_STAT_MELEE_ATTACKING) ||
        bot->GetMotionMaster()->GetCurrentMovementGeneratorType()==CHASE_MOTION_TYPE)) return true;
    return TankEntry(ai)!=15276 && (bot->GetVictim()!=target ||
        (!ai->IsRanged(bot) && !bot->HasUnitState(UNIT_STAT_MELEE_ATTACKING)));
}
bool TwinsEngageAction::Execute(Event& event)
{
    if(PairWaiting(ai) || PairCasting(ai)) return false;
    if(!TwinsOpeningEnabled(ai)) return false;
    if(PetOnCaster(ai)) RecallPet(ai);
    Unit* target=nullptr;
    if(!SelectTwinsTankTarget(ai,target) && !SelectTwinsDpsTarget(ai,target)) return false;
    ClearWrongTarget(ai,target);
    if(TankEntry(ai)==15275 && !ai->HasAura("defensive stance",bot) && bot->HasSpell(71))
        return ai->DoSpecificAction("defensive stance",event,true);
    if(!target)
    {
        if(DpsHolding(ai) && bot->GetMotionMaster()->GetCurrentMovementGeneratorType()==CHASE_MOTION_TYPE) ai->StopMoving();
        return true;
    }
    if(TankEntry(ai)==15275 && ObserveTwins(ai).opening && target->GetVictim()!=bot)
    {
        bot->AttackStop();SET_AI_VALUE(Unit*,"current target",target);
        CastShootAction shot(ai);return shot.isPossible() && shot.Execute(event);
    }
    if(TankEntry(ai)==15276)
    {
        if(bot->GetMotionMaster()->GetCurrentMovementGeneratorType()==CHASE_MOTION_TYPE) ai->StopMoving();
        bot->AttackStop();bot->SetSelectionGuid(target->GetObjectGuid());SET_AI_VALUE(Unit*,"current target",target);
        return true;
    }
    return Attack(GetMaster(),target);
}
bool TwinsSearingPainAction::isUseful()
{
    Unit* target=nullptr;
    return !PairWaiting(ai) && !PairCasting(ai) && TankEntry(ai)==15276 && SelectTwinsTankTarget(ai,target) && target &&
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
        if(PairCasting(ai) && TankEntry(ai) && name!="move away from hazard" && name!="flee" && name!="runaway" &&
            (dynamic_cast<MovementAction*>(action) || dynamic_cast<AttackAction*>(action) ||
             dynamic_cast<CastSpellAction*>(action) || name=="twins engage" || name=="twins searing pain")) return 0;
        if(name=="twins position" || name=="twins engage" || name=="twins searing pain" ||
            name=="set facing" || name=="move away from hazard" || name=="flee" || name=="runaway") return 1;
        if(TankEntry(ai) && ObserveTwins(ai).opening && (name=="pull action" || name=="reach pull")) return 0;
        if(name=="pet attack" && !TwinsPetAttackAllowed(ai,action->GetTarget())) return 0;
        if(dynamic_cast<CastHealingSpellAction*>(action)) return TwinsHealTargetAllowed(ai,action->GetTarget()) ? 1 : 0;
        if(!action->IsCrowdControlAction() && DpsHolding(ai) &&
            (name=="melee" || name=="shoot" || name=="pet attack" ||
             action->getThreatType()==ActionThreatType::ACTION_THREAT_AOE ||
             (action->GetTarget()!=ai->GetBot() && (dynamic_cast<AttackAction*>(action) || dynamic_cast<CastSpellAction*>(action))))) return 0;
        if(HealerRole(ai) && dynamic_cast<ReachTargetAction*>(action))
            return TwinsHealTargetAllowed(ai,action->GetTarget()) ? 1 : 0;
        auto* positioning=dynamic_cast<TwinsPositionAction*>(ai->GetAiObjectContext()->GetAction("twins position"));
        if(dynamic_cast<MovementAction*>(action) &&
            (HealerRole(ai) || DpsHolding(ai) || (positioning && positioning->OwnsMovement()))) return 0;
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
