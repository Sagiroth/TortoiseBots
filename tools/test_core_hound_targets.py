"""Production target methods with a double of Turtle's scripted collapse.

TBOTS_TEST_SOURCE selects pre-fix source for the same failing regression.
Core state/context are doubles; this does not execute the encounter scheduler.
"""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class CoreHoundTargets(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_production_target_paths(self):
        root = pathlib.Path(os.environ.get('TBOTS_TEST_SOURCE', pathlib.Path(__file__).resolve().parents[1]))
        values = root/'ai/playerbot/strategy/values'
        code = r'''
#include <algorithm>
#include <iostream>
#include <list>
#include <map>
#include <sstream>
#include <string>
#include <type_traits>
using uint8 = unsigned char;
constexpr int UNIT_DYNAMIC_FLAGS=1, UNIT_DYNFLAG_DEAD=2, UNIT_FIELD_TARGET=3;
struct ObjectGuid {
 unsigned id=0; ObjectGuid(unsigned i=0):id(i){} operator bool()const{return id!=0;}
 bool IsEmpty()const{return !id;} bool IsPlayer()const{return false;}
 bool operator==(ObjectGuid b)const{return id==b.id;} bool operator!=(ObjectGuid b)const{return id!=b.id;}
 bool operator<(ObjectGuid b)const{return id<b.id;}
};
struct Unit;
struct ThreatManager { Unit* victim=nullptr; Unit* getCurrentVictim(){return victim;} float getThreat(Unit*){return 1;} };
struct Unit {
 virtual ~Unit()=default;
 unsigned map=409,entry=11671,hp=31489; bool alive=true,dynamicDead=false,world=true;
 ObjectGuid guid{1}; ThreatManager threat;
 virtual bool IsCreature()const{return false;} bool IsPlayer()const{return !IsCreature();}
 unsigned GetMapId()const{return map;} unsigned GetEntry()const{return entry;}
 unsigned GetHealth()const{return hp;} bool IsAlive()const{return alive;}
 bool HasFlag(int field,int flag)const{return field==UNIT_DYNAMIC_FLAGS && flag==UNIT_DYNFLAG_DEAD && dynamicDead;}
 bool IsInWorld(){return world;} ObjectGuid getObjectGuid(){return guid;} ObjectGuid GetObjectGuid(){return guid;}
 Unit* GetVictim(){return threat.victim;} ThreatManager& GetThreatManager(){return threat;}
 bool IsVisibleForOrDetect(Unit*,Unit*,bool){return true;} ObjectGuid GetGuidValue(int){return guid;}
 const char* GetName(){return "fixture hound";}
};
struct Creature:Unit { bool IsCreature()const override{return true;} bool IsInEvadeMode(){return false;} bool IsEvadeBecauseTargetNotReachable(){return false;} };
struct Group { ObjectGuid mark; ObjectGuid GetTargetIcon(uint8){return mark;} };
struct Player:Unit {
 bool los=true;bool IsWithinLOSInMap(Unit*){return los;}
 Group* group=nullptr;ObjectGuid selection{1};Group* GetGroup(){return group;}
 ObjectGuid GetSelectionGuid(){return selection;} bool IsWithinDistInMap(Unit*,float,bool=false){return true;}
 bool IsValidAttackTarget(Unit* u){return u && u->alive;} bool IsInCombat(){return true;}
 struct Camera{Unit* GetBody(){return nullptr;}} camera;Camera& GetCamera(){return camera;}
};
struct Context {
 std::map<std::string,Unit*> units;std::map<std::string,ObjectGuid> guids;
 std::list<ObjectGuid> cached{1,2};
 template<class T>T Read(std::string const& key){
  if constexpr(std::is_same_v<T,Unit*>)return units[key];
  else if constexpr(std::is_same_v<T,ObjectGuid>)return guids[key];
  else if constexpr(std::is_same_v<T,std::string>)return "skull";
  else return cached;
 }
 template<class T>struct Value{Context* context;std::string key;T Get(){return context->Read<T>(key);}};
 template<class T>Value<T>* GetValue(std::string const& key){static Value<T> v;v={this,key};return &v;}
};
struct PlayerbotAI {
 Player* bot;Context context;std::map<ObjectGuid,Unit*> units;
 Unit* GetUnit(ObjectGuid id){return units[id];}Context* GetAiObjectContext(){return &context;}
 Player* GetBot(){return bot;}bool HasStrategy(std::string,int){return false;}bool IsTank(Player*){return false;}
 bool HasActivePlayerMaster(){return true;}void TellPlayerNoFacing(Player*,std::string){}
};
struct PlayerbotAIStorage {PlayerbotAI* ai=nullptr;static PlayerbotAIStorage& Instance(){static PlayerbotAIStorage s;return s;}PlayerbotAI* GetAI(Player*){return ai;}};
#define AI_VALUE(T,K) ai->context.Read<T>(K)
#define PAI_VALUE(T,K) PlayerbotAIStorage::Instance().ai->context.Read<T>(K)
struct {float sightDistance=60,reactDistance=100;} sPlayerbotAIConfig;
struct Facade {
 bool UnitIsDead(Unit* u){return !u->alive;}bool IsAlive(Unit* u){return u->alive;}
 bool IsFriendlyTo(Unit*,Unit*){return false;}float getDistance2d(Unit*,Unit*){return 5;}
 ThreatManager& GetThreatManager(Unit* u){return u->threat;}
} sServerFacade;
namespace BotState{constexpr int BOT_STATE_COMBAT=1;}
struct CanFreeMoveValue{static bool CanFreeAttack(PlayerbotAI*,Unit*){return true;}};
struct AttackersValue{static bool IsValid(Unit* u,Player*){return u && u->GetVictim();}};
bool IsImmuneToDamage(Unit*,Player*){return false;}bool HasIgnoreCCRti(Unit*,Player*){return false;}
bool HasBreakableCC(Unit*,Player*){return false;}bool HasUnBreakableCC(Unit*,Player*){return false;}
bool IsTapped(Unit*,Player*){return true;}
bool ShouldDropMaexxnaRescueTarget(PlayerbotAI*,Unit*){return false;}bool IsMaexxnaRescueTarget(PlayerbotAI*,Unit*){return false;}
bool IsRaidCrowdControlMarkedTarget(PlayerbotAI*,Unit*){return false;}bool IsRaidDpsTargetExcluded(PlayerbotAI*,Unit*){return false;}
template<class F>bool CanUseViscidusGlobTarget(unsigned,bool,bool,bool,bool,unsigned,F){return false;}
struct PossibleTargetsValue {
 static bool IsValid(Unit*,Player*,bool=false,char const** =nullptr);
 static bool IsFriendly(Unit*,Player*){return false;}static bool IsAttackable(Unit*,Player*){return true;}
};
struct PossibleAttackTargetsValue {
 static bool IsValid(Unit*,Player*,float,bool=false,bool=true);
 static bool IsPossibleTarget(Unit*,Player*,float,bool=false);
};
bool IsOnyxiaAirCombatTarget(PlayerbotAI*,Unit*){return false;}
struct InvalidTargetValue{PlayerbotAI* ai;Player* bot;std::string qualifier="current target";bool Calculate();};
struct FindTargetStrategy {Unit* result=nullptr;virtual ~FindTargetStrategy()=default;virtual void CheckAttacker(Unit*,ThreatManager*)=0;Unit* GetResult(){return result;}};
struct FindNonCcTargetStrategy:FindTargetStrategy {PlayerbotAI* ai;FindNonCcTargetStrategy(PlayerbotAI* a):ai(a){}static bool IsCastingCcAt(Player*,Unit*){return false;}bool IsCcTarget(Unit*){return false;}};
struct TargetValue {PlayerbotAI* ai;Player* bot;Unit* FindTarget(FindTargetStrategy*);Unit* GetExplicitAttackTarget();};
struct RtiTargetValue {PlayerbotAI* ai;Player* bot;Context* context;std::string type="rti";static int GetRtiIndex(std::string r){return r=="skull"?7:-1;}Unit* Calculate();};
struct CcGuard{CcGuard(PlayerbotAI*){}bool IsHeldCcMark(Unit*){return false;}};
struct AttackAction {PlayerbotAI* ai;Player* bot;bool verbose=true;bool IsTargetValid(Player*,Unit*,float=60);};
'''
        policy = values/'CoreHoundTargetPolicy.h'
        if policy.exists():
            code += function(policy.read_text(), 'inline bool IsCollapsedCoreHound(')
        for filename, signature in [
            ('PossibleTargetsValue.cpp','bool PossibleTargetsValue::IsValid('),
            ('PossibleAttackTargetsValue.cpp','bool PossibleAttackTargetsValue::IsPossibleTarget('),
            ('PossibleAttackTargetsValue.cpp','bool PossibleAttackTargetsValue::IsValid('),
            ('InvalidTargetValue.cpp','bool InvalidTargetValue::Calculate('),
            ('TargetValue.cpp','Unit* TargetValue::FindTarget('),
            ('TargetValue.cpp','Unit* TargetValue::GetExplicitAttackTarget('),
        ]:
            code += '\n'+function((values/filename).read_text(), signature)
        code += '\n'+function((values/'RtiTargetValue.h').read_text(), 'Unit *Calculate() override').replace('Unit *Calculate() override', 'Unit* RtiTargetValue::Calculate()')
        code += '\n'+function((values/'LeastHpTargetValue.h').read_text(), 'class FindLeastHpTargetStrategy')+';'
        code += '\n'+function((root/'ai/playerbot/strategy/actions/AttackAction.cpp').read_text(), 'bool AttackAction::IsTargetValid(')
        code += r'''
int main(){
 int failures=0;auto check=[&](bool ok,const char* label){if(!ok){std::cerr<<"FAIL: "<<label<<"\n";++failures;}};
 Player bot;Creature collapsed,live;live.guid=2;live.hp=500;
 // Turtle's FeignDeath does not change engine death state, victim or threat.
 collapsed.hp=0;collapsed.dynamicDead=true;collapsed.threat.victim=&bot;live.threat.victim=&bot;
 PlayerbotAI ai{&bot};ai.units={{1,&collapsed},{2,&live}};PlayerbotAIStorage::Instance().ai=&ai;
 ai.context.units["current target"]=&collapsed;ai.context.guids["attack target"]=1;ai.context.guids["explicit attack target"]=1;
 InvalidTargetValue held{&ai,&bot};TargetValue targets{&ai,&bot};AttackAction attack{&ai,&bot};
 check(collapsed.IsAlive() && collapsed.GetHealth()==0,"fixture retains core ALIVE at HP0");
 check(held.Calculate(),"held explicit target drops despite stale attacker cache");
 char const* reason=nullptr;
 check(!PossibleTargetsValue::IsValid(&collapsed,&bot,false,&reason),"fresh nearby scan rejects collapse");
 check(!PossibleAttackTargetsValue::IsValid(&collapsed,&bot,60),"retained threat does not vouch for collapse");
 check(!targets.GetExplicitAttackTarget(),"explicit target cannot reselect collapse");
 check(!attack.IsTargetValid(&bot,&collapsed),"cached/direct attack rejects collapse");
 FindLeastHpTargetStrategy least(&ai);check(targets.FindTarget(&least)==&live,"stale cache picks live hound instead of zero HP");
 Group group;group.mark=1;bot.group=&group;RtiTargetValue marked{&ai,&bot,&ai.context};
 check(!marked.Calculate(),"skull in stale cache cannot force collapsed hound");
 // With the whole cached pack collapsed there is no usable target.
 bot.group=nullptr;live.hp=0;live.dynamicDead=true;
 FindLeastHpTargetStrategy none(&ai);check(!targets.FindTarget(&none),"all collapsed means no target");
 live.hp=500;live.dynamicDead=false;bot.group=&group;
 // Same GUID revives; no cache refresh or player command needed.
 collapsed.hp=31489;collapsed.dynamicDead=false;
 check(!held.Calculate(),"revived held target valid");
 check(PossibleTargetsValue::IsValid(&collapsed,&bot),"revived fresh scan valid");
 check(PossibleAttackTargetsValue::IsValid(&collapsed,&bot,60),"revived attacker valid");
 check(targets.GetExplicitAttackTarget()==&collapsed,"revived explicit target valid");
 check(attack.IsTargetValid(&bot,&collapsed),"revived attack valid");
 check(marked.Calculate()==&collapsed,"revived skull valid");
 bot.los=false;check(!marked.Calculate(),"upstream marked-target LOS gate retained");bot.los=true;
 bot.group=nullptr;ai.context.cached={1};FindLeastHpTargetStrategy revived(&ai);
 check(targets.FindTarget(&revived)==&collapsed,"revived cached GUID eligible");
 // Scope boundaries retain old eligibility, even at HP0 with the dynamic flag.
 collapsed.hp=0;collapsed.dynamicDead=true;
 for(unsigned entry:{11672u,11673u,123u}){collapsed.entry=entry;check(!held.Calculate(),"other creature unchanged");check(PossibleTargetsValue::IsValid(&collapsed,&bot),"other creature scan unchanged");}
 collapsed.entry=11671;collapsed.map=1;bot.map=1;check(!held.Calculate(),"outside MC unchanged");
 collapsed.map=409;bot.map=409;collapsed.dynamicDead=false;check(!held.Calculate(),"zero HP without scripted flag unchanged");
 collapsed.dynamicDead=true;collapsed.hp=1;check(!held.Calculate(),"positive HP unchanged");
 Player player;player.hp=0;player.dynamicDead=true;player.threat.victim=&bot;ai.context.units["current target"]=&player;
 check(!held.Calculate(),"player feign state unchanged");
 check(!attack.IsTargetValid(&bot,nullptr),"null attack still rejected");
 std::cout<<"Core Hound production-method fixture: "<<failures<<" failures\n";return failures?1:0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source = pathlib.Path(tmp)/'fixture.cpp'
            binary = pathlib.Path(tmp)/'fixture'
            source.write_text(code)
            subprocess.run(['g++','-std=c++17',str(source),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)


if __name__ == '__main__':
    unittest.main()
