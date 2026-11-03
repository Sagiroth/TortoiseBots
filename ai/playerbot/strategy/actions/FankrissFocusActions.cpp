#include "playerbot/playerbot.h"
#include "FankrissFocusActions.h"
#include "FankrissRaidActions.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "playerbot/strategy/values/TargetValue.h"
#include "playerbot/strategy/generic/FankrissStrategy.h"
#include <map>
#include <mutex>
#include <set>
#include <cmath>
#include <algorithm>
using namespace ai;
namespace {
struct FocusRecord {ObjectGuid target;uint32 seen=0,started=0,controlAt=0,nextArrival=0;std::map<ObjectGuid,uint32> arrivals;};
std::map<std::pair<uint32,uint64>,FocusRecord> focuses;
std::mutex focusMutex;
std::map<std::pair<uint32,uint64>,uint32> spawnBorn;
bool FocusCcHeld(PlayerbotAI* ai,Unit* spawn) {
 AiObjectContext* context=ai->GetAiObjectContext();
 if(AI_VALUE(Unit*,"cc target")==spawn)return true;
 class Probe:public FindNonCcTargetStrategy {
 public:explicit Probe(PlayerbotAI* ai):FindNonCcTargetStrategy(ai){}
 using FindNonCcTargetStrategy::IsCcTarget;
 void CheckAttacker(Unit*,ThreatManager*) override {}
 } probe(ai);
 return probe.IsCcTarget(spawn);
}
ObjectGuid FankrissChosenSpawnLocked(PlayerbotAI* ai) {
 Player* bot=ai->GetBot();std::lock_guard<std::mutex> lock(focusMutex);
 auto it=focuses.find({bot->GetInstanceId(),bot->GetGroup()->GetLeaderGuid().GetRawValue()});
 return it==focuses.end()?ObjectGuid():it->second.target;
}
bool LiveSpawn(Player* bot,Unit* spawn) {
 return spawn&&spawn->GetEntry()==15630&&spawn->IsAlive()&&spawn->IsInWorld()&&
  spawn->IsInCombat()&&spawn->IsInMap(bot)&&bot->IsValidAttackTarget(spawn)&&
  (!spawn->IsCreature()||!static_cast<Creature*>(spawn)->IsInEvadeMode());
}
}
bool ai::FankrissDpsOptedIn(PlayerbotAI* ai) {
 return ai->HasStrategy("fankriss spawn focus",BotState::BOT_STATE_COMBAT)||ai->HasStrategy("fankriss stack",BotState::BOT_STATE_COMBAT);
}
bool ai::FankrissFocusEnabled(PlayerbotAI* ai) {
 Player* bot=ai?ai->GetBot():nullptr;
 return bot&&bot->IsAlive()&&bot->IsInWorld()&&bot->IsInCombat()&&bot->GetMapId()==531&&bot->GetGroup()&&
  !ai->IsTank(bot)&&!ai->IsHeal(bot)&&FankrissDpsOptedIn(ai);
}
bool ai::RetainFankrissFocusTarget(PlayerbotAI* ai,Unit* target) {
 return FankrissFocusEnabled(ai)&&LiveSpawn(ai->GetBot(),target)&&
  ai->GetBot()->GetVictim()==target&&FankrissSpawnAtStack(ai,target);
}
bool ai::FankrissBurnSpawn(PlayerbotAI* ai, Unit* target) {
 if(target&&target->GetEntry()!=15630&&target->GetEntry()!=15510)return GetFankrissRescueTarget(ai)==target;
 return target && FankrissFocusEnabled(ai) && LiveSpawn(ai->GetBot(),target) &&
  FankrissSpawnAtStack(ai,target) && FankrissChosenSpawnLocked(ai)==target->GetObjectGuid();
}
Unit* ai::GetFankrissFocusSpawn(PlayerbotAI* ai) {
 if(!FankrissFocusEnabled(ai))return nullptr;
 Player* bot=ai->GetBot();auto key=std::make_pair(bot->GetInstanceId(),bot->GetGroup()->GetLeaderGuid().GetRawValue());
 uint32 now=WorldTimer::getMSTime();
 std::lock_guard<std::mutex> lock(focusMutex);
 for(auto it=focuses.begin();it!=focuses.end();)if(uint32(now-it->second.seen)>60000)it=focuses.erase(it);else ++it;
 auto& focus=focuses[key];focus.seen=now;
 // Track all arrivals while retaining the focus so equal-health survivors
 // can later be ordered by when they first reached the stack.
 for(auto it=focus.arrivals.begin();it!=focus.arrivals.end();) {
  Unit* u=ai->GetUnit(it->first);
  if(!u||!u->IsAlive()||!u->IsInWorld()||!u->IsInCombat()||!u->IsInMap(bot))it=focus.arrivals.erase(it);else ++it;
 }
 std::map<ObjectGuid,Unit*> candidates;
 for(Player* member:LiveGroupMembers(bot->GetGroup())) {
  if(!member->IsInMap(bot)||!member->IsAlive())continue;
  for(Unit* attacker:member->GetAttackers())if(LiveSpawn(bot,attacker))candidates.emplace(attacker->GetObjectGuid(),attacker);
  if(LiveSpawn(bot,member->GetVictim()))candidates.emplace(member->GetVictim()->GetObjectGuid(),member->GetVictim());
 }
 // The control timer counts from the first time the raid saw the Spawn in
 // combat, not from when it became the focus.
 for(auto it=spawnBorn.begin();it!=spawnBorn.end();)if(uint32(now-it->second)>600000)it=spawnBorn.erase(it);else ++it;
 for(auto const& entry:candidates)
  spawnBorn.emplace(std::make_pair(bot->GetInstanceId(),entry.first.GetRawValue()),now);
 for(auto const& entry:candidates)
  if(FankrissSpawnAtStack(ai,entry.second)&&!focus.arrivals.count(entry.first))
   focus.arrivals.emplace(entry.first,++focus.nextArrival);
 Unit* held=ai->GetUnit(focus.target);
 if(held&&held->IsAlive()&&held->IsInWorld()&&held->IsInMap(bot)&&held->IsInCombat()&&
    (!held->IsCreature()||!static_cast<Creature*>(held)->IsInEvadeMode())) {
  // A bot-local CC, range or attackability failure must not replace the
  // group's live focus. No-chase still applies to this bot's handoff.
  return LiveSpawn(bot,held)&&FankrissSpawnAtStack(ai,held)&&!FocusCcHeld(ai,held)?held:nullptr;
 }
 focus.target.Clear();
 Unit* best=nullptr;uint32 health=0,oldest=0;
 for(auto const& entry:candidates) {
  Unit* spawn=entry.second;if(!FankrissSpawnAtStack(ai,spawn)||FocusCcHeld(ai,spawn))continue;
  uint32 arrival=focus.arrivals.at(entry.first);
  if(!best||spawn->GetHealth()<health||(spawn->GetHealth()==health&&arrival<oldest))
   {best=spawn;health=spawn->GetHealth();oldest=arrival;}
 }
 if(best){
  focus.target=best->GetObjectGuid();focus.started=now;focus.controlAt=0;
 }
 return best;
}
void ai::UpdateFankrissFocus(PlayerbotAI* ai) {
 if(FankrissSingleTargetActive(ai)) {
  Player* bot=ai->GetBot();
  for(CurrentSpellTypes type:{CURRENT_GENERIC_SPELL,CURRENT_CHANNELED_SPELL,CURRENT_MELEE_SPELL})
   if(Spell* spell=bot->GetCurrentSpell(type))if(FankrissAoeSpell(spell->m_spellInfo))bot->InterruptSpell(type,false);
  std::vector<uint32> remove;
  for(auto const& entry:bot->GetSpellAuraHolderMap())
   if(entry.second&&entry.second->GetCasterGuid()==bot->GetObjectGuid()&&FankrissAoeSpell(entry.second->GetSpellProto()))
    remove.push_back(entry.second->GetSpellProto()->Id);
  for(uint32 id:remove)bot->RemoveAurasDueToSpell(id);
 }
 if(!FankrissFocusEnabled(ai))return;
 auto* action=dynamic_cast<FankrissFocusAction*>(ai->GetAiObjectContext()->GetAction("fankriss focus spawn"));
 if(action)action->Update();
}
void FankrissFocusAction::Update() {
 uint32 now=WorldTimer::getMSTime();
 if(lastUpdate&&uint32(now-lastUpdate)<250)return;
 lastUpdate=now;
 Unit* spawn=GetFankrissDpsTarget(ai);if(!spawn)return;
 // Target selection cannot wait behind a running reaction or an action's
 // duration. Do not move, bypass control loss, or repeatedly cancel casts.
 bool selected=AI_VALUE(Unit*,"current target")==spawn&&bot->GetVictim()==spawn;
 if(selected)return;
 char const* reason="attack-refused";
 if(!isPossible())reason="control-loss";
 else if(bot->GetDistance(spawn)>60.f)reason="outside-handoff-range";
 else if(bot->HasAura(720)||bot->HasAura(731)||bot->HasAura(1121))reason="webbed";
 else {
  Event event;
  if(Execute(event)) {ai->SetActionDuration(uint32(0));reason="switched";}
  else reason="attack-refused";
 }
 if(!lastReport||uint32(now-lastReport)>=2000||std::string(reason)=="switched") {
  lastReport=now;
  sPlayerbotAIConfig.logEvent(ai,spawn->GetEntry()==15630?"FankrissFocusHandoff":"FankrissDpsHandoff",reason,
   "instance="+std::to_string(bot->GetInstanceId())+"|spawn_guid="+std::to_string(spawn->GetObjectGuid().GetRawValue())+
   "|distance="+std::to_string(bot->GetDistance(spawn)));
 }
}
bool FankrissFocusAction::isUseful() {
 Unit* spawn=GetFankrissDpsTarget(ai);if(!spawn||bot->GetDistance(spawn)>60.f)return false;
 // The timed nearby Spawn now outranks old orders in the normal DPS selector.
 context->GetValue<Unit*>("dps target")->Reset();
 if(AI_VALUE(Unit*,"dps target")!=spawn)return false;
 return AI_VALUE(Unit*,"current target")!=spawn||bot->GetVictim()!=spawn;
}
bool FankrissFocusAction::Execute(Event&) {
 Unit* spawn=GetFankrissDpsTarget(ai);if(!spawn||!isPossible()||!isUseful())return false;
 // Stop a boss cast only on a real target handoff, never each trigger tick.
 bot->InterruptNonMeleeSpells(false);
 // Select without a forced reach: the formation owns movement, not the target handoff.
 bool attacked=Attack(GetMaster(),spawn,0.f);
 if(attacked)sPlayerbotAIConfig.logEvent(ai,spawn->GetEntry()==15630?"FankrissFocusTarget":"FankrissDpsTarget","engaged encounter target",
  "map=531|instance="+std::to_string(bot->GetInstanceId())+"|spawn_guid="+std::to_string(spawn->GetObjectGuid().GetRawValue()));
 return attacked;
}
void FankrissSpawnStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers) {
 triggers.push_back(new TriggerNode("fankriss control needed",NextAction::array(0,new NextAction("fankriss control spawn",ACTION_HIGH+10),NULL)));
 triggers.push_back(new TriggerNode("fankriss focus needed",NextAction::array(0,new NextAction("fankriss focus spawn",ACTION_HIGH+9),NULL)));
}
void FankrissSpawnStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers) {InitCombatTriggers(triggers);}

namespace {
uint32 FankrissControlSpell(PlayerbotAI* ai,Unit* spawn) {
 if(!spawn||spawn->HasUnitState(UNIT_STAT_STUNNED))return 0;
 std::vector<char const*> names;
 switch(ai->GetBot()->GetClass()) {
 case CLASS_WARRIOR:names={"concussion blow","hamstring"};break;
 case CLASS_ROGUE:names={"kidney shot"};break;
 case CLASS_PALADIN:names={"hammer of justice"};break;
 case CLASS_HUNTER:names={"concussive shot"};break;
 case CLASS_MAGE:names={"slow","frostbolt"};break;
 case CLASS_SHAMAN:names={"frost shock"};break;
 case CLASS_WARLOCK:names={"curse of exhaustion"};break;
 case CLASS_DRUID:names={"bash"};break;
 default:return 0;
 }
 AiObjectContext* context=ai->GetAiObjectContext();
 for(char const* name:names) {
  uint32 id=AI_VALUE2(uint32,"spell id",name);
  SpellEntry const* spell=sSpellTemplate.LookupEntry<SpellEntry>(id);
  if(!spell||!ai->GetBot()->HasSpell(id)||ai->HasAura(name,spawn)||spawn->IsImmuneToSpell(spell,false))continue;
  bool useful=false;
  for(unsigned i=0;i<3;++i)if(spell->Effect[i]==SPELL_EFFECT_APPLY_AURA&&
   (spell->EffectApplyAuraName[i]==SPELL_AURA_MOD_STUN||
    (spell->EffectApplyAuraName[i]==SPELL_AURA_MOD_DECREASE_SPEED&&!spawn->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED)))&&
   !spawn->IsImmuneToSpellEffect(spell,SpellEffectIndex(i),false))useful=true;
  if(useful&&ai->CanCastSpell(id,spawn))return id;
 }
 return 0;
}
bool ControlDue(PlayerbotAI* ai,Unit* spawn,bool claim) {
 if(!spawn)return false;Player* bot=ai->GetBot();uint32 now=WorldTimer::getMSTime();
 std::lock_guard<std::mutex> lock(focusMutex);
 auto it=focuses.find({bot->GetInstanceId(),bot->GetGroup()->GetLeaderGuid().GetRawValue()});
 if(it==focuses.end()||it->second.target!=spawn->GetObjectGuid())return false;
 auto& focus=it->second;
 auto birth=spawnBorn.find({spawn->GetInstanceId(),spawn->GetObjectGuid().GetRawValue()});
 uint32 age=uint32(now-(birth==spawnBorn.end()?focus.started:birth->second));
 if(!spawn->HasAura(26662)&&age<10000)return false;
 if(focus.controlAt&&uint32(now-focus.controlAt)<1500)return false;
 if(claim)focus.controlAt=now;
 return true;
}
}
bool FankrissControlAction::isUseful() {
 Unit* spawn=GetFankrissFocusSpawn(ai);
 return spawn&&ControlDue(ai,spawn,false)&&FankrissControlSpell(ai,spawn);
}
bool FankrissControlAction::Execute(Event&) {
 Unit* spawn=GetFankrissFocusSpawn(ai);uint32 id=FankrissControlSpell(ai,spawn);
 return id&&ControlDue(ai,spawn,true)&&ai->CastSpell(id,spawn);
}

bool ai::FankrissSingleTargetActive(PlayerbotAI* ai) {
 Player* bot=ai?ai->GetBot():nullptr;
 if(!bot||!bot->IsAlive()||!bot->IsInWorld()||bot->GetMapId()!=531||!bot->GetGroup()||ai->IsTank(bot)||ai->IsHeal(bot)||
    !FankrissDpsOptedIn(ai))return false;
 Unit* boss=ai->GetAiObjectContext()->GetValue<Unit*>("fankriss active boss")->Get();
 return boss&&boss->IsAlive()&&boss->IsInCombat()&&boss->IsInMap(ai->GetBot());
}
namespace {
bool FankrissAoeSpellDepth(SpellEntry const* spell,unsigned depth) {
 if(!spell)return false;
 // These cast a self aura or place an object; their harmful area effect
 // is indirect, so the parent need not carry an enemy-area target flag.
 for(char const* name:{"Blade Flurry","Sweeping Strikes","Hellfire","Explosive Trap","Frost Trap","Magma Totem","Fire Nova Totem"})
  if(spell->SpellName[0]==name)return true;
 auto enemyArea=[](uint32 target) {
  return target==TARGET_ENUM_UNITS_ENEMY_AOE_AT_SRC_LOC||target==TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC||
   target==TARGET_ENUM_UNITS_ENEMY_IN_CONE_24||target==TARGET_ENUM_UNITS_ENEMY_AOE_AT_DYNOBJ_LOC||target==TARGET_ENUM_UNITS_ENEMY_WITHIN_CASTER_RANGE;
 };
 for(unsigned i=0;i<3;++i) {
  if(enemyArea(spell->EffectImplicitTargetA[i])||enemyArea(spell->EffectImplicitTargetB[i]))return true;
  if(spell->EffectChainTarget[i]>1&&(spell->EffectImplicitTargetA[i]==TARGET_UNIT_ENEMY||spell->EffectImplicitTargetB[i]==TARGET_UNIT_ENEMY))return true;
 }
 // Proc auras can be positive while their triggered spell damages an area.
 if(depth)for(unsigned i=0;i<3;++i)if(spell->EffectTriggerSpell[i])
  if(FankrissAoeSpellDepth(sSpellTemplate.LookupEntry<SpellEntry>(spell->EffectTriggerSpell[i]),depth-1))return true;
 if(spell->IsPositiveSpell())return false;
 if(spell->IsAreaOfEffectSpell())return true;
 for(unsigned i=0;i<3;++i)if(spell->EffectChainTarget[i]>1)return true;
 return false;
}
}
bool ai::FankrissAoeSpell(SpellEntry const* spell) {return FankrissAoeSpellDepth(spell,2);}
bool ai::FankrissBlocksAoe(PlayerbotAI* ai,uint32 id) {
 return FankrissSingleTargetActive(ai)&&FankrissAoeSpell(sSpellTemplate.LookupEntry<SpellEntry>(id));
}
namespace {
struct RescueRecord {ObjectGuid target;uint32 seen=0;};
std::map<std::pair<uint32,uint64>,RescueRecord> rescues;
std::mutex rescueMutex;
bool RescueBugAlive(PlayerbotAI* ai,Unit* bug) {
 Player* bot=ai->GetBot();
 if(!bug||!bug->IsCreature()||bug->GetEntry()==15510||bug->GetEntry()==15630||
    !bug->IsAlive()||!bug->IsInCombat()||!bug->IsInMap(bot)||!bot->IsValidAttackTarget(bug)||
    static_cast<Creature*>(bug)->IsInEvadeMode()||PossibleAttackTargetsValue::HasBreakableCC(bug,bot))return false;
 Unit* victim=bug->GetVictim();
 return victim&&victim->IsPlayer()&&victim->IsAlive()&&static_cast<Player*>(victim)->GetGroup()==bot->GetGroup();
}
bool HealerBug(PlayerbotAI* ai,Unit* bug) {
 return RescueBugAlive(ai,bug)&&ai->IsHeal(static_cast<Player*>(bug->GetVictim()));
}
}
Unit* ai::GetFankrissRescueTarget(PlayerbotAI* ai) {
 if(!FankrissSingleTargetActive(ai))return nullptr;
 Player* bot=ai->GetBot();auto key=std::make_pair(bot->GetInstanceId(),bot->GetGroup()->GetLeaderGuid().GetRawValue());
 uint32 now=WorldTimer::getMSTime();std::lock_guard<std::mutex> lock(rescueMutex);
 for(auto it=rescues.begin();it!=rescues.end();)if(uint32(now-it->second.seen)>60000)it=rescues.erase(it);else ++it;
 auto& rescue=rescues[key];rescue.seen=now;
 Unit* bug=ai->GetUnit(rescue.target);
 if(!RescueBugAlive(ai,bug)) {
  bug=nullptr;
  for(Player* member:LiveGroupMembers(bot->GetGroup())) {
   if(!member->IsAlive()||!member->IsInMap(bot)||!ai->IsHeal(member))continue;
   for(Unit* attacker:member->GetAttackers())if(HealerBug(ai,attacker))
    if(!bug||attacker->GetHealth()<bug->GetHealth()||(attacker->GetHealth()==bug->GetHealth()&&attacker->GetObjectGuid()<bug->GetObjectGuid()))bug=attacker;
  }
  rescue.target=bug?bug->GetObjectGuid():ObjectGuid();
 }
 if(!bug)return nullptr;
 std::vector<std::pair<float,ObjectGuid>> helpers;
 for(Player* member:LiveGroupMembers(bot->GetGroup())) {
  PlayerbotAI* brain=PlayerbotAIStorage::Instance().GetAI(member);
  if(!brain||!FankrissSingleTargetActive(brain)||!member->IsInMap(bot)||
     member->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL)||member->HasAura(720)||member->HasAura(731)||member->HasAura(1121)||
     member->GetDistance(bug)>20.f||!member->IsWithinLOSInMap(bug,true))continue;
  if(brain->IsMelee(member)&&!member->CanReachWithMeleeAutoAttack(bug))continue;
  if(member->GetClass()==CLASS_HUNTER&&member->GetDistance(bug)<8.f)continue;
  helpers.emplace_back(member->GetDistance(bug),member->GetObjectGuid());
 }
 std::sort(helpers.begin(),helpers.end());
 for(unsigned i=0;i<helpers.size()&&i<3;++i)if(helpers[i].second==bot->GetObjectGuid())return bug;
 return nullptr;
}
Unit* ai::GetFankrissDpsTarget(PlayerbotAI* ai) {
 if(Unit* rescue=GetFankrissRescueTarget(ai))return rescue;
 if(Unit* spawn=GetFankrissFocusSpawn(ai)) {
  Player* bot=ai->GetBot();
  bool range=ai->IsMelee(bot)?bot->CanReachWithMeleeAutoAttack(spawn):bot->GetDistance(spawn)<=ai->GetRange("spell");
  if(bot->GetClass()==CLASS_HUNTER&&bot->GetDistance(spawn)<8.f)range=false;
  if(range&&bot->IsWithinLOSInMap(spawn,true))return spawn;
 }
 if(FankrissSingleTargetActive(ai))return ai->GetAiObjectContext()->GetValue<Unit*>("fankriss active boss")->Get();
 return nullptr;
}
bool ai::FankrissDpsMayAttack(PlayerbotAI* ai,Unit* target) {
 return !FankrissSingleTargetActive(ai)||target==GetFankrissDpsTarget(ai);
}
