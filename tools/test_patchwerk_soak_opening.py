"""Compile the real Patchwerk selector/opening with a small simulated raid.

TBOTS_SOURCE_ROOT can point at the unpatched batch to reproduce the live failure.
The fixture's soaker is out of combat while the main already fights Patchwerk.
"""
import os
from pathlib import Path
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
    source = (root/'ai/playerbot/strategy/actions/PatchwerkSoakerActions.cpp').read_text()
    fixed = 'ObjectGuid PatchwerkSoakBossValue::Calculate()' in source
    code = r'''
#include <algorithm>
#include <cassert>
#include <list>
#include <map>
#include <string>
#include <type_traits>
#include <vector>
using ObjectGuid=unsigned;using uint32=unsigned;
enum class BotState {BOT_STATE_COMBAT};
constexpr int CLASS_WARRIOR=1,CLASS_DRUID=11,UNIT_STAT_MELEE_ATTACKING=1;
struct PlayerbotAI;struct Group;
struct Unit {
 unsigned entry=0,map=533,instance=1,guid=0,cls=1;float distance=30;
 bool alive=true,world=true,combat=false,teleport=false,valid=true,evade=false,friendly=true;
 bool stance=false,bear=false,melee=false,los=true,swings=false,cc=false;
 bool knowsStance=true,knowsBear=true,knowsDire=true;Group* group=nullptr;
 Unit* victim=nullptr;std::vector<Unit*> attackers;
 unsigned GetEntry(){return entry;}unsigned GetMapId(){return map;}unsigned GetObjectGuid(){return guid;}
 bool IsAlive(){return alive;}bool IsInWorld(){return world;}bool IsInCombat(){return combat;}
 bool IsBeingTeleported(){return teleport;}bool IsInMap(Unit* u){return map==u->map&&instance==u->instance;}
 bool IsCreature(){return entry!=0;}bool IsInEvadeMode(){return evade;}
 bool IsValidAttackTarget(Unit* u){return u->valid;}
 bool IsWithinDistInMap(Unit* u,float d){return IsInMap(u)&&distance<=d;}
 Group* GetGroup(){return group;}Unit* GetVictim(){return victim;}
 std::vector<Unit*> GetAttackers(){return attackers;}
 unsigned GetClass(){return cls;}bool HasSpell(unsigned id){return id==71?knowsStance:id==9634?knowsDire:knowsBear;}
 bool HasUnitState(int){return swings;}bool CanReachWithMeleeAutoAttack(Unit*){return melee;}
 bool IsWithinLOSInMap(Unit*,bool){return los;}bool IsNonMeleeSpellCasted(bool,bool,bool){return false;}
 float GetAngle(Unit*){return 0;}bool Attack(Unit* u,bool m){victim=u;swings=m;return true;}
};
using Player=Unit;using Creature=Unit;
struct Group {std::vector<Player*> members;};
std::vector<Player*> LiveGroupMembers(Group* g){return g->members;}
struct Position{bool set=false;bool isSet(){return set;}};using PositionMap=std::map<std::string,Position>;
struct Event{};
struct AiObjectContext;
template<class T>struct FixtureValue{void Reset(){}};
struct AiObjectContext {
 PlayerbotAI* ai=nullptr;PositionMap positions;std::list<ObjectGuid> attackers{1};
 ObjectGuid order=0;Unit* rti=nullptr;
 template<class T>FixtureValue<T>* GetValue(std::string,std::string=""){static FixtureValue<T> v;return &v;}
};
struct PlayerbotAI {
 Player* bot;Unit* boss;bool soak=true,main=false,tank=true,stay=false;AiObjectContext context;
 std::vector<std::string> calls;unsigned duration=0;
 Player* GetBot(){return bot;}AiObjectContext* GetAiObjectContext(){context.ai=this;return &context;}
 Unit* GetUnit(ObjectGuid id){return id==1?boss:nullptr;}
 bool IsTank(Player*){return tank;}
 bool HasStrategy(std::string s,BotState){return s=="patchwerk soak"?soak:s=="patchwerk main"?main:s=="stay"?stay:false;}
 bool HasAura(std::string s,Player* p){return s=="defensive stance"?p->stance:p->bear;}
 bool DoSpecificAction(std::string s,Event&,bool){calls.push_back(s);if(s=="defensive stance")bot->stance=true;else bot->bear=true;return true;}
 void SetActionDuration(unsigned d){duration=d;}
};
struct {float sightDistance=60;}sPlayerbotAIConfig;
struct {bool IsFriendlyTo(Unit*,Unit* u){return u->friendly;}}sServerFacade;
struct PossibleAttackTargetsValue{static bool IsValid(Unit* u,Player*,float,bool,bool){return u->valid;}};
namespace ai {
 Unit* GetPatchwerkBoss(PlayerbotAI*);Unit* GetPatchwerkSoakBoss(PlayerbotAI*);
 bool PatchwerkSoakerAssigned(PlayerbotAI*);
 struct PatchwerkSoakBossValue{PlayerbotAI* ai;Player* bot;ObjectGuid Calculate();};
}
using namespace ai;
template<class T>T value(AiObjectContext* c,std::string n){
 if constexpr(std::is_same_v<T,PositionMap&>)return c->positions;
 else if constexpr(std::is_same_v<T,std::list<ObjectGuid>>)return c->attackers;
 else if constexpr(std::is_same_v<T,ObjectGuid>){
#ifdef FIXED
  if(n=="patchwerk soak boss"){PatchwerkSoakBossValue v{c->ai,c->ai->bot};return v.Calculate();}
#endif
  return c->order;
 }else return c->rti;
}
#define AI_VALUE(T,N) value<T>(context,N)
struct PatchwerkSoakEngageAction {
 PlayerbotAI* ai;Player* bot;AiObjectContext* context;
 bool isUseful();bool Execute(Event&);
 bool isPossible(){return !bot->cc;}Player* GetMaster(){return nullptr;}
 bool Attack(Player*,Unit* u){ai->calls.push_back("attack");bot->combat=true;return bot->Attack(u,true);}
 bool ChaseTo(Unit* u,float d,float){assert(d==0);ai->calls.push_back("move");bot->Attack(u,false);return true;}
 unsigned GetDuration(){return 9000;}
};
'''
    if fixed:
        code = '#define FIXED\n' + code
    for sig in ['bool Has(', 'bool OrderedElsewhere(', 'bool ai::PatchwerkSoakerAssigned(']:
        code += function(source, sig) + '\n'
    if fixed:
        for sig in ['bool EligibleSoaker(', 'bool LivePatchwerk(', 'char const* MissingSoakStance(',
                    'ObjectGuid PatchwerkSoakBossValue::Calculate()', 'Unit* ai::GetPatchwerkSoakBoss(',
                    'bool PatchwerkSoakEngageAction::isUseful()', 'bool PatchwerkSoakEngageAction::Execute(']:
            code += function(source, sig) + '\n'
    code += function(source, 'Unit* ai::GetPatchwerkBoss(') + '\n'
    code += r'''
int main(int argc, char**){
 Player bot,main,human;Unit boss;boss.entry=16028;boss.guid=1;boss.combat=true;
 Group group{{&bot,&main,&human}};bot.group=&group;main.combat=true;main.victim=&boss;
 PlayerbotAI ai{&bot,&boss};ai.GetAiObjectContext();ai.stay=argc>1;
 // Live 21:13: main fights, soaker at raid anchor outside combat. No stay.
 assert(GetPatchwerkBoss(&ai)==&boss);
 // Live 16:33: same opening while the player's stay order remains stored.
 ai.stay=true;assert(GetPatchwerkBoss(&ai)==&boss);
#ifdef FIXED
 Event event;PatchwerkSoakEngageAction action{&ai,&bot,&ai.context};
 assert(action.isUseful());assert(action.Execute(event));
 assert(ai.calls==std::vector<std::string>{"defensive stance"});assert(!bot.victim);
 assert(action.Execute(event));assert((ai.calls==std::vector<std::string>{"defensive stance","attack","move"}));
 assert(bot.victim==&boss&&bot.swings&&ai.stay);assert(ai.duration==1000);
 bot.melee=true;assert(!action.isUseful());
 bot.melee=false;assert(action.isUseful()); // reacquire after displacement
 bot.cc=true;assert(!action.Execute(event));bot.cc=false;
 // The human body-pull path works before any bot has attacked.
 bot.combat=false;bot.victim=nullptr;main.combat=false;main.victim=nullptr;
 human.attackers={&boss};assert(GetPatchwerkSoakBoss(&ai)==&boss);
 human.attackers.clear();assert(!GetPatchwerkSoakBoss(&ai)); // no fresh pull
 human.attackers={&boss};boss.combat=false;assert(!GetPatchwerkSoakBoss(&ai));boss.combat=true;
 boss.alive=false;assert(!GetPatchwerkSoakBoss(&ai));boss.alive=true;
 boss.evade=true;assert(!GetPatchwerkSoakBoss(&ai));boss.evade=false;
 ai.soak=false;assert(!GetPatchwerkSoakBoss(&ai));ai.soak=true;
 ai.main=true;assert(!GetPatchwerkSoakBoss(&ai));ai.main=false;
 ai.tank=false;assert(!GetPatchwerkSoakBoss(&ai));ai.tank=true;
 bot.map=0;assert(!GetPatchwerkSoakBoss(&ai));bot.map=533;
 bot.instance=2;assert(!GetPatchwerkSoakBoss(&ai));bot.instance=1;
 boss.entry=15990;assert(!GetPatchwerkSoakBoss(&ai));boss.entry=16028;
 bot.distance=100;assert(!GetPatchwerkSoakBoss(&ai));bot.distance=30;
 bot.alive=false;assert(!GetPatchwerkSoakBoss(&ai));bot.alive=true;
 bot.teleport=true;assert(!GetPatchwerkSoakBoss(&ai));bot.teleport=false;
 // Bear tanks prepare form; a paladin needs no warrior stance.
 bot.cls=CLASS_DRUID;ai.calls.clear();assert(action.Execute(event));
 assert(ai.calls==std::vector<std::string>{"dire bear form"});
 bot.bear=false;bot.knowsDire=false;assert(action.Execute(event));assert(ai.calls.back()=="bear form");
 bot.cls=2;ai.calls.clear();assert(action.Execute(event));assert(ai.calls.front()=="attack");
 assert(ai.stay); // no destructive ChangeStrategy: the saved order survives
#endif
}
'''
    return code


class PatchwerkSoakOpeningTest(unittest.TestCase):
    def test_real_group_selector_and_stance_attack_movement(self):
        root = Path(os.environ.get('TBOTS_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
        compiler = shutil.which('g++')
        if not compiler:
            self.skipTest('g++ unavailable')
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)/'opening.cpp'
            exe = Path(directory)/'opening'
            source.write_text(fixture(root))
            subprocess.run([compiler, '-std=c++17', str(source), '-o', str(exe)], check=True)
            for args in ([], ['stay']):
                with self.subTest(stay=bool(args)):
                    subprocess.run([str(exe), *args], check=True)

    def test_production_update_wakes_once_and_retains_stance_delay(self):
        root = Path(os.environ.get('TBOTS_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
        source = (root/'ai/playerbot/PlayerbotAI.cpp').read_text()
        update = function(source, 'void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)')
        opening = update[update.index('bool const soakActive'):update.index('bool doMinimalReaction')]
        code = r"""
#include <cassert>
#include <string>
using ObjectGuid=unsigned;
struct Event{Event(char const*){}};
struct Bot{int interrupted=0;void InterruptNonMeleeSpells(bool){++interrupted;}};
struct PlayerbotAI;
struct Config {bool hasLog(char const*){return false;}void logEvent(PlayerbotAI*,char const*,ObjectGuid,char const*){}}sPlayerbotAIConfig;
struct PlayerbotAI{
 Bot unit;Bot* bot=&unit;bool eligible=true,active=false,possible=true;unsigned delay=20000,stage=0,normal=0;
 unsigned GetAIInternalUpdateDelay(){return delay;}void SetAIInternalUpdateDelay(unsigned d){delay=d;}
 bool CanUpdateAIInternal(){return delay<100;}void ResetAIInternalUpdateDelay(){delay=0;}
 void Tick(unsigned elapsed);
};
Bot* GetPatchwerkSoakBoss(PlayerbotAI* ai){return ai->eligible?ai->bot:nullptr;}
struct PatchwerkSoakEngageAction{
 PlayerbotAI* ai;PatchwerkSoakEngageAction(PlayerbotAI* ai):ai(ai){}
 bool isUseful(){return ai->stage<2;}bool isPossible(){return ai->possible;}
 bool Execute(Event&){if(ai->stage++==0)ai->delay=1500;return true;}
};
#define AI_VALUE2(T,N,Q) active
#define SET_AI_VALUE2(T,N,Q,V) active=V
void PlayerbotAI::Tick(unsigned elapsed){
 delay=delay>elapsed?delay-elapsed:0;
""" + opening + r"""
 ++normal;
}
int main(){
 PlayerbotAI ai;
 ai.Tick(10);assert(ai.stage==1&&ai.delay==1500&&ai.unit.interrupted==1&&ai.normal==0);
 ai.Tick(100);assert(ai.stage==1&&ai.delay==1400&&ai.unit.interrupted==1);
 ai.Tick(1400);assert(ai.stage==2&&ai.delay==250&&ai.unit.interrupted==1);
 ai.Tick(250);assert(ai.normal==1); // normal tank rotation after arrival
 ai.eligible=false;ai.Tick(1);assert(!ai.active&&ai.normal==2); // dead/evaded/opted out
 ai.eligible=true;ai.stage=0;ai.delay=20000;ai.Tick(1);assert(ai.stage==1&&ai.unit.interrupted==2);
 PlayerbotAI dps;dps.eligible=false;dps.Tick(10);assert(dps.stage==0&&dps.delay==19990&&dps.normal==1);
 PlayerbotAI stunned;stunned.possible=false;stunned.Tick(10);assert(stunned.stage==0);
}
"""
        compiler = shutil.which('g++')
        if not compiler:
            self.skipTest('g++ unavailable')
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)/'wake.cpp'
            exe = Path(directory)/'wake'
            source.write_text(code)
            subprocess.run([compiler, '-std=c++17', str(source), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_wake_precedes_internal_delay_and_preserves_orders(self):
        root = Path(os.environ.get('TBOTS_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
        source = (root/'ai/playerbot/PlayerbotAI.cpp').read_text()
        update = function(source, 'void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)')
        self.assertIn('PatchwerkSoakEngageAction opening(this)', update)
        opening = update[update.index('bool const soakActive'):update.index('bool doMinimalReaction')]
        self.assertIn('if (!soakWasActive)', opening)
        self.assertIn('ResetAIInternalUpdateDelay()', opening)
        self.assertIn('CanUpdateAIInternal() && opening.isPossible()', opening)
        self.assertNotIn('ChangeStrategy', opening)
        self.assertLess(update.index('opening.Execute(event)'), update.index('UpdateAIReaction(elapsed'))
        strategies = (root/'ai/playerbot/strategy/generic/NaxxramasDungeonStrategies.cpp').read_text()
        self.assertIn('name == "return to stay position"', strategies)
        wait = (root/'ai/playerbot/strategy/generic/CombatStrategy.cpp').read_text()
        self.assertIn('if (GetPatchwerkSoakBoss(ai))\n        return false;', wait)


if __name__ == '__main__':
    unittest.main()
