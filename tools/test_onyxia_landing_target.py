"""Production landing assignment and tank-selector caller, with engine stubs."""
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


def fixture(root):
    source = (root/'ai/playerbot/strategy/actions/OnyxiasLairDungeonActions.cpp').read_text()
    caller_root = pathlib.Path(os.environ.get('TBOTS_CALLER_SOURCE', root))
    tank = (caller_root/'ai/playerbot/strategy/values/TankTargetValue.cpp').read_text()
    code = '#include "' + str(root / 'ai/playerbot/MainTankPolicy.h') + '"\n' + r'''
#include <cassert>
#include <list>
#include <map>
#include <string>
#include <type_traits>
using ObjectGuid=unsigned;
enum class BotState{BOT_STATE_COMBAT};
struct PlayerbotAI;
struct Unit {
 Unit* victim=nullptr;PlayerbotAI* controller=nullptr;
 Unit* GetVictim(){return victim;}
 unsigned entry=10184,map=249,instance=1;float hp=39;
 bool world=true,alive=true,combat=true,teleport=false,flying=false,aura=false,valid=true;
 unsigned GetEntry(){return entry;}unsigned GetMapId(){return map;}
 bool IsAlive(){return alive;}bool IsInWorld(){return world;}bool IsInCombat(){return combat;}
 bool IsBeingTeleported(){return teleport;}bool IsFlying(){return flying;}
 bool HasAura(unsigned){return aura;}float GetHealthPercent(){return hp;}
 bool IsInMap(Unit* u){return map==u->map&&instance==u->instance;}
};
using Player=Unit;
struct Position{bool set=false;bool isSet(){return set;}};
using PositionMap=std::map<std::string,Position>;
template<class T>struct FixtureValue{T data{};T Get(){return data;}};
struct AiObjectContext{FixtureValue<std::string> rtiSetting;
 template<class T>FixtureValue<T>* GetValue(std::string){return &rtiSetting;}PositionMap positions;std::list<ObjectGuid> attackers{1};ObjectGuid order=0;Unit* rti=nullptr;Unit* current=nullptr;};
struct PlayerbotAI{
 Player* bot;Unit* boss;Unit* ordered=nullptr;bool enabled=true,tank=true;std::string hold;
 AiObjectContext context;
 Player* GetBot(){return bot;}AiObjectContext* GetAiObjectContext(){return &context;}
 Unit* GetUnit(ObjectGuid g){return g==1?boss:g==2?ordered:nullptr;}
 bool IsTank(Player*){return tank;}
 bool HasStrategy(std::string s,BotState){return s=="onyxia landing tank"?enabled:s==hold;}
};
struct PlayerbotAIStorage{
 static PlayerbotAIStorage& Instance(){static PlayerbotAIStorage storage;return storage;}
 PlayerbotAI* GetAI(Player* p){return p->controller;}
};
template<class T>T value(AiObjectContext* c,std::string n){
 if constexpr(std::is_same_v<T,PositionMap&>)return c->positions;
 else if constexpr(std::is_same_v<T,std::list<ObjectGuid>>)return c->attackers;
 else if constexpr(std::is_same_v<T,ObjectGuid>)return c->order;
 else return n=="rti target"?c->rti:c->current;
}
#define AI_VALUE(T,N) value<T>(context,N)
struct {float sightDistance=60;}sPlayerbotAIConfig;
struct PossibleAttackTargetsValue{static bool IsValid(Unit* u,Player*,float,bool,bool){return u->valid;}};
namespace ai {Unit* GetAssignedOnyxiaLandingTarget(PlayerbotAI*);}
using namespace ai;
struct OnyxiaLandingAttackAction{
 PlayerbotAI* ai;AiObjectContext* context;Unit* GetTarget(){return GetAssignedOnyxiaLandingTarget(ai);}bool isUseful();
};
// The existing fallback represents its documented nearest-loose-add result.
// The test verifies the production Calculate caller actually overrides it.
Unit* FourHorsemenRotationTarget(PlayerbotAI*,Player*){return nullptr;}
Unit* looseWhelp=nullptr;
Unit* markedTarget=nullptr;
struct RtiTargetValue{Unit* Calculate(){return markedTarget;}};
// Garr assignment is inactive in this Onyxia fixture.
Unit* GetAssignedGarrTankTarget(PlayerbotAI*){return nullptr;}
Unit* GetAssignedPatchwerkTankTarget(PlayerbotAI*){return nullptr;}
Unit* GetAssignedFankrissTankTarget(PlayerbotAI*){return nullptr;}
Unit* GetOnyxiaWhelpTarget(PlayerbotAI*){return nullptr;}
bool IsNefarianAddTankOptedIn(PlayerbotAI*){return false;}
using NefarianAddAssignment=int;
Unit* GetAssignedNefarianAddTarget(PlayerbotAI*,int){return nullptr;}
struct AttackersValue {static bool IsKeepoutSetAside(Unit*, Player*){return false;}};
bool IsExplicitMainTank(Player*){return false;}
unsigned GroupTankCount(PlayerbotAI*,Player*){return 1;}
struct FindTankTargetSmartStrategy{FindTankTargetSmartStrategy(PlayerbotAI*){}};
struct TankTargetValue:RtiTargetValue{
 Unit* FindLastCcTarget(){return nullptr;}
 Player* bot;
 PlayerbotAI* ai;AiObjectContext* context;
 Unit* GetExplicitAttackTarget(){return nullptr;}
 Unit* FindTarget(FindTankTargetSmartStrategy*){return looseWhelp;}
 Unit* Calculate();
};
'''
    code += function(source,'Unit* ai::GetAssignedOnyxiaLandingTarget(')
    code += '\n'+function(source,'bool OnyxiaLandingAttackAction::isUseful()')
    code += '\n'+function(tank,'Unit* TankTargetValue::Calculate()')
    code += r'''
int main(){
 Player bot;Unit boss,whelp;whelp.entry=11262;looseWhelp=&whelp;
 PlayerbotAI ai{&bot,&boss};TankTargetValue selector;selector.bot=&bot;selector.ai=&ai;selector.context=&ai.context;
 OnyxiaLandingAttackAction action{&ai,&ai.context};ai.context.current=&whelp;
 // Exercise upstream conditional RTI with both lookup paths returning the same mark.
 // Landing is disabled for these cases so falling through reaches the loose add.
 Unit marked;marked.entry=999;markedTarget=&marked;ai.context.rti=&marked;ai.enabled=false;
 assert(selector.Calculate()==&marked);
 marked.victim=&bot;assert(selector.Calculate()==&whelp);
 Player otherTank;PlayerbotAI otherAi{&otherTank,&boss};otherTank.controller=&otherAi;
 marked.victim=&otherTank;assert(selector.Calculate()==&whelp);
 otherAi.context.rtiSetting.data="cross";assert(selector.Calculate()==&marked);
 otherAi.context.rtiSetting.data="";ai.tank=false;assert(selector.Calculate()==&marked);ai.tank=true;
 markedTarget=nullptr;ai.context.rti=nullptr;ai.enabled=true;
 assert(selector.Calculate()==&boss);assert(action.isUseful());
 ai.context.current=&boss;assert(!action.isUseful());ai.context.current=&whelp;
 boss.flying=true;assert(selector.Calculate()==&whelp);boss.flying=false;
 boss.aura=true;assert(selector.Calculate()==&whelp);boss.aura=false;
 boss.hp=100;assert(selector.Calculate()==&boss);boss.hp=39; // fixed spot/assignment also covers phase one
 ai.enabled=false;assert(selector.Calculate()==&whelp);ai.enabled=true;
 ai.tank=false;assert(selector.Calculate()==&whelp);ai.tank=true;
 bot.map=1;assert(selector.Calculate()==&whelp);bot.map=249;
 boss.instance=2;assert(selector.Calculate()==&whelp);boss.instance=1;
 boss.alive=false;assert(selector.Calculate()==&whelp);boss.alive=true;
 boss.combat=false;assert(selector.Calculate()==&whelp);boss.combat=true;
 boss.valid=false;assert(selector.Calculate()==&whelp);boss.valid=true;
 ai.context.order=2;ai.ordered=&whelp;assert(GetAssignedOnyxiaLandingTarget(&ai)==nullptr);ai.context.order=0;
 ai.context.rti=&whelp;assert(GetAssignedOnyxiaLandingTarget(&ai)==nullptr);ai.context.rti=nullptr;
 for(auto hold:{"stay","follow","wait for attack","grind"}){
  ai.hold=hold;assert(selector.Calculate()==&whelp);
 }ai.hold="";
 ai.context.positions["pull hold"].set=true;assert(selector.Calculate()==&whelp);
 ai.context.positions.clear();ai.context.attackers.clear();assert(selector.Calculate()==&whelp);
 assert(GetAssignedOnyxiaLandingTarget(nullptr)==nullptr);
}
'''
    # The unrelated Nefarian branch is dead in this fixture. Its value has a
    # different type than the modeled context values; replace just that lookup.
    code = code.replace('AI_VALUE(NefarianAddAssignment, "nefarian add assignment")','0')
    return code


class OnyxiaLandingTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_assignment_and_caller(self):
        root=pathlib.Path(os.environ.get('TBOTS_TEST_SOURCE',pathlib.Path(__file__).resolve().parents[1]))
        with tempfile.TemporaryDirectory() as tmp:
            cpp,exe=pathlib.Path(tmp)/'landing.cpp',pathlib.Path(tmp)/'landing'
            cpp.write_text(fixture(root))
            subprocess.run(['g++','-std=c++17',str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)


if __name__=='__main__':unittest.main()
