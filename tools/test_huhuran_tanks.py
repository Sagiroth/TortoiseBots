"""Run production Huhuran eligibility/actions and opening-delay gates with core doubles."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(os.environ.get('HUHURAN_SOURCE', Path(__file__).resolve().parents[1]))


def body(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def run(code):
    assert shutil.which('g++'), 'C++ compiler required'
    with tempfile.TemporaryDirectory() as tmp:
        src, exe = Path(tmp)/'test.cpp', Path(tmp)/'test'
        src.write_text(code)
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-variable', '-Wno-unused-parameter', '-I', str(ROOT),
                        str(src), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


class HuhuranTanks(unittest.TestCase):
    def test_production_role_and_handoffs(self):
        path = ROOT/'ai/playerbot/strategy/actions/HuhuranActions.cpp'
        self.assertTrue(path.exists(), 'baseline has no opt-in Huhuran role')
        text = path.read_text()
        cpp = r'''
#include <cassert>
#include <list>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <cmath>
#include "ai/playerbot/EbonrocSwapPolicy.h"
#include "ai/playerbot/HuhuranPolicy.h"
using namespace ai;
constexpr int CLASS_WARRIOR=1, CLASS_DRUID=11, UNIT_STAT_MELEE_ATTACKING=1;
namespace BotState {constexpr int BOT_STATE_COMBAT=0, BOT_STATE_NON_COMBAT=1;}
using ObjectGuid=unsigned;
struct Group;
struct Player;
struct Unit {
 virtual ~Unit()=default;
 unsigned entry=15509,map=531,instance=1;bool alive=true,world=true,combat=true,player=false,valid=true;
 Unit* victim=nullptr;float x=0;
 bool IsAlive(){return alive;}bool IsInWorld(){return world;}bool IsInCombat(){return combat;}
 bool IsInMap(Unit* u){return map==u->map && instance==u->instance;}
 bool IsCreature(){return !player;}bool IsPlayer(){return player;}
 unsigned GetEntry(){return entry;}Unit* GetVictim(){return victim;}
 float GetDistance2dToCenter(Unit* u){return std::fabs(x-u->x);}
};
struct Creature:Unit {bool evade=false;bool IsInEvadeMode(){return evade;}};
struct Player:Unit {
 Player(){player=true;entry=0;}
 Group* group=nullptr;bool tank=true,ranged=false,heal=false,teleport=false,melee=false,casting=false,channel=false;
 int cls=1;std::set<unsigned> spells{71};
 Group* GetGroup(){return group;}unsigned GetMapId(){return map;}
 bool IsBeingTeleported(){return teleport;}bool IsValidAttackTarget(Unit* u){return u->valid;}
 int GetClass(){return cls;}bool HasSpell(unsigned id){return spells.count(id);}
 bool HasUnitState(int){return melee;}
 bool IsNonMeleeSpellCasted(bool,bool skipChannel,bool){return casting || (channel && !skipChannel);}
};
struct Group {std::vector<Player*> members;};
struct AiObjectContext {};
struct Event {};
namespace ai {
struct PlayerbotAI {
 explicit PlayerbotAI(Player* p):bot(p){}
 Player* bot;Unit* current=nullptr;Unit* ordered=nullptr;Unit* mark=nullptr;
 std::set<std::string> co,nc,auras;std::list<ObjectGuid> attackers{1};
 std::string stance;Unit* attacked=nullptr;bool possible=true;
 Player* GetBot(){return bot;}static bool IsTank(Player* p){return p->tank;}
 bool IsRanged(Player* p){return p->ranged;}bool IsHeal(Player* p){return p->heal;}
 bool HasStrategy(char const* s,int state){return (state==0?co:nc).count(s);}
 bool HasAura(char const* s,Player*){return auras.count(s);}
 AiObjectContext* GetAiObjectContext(){return nullptr;}
 Unit* GetUnit(ObjectGuid);
 bool DoSpecificAction(char const* s,Event&,bool){stance=s;return true;}
 template<class T>T value(std::string key){
  if constexpr(std::is_same_v<T,std::list<ObjectGuid>>)return attackers;
  else if constexpr(std::is_same_v<T,ObjectGuid>)return ordered?2:0;
  else return key=="rti target"?mark:current;
 }
};
Unit* GetAssignedHuhuranTankTarget(PlayerbotAI*);
}
Creature boss,add;
Unit* PlayerbotAI::GetUnit(ObjectGuid id){return id==1?&boss:id==2?ordered:nullptr;}
struct PossibleAttackTargetsValue {static bool IsValid(Unit* u,Player*,float,bool,bool){return u->valid;}};
struct {float sightDistance=80;} sPlayerbotAIConfig;
#define AI_VALUE(type,key) ai->value<type>(key)
struct PlayerbotAIStorage {
 std::map<Player*,PlayerbotAI*> ais;
 static PlayerbotAIStorage& Instance(){static PlayerbotAIStorage s;return s;}
 PlayerbotAI* GetAI(Player* p){return ais.count(p)?ais.at(p):nullptr;}
};
auto LiveGroupMembers(Group* g){return g->members;}
struct Action {
 PlayerbotAI* ai;Player* bot;std::string name;Unit* target=nullptr;
 Action(PlayerbotAI* a,std::string n=""):ai(a),bot(a->bot),name(n){}
 std::string getName(){return name;}Unit* GetTarget(){return target;}
};
struct Multiplier:Action {using Action::Action;virtual float GetValue(Action*)=0;};
struct MovementAction:Action {using Action::Action;bool isPossible(){return ai->possible;}};
struct HuhuranTankEngageAction:Action {
 using Action::Action;bool isUseful();bool Execute(Event&);
 bool isPossible(){return ai->possible;}Player* GetMaster(){return nullptr;}
 bool Attack(Player*,Unit* u){ai->attacked=u;return true;}
};
struct HuhuranRangedAction:MovementAction {using MovementAction::MovementAction;bool isUseful();};
'''
        for signature in ['bool Has(', 'Unit* HuhuranBoss(', 'bool OrderedElsewhere(', 'bool Parked(',
                          'char const* MissingStance(', 'Unit* RangedBoss(', 'float RangedRadius(']:
            cpp += body(text, signature) + '\n'
        for name in ['class HuhuranTauntMultiplier', 'class HuhuranRangedMultiplier']:
            cpp += body(text, name) + ';\n'
        for signature in ['Unit* ai::GetAssignedHuhuranTankTarget(', 'bool HuhuranTankEngageAction::isUseful()',
                          'bool HuhuranTankEngageAction::Execute(', 'bool HuhuranRangedAction::isUseful()']:
            cpp += body(text, signature) + '\n'
        cpp += r'''
int main(){
 Group raid,other;Player a,b,c,r;for(auto p:{&a,&b,&c,&r})p->group=&raid;
 PlayerbotAI aa{&a},ab{&b},ac{&c},ar{&r};
 for(auto p:{&aa,&ab,&ac}){p->co={"huhuran tank"};p->auras={"defensive stance"};}
 r.tank=false;r.ranged=true;r.x=4;ar.co={"huhuran ranged"};
 raid.members={&a,&b,&c,&r};PlayerbotAIStorage::Instance().ais={{&a,&aa},{&b,&ab},{&c,&ac},{&r,&ar}};
 boss.victim=&a;Event e;
 // All three tanks select/engage from the first group combat observation.
 for(auto p:{&aa,&ab,&ac}){
  assert(GetAssignedHuhuranTankTarget(p)==&boss);
  HuhuranTankEngageAction engage(p);assert(engage.isUseful());assert(engage.Execute(e));assert(p->attacked==&boss);
 }
 aa.auras.clear();HuhuranTankEngageAction engage(&aa);assert(engage.Execute(e));assert(aa.stance=="defensive stance");
 aa.auras={"defensive stance"};aa.current=&boss;a.victim=&boss;a.melee=true;assert(!engage.isUseful());
 a.alive=false;boss.victim=&b;assert(GetAssignedHuhuranTankTarget(&ab)==&boss);assert(GetAssignedHuhuranTankTarget(&ac)==&boss);
 b.alive=false;boss.victim=&c;assert(GetAssignedHuhuranTankTarget(&ac)==&boss);a.alive=b.alive=true;
 HuhuranTauntMultiplier gate(&ac);
 for(auto name:{"taunt","taunt on snare target","growl","mocking blow","challenging shout","challenging roar","righteous defense","hand of reckoning"}){
  Action action(&ac,name);action.target=&boss;assert(gate.GetValue(&action)==0);
 }
 for(auto name:{"sunder armor","heroic strike","shield slam","revenge","shield block"}){
  Action action(&ac,name);assert(gate.GetValue(&action)==1);
 }
 ac.co.insert("stay");assert(!GetAssignedHuhuranTankTarget(&ac));ac.co.erase("stay");
 ac.nc.insert("stay");assert(!GetAssignedHuhuranTankTarget(&ac));ac.nc.clear();
 ac.ordered=&add;assert(!GetAssignedHuhuranTankTarget(&ac));ac.ordered=nullptr;
 ac.mark=&add;assert(!GetAssignedHuhuranTankTarget(&ac));ac.mark=nullptr;
 ac.co.clear();assert(!GetAssignedHuhuranTankTarget(&ac));ac.co={"huhuran tank"};
 c.tank=false;assert(!GetAssignedHuhuranTankTarget(&ac));c.tank=true;
 c.teleport=true;assert(!GetAssignedHuhuranTankTarget(&ac));c.teleport=false;
 c.map=509;assert(!GetAssignedHuhuranTankTarget(&ac));c.map=531;
 c.instance=2;assert(!GetAssignedHuhuranTankTarget(&ac));c.instance=1;
 boss.combat=false;assert(!GetAssignedHuhuranTankTarget(&ac));boss.combat=true;
 boss.evade=true;assert(!GetAssignedHuhuranTankTarget(&ac));boss.evade=false;
 boss.alive=false;assert(!GetAssignedHuhuranTankTarget(&ac));boss.alive=true;
 boss.entry=15510;assert(!GetAssignedHuhuranTankTarget(&ac));boss.entry=15509;
 c.group=&other;boss.victim=&a;assert(!GetAssignedHuhuranTankTarget(&ac));c.group=&raid;
 ac.attackers.clear();assert(!GetAssignedHuhuranTankTarget(&ac));ac.attackers={1};
 // Ranged stays outside soakers where possible; dead/missing soakers cannot cause an unbounded retreat.
 HuhuranRangedAction move(&ar);assert(move.isUseful());
 r.casting=true;assert(!move.isUseful());r.casting=false;
 r.channel=true;assert(!move.isUseful());r.channel=false;
 ar.co.insert("stay");assert(!move.isUseful());ar.co.erase("stay");
 boss.victim=&r;assert(!move.isUseful());boss.victim=&a;
 assert(HuhuranRangedRadius({})==24);assert(!HuhuranNeedsSpace(24,24));
 std::vector<float> d(15,10);assert(HuhuranRangedRadius(d)==24);
 d[0]=24;assert(HuhuranRangedRadius(d)==27);d[0]=100;assert(HuhuranRangedRadius(d)==28);
 d.pop_back();assert(HuhuranRangedRadius(d)==24);
 assert(RangedRadius(&ar,&boss)==24);
 std::vector<Player> soakers(15);raid.members.clear();
 for(auto& p:soakers){p.group=&raid;p.x=25;raid.members.push_back(&p);}
 assert(RangedRadius(&ar,&boss)==28);
 soakers[0].alive=false;assert(RangedRadius(&ar,&boss)==24);soakers[0].alive=true;
 PlayerbotAI protectedPeer(&soakers[0]);soakers[0].tank=false;soakers[0].ranged=true;
 protectedPeer.co={"huhuran ranged"};PlayerbotAIStorage::Instance().ais[&soakers[0]]=&protectedPeer;
 assert(RangedRadius(&ar,&boss)==24);

 r.x=24;assert(!move.isUseful());
 HuhuranRangedMultiplier rangeGate(&ar);Action reach(&ar,"reach spell");reach.target=&boss;
 assert(rangeGate.GetValue(&reach)==0);reach.target=&a;assert(rangeGate.GetValue(&reach)==1);
 reach.target=&boss;r.x=40;assert(rangeGate.GetValue(&reach)==1);
 ar.co.clear();assert(!move.isUseful());assert(rangeGate.GetValue(&reach)==1);
}
'''
        run(cpp)

    def test_production_opening_delay(self):
        text = (ROOT/'ai/playerbot/strategy/generic/CombatStrategy.cpp').read_text()
        cpp = r'''
#include <cassert>
#include <ctime>
#include <string>
namespace BotState {constexpr int BOT_STATE_COMBAT=0;}
struct Unit {virtual ~Unit()=default;};struct Group {};
struct Player:Unit {Group group;Group* GetGroup(){return &group;}};
struct AiObjectContext {template<class T>struct V{T Get(){return nullptr;}};template<class T>V<T>* GetValue(char const*){static V<T> v;return &v;}};
struct PlayerbotAI {bool assigned=true;Player bot;AiObjectContext ctx;
 bool HasStrategy(char const*,int){return true;}bool HasRealPlayerMaster(){return true;}
 Player* GetBot(){return &bot;}AiObjectContext* GetAiObjectContext(){return &ctx;}};
bool TwinsOpeningHold(PlayerbotAI*){return false;}bool TwinsOpeningTank(PlayerbotAI*){return false;}
bool PatchwerkDpsOpeningHold(PlayerbotAI*){return false;}Unit* GetPatchwerkSoakBoss(PlayerbotAI*){return nullptr;}
Unit* GetAssignedHuhuranTankTarget(PlayerbotAI* ai){return ai->assigned?&ai->bot:nullptr;}
struct {bool IsFriendlyTo(Unit*,Player*){return true;}}sServerFacade;
#define AI_VALUE(type,key) time(nullptr)
struct WaitForAttackStrategy {static bool ShouldWait(PlayerbotAI*);static unsigned GetWaitTime(PlayerbotAI*){return 30;}};
'''
        cpp += body(text, 'bool WaitForAttackStrategy::ShouldWait(')
        cpp += '\nint main(){PlayerbotAI ai;assert(!WaitForAttackStrategy::ShouldWait(&ai));ai.assigned=false;assert(WaitForAttackStrategy::ShouldWait(&ai));}\n'
        run(cpp)

    def test_production_threat_backoff(self):
        text = (ROOT/'ai/playerbot/strategy/generic/ThreatStrategy.cpp').read_text()
        cpp = r'''

#include <cassert>
#include <string>
using uint8=unsigned char;
namespace BotState {constexpr int BOT_STATE_COMBAT=1;}
enum class ActionThreatType {ACTION_THREAT_NONE,ACTION_THREAT_SINGLE,ACTION_THREAT_AOE};
struct Action {virtual ~Action(){};virtual std::string getName(){return "action";}virtual ActionThreatType GetThreatType(){return ActionThreatType::ACTION_THREAT_AOE;}};
struct CastSpellAction:Action {std::string name;std::string getName() override{return name;}};
struct CastHealingSpellAction:CastSpellAction {};
struct ShieldAction:CastSpellAction {ActionThreatType GetThreatType() override{return ActionThreatType::ACTION_THREAT_NONE;}};
struct Unit {virtual ~Unit(){};Unit* GetVictim(){return nullptr;}Unit* ToPlayer(){return nullptr;}unsigned GetObjectGuid(){return 1;}};
using Player=Unit;
struct Group {bool IsMember(unsigned){return false;}};
struct Bot {void SendPlaySpellVisual(int){};Group* GetGroup(){return nullptr;}};
struct PlayerbotAI {static bool IsTank(Player*){return false;}};
namespace TortoiseBots {namespace BotCommands {bool IsInCommandedGroup(Bot*){return false;}}
namespace RemoteCommander {bool CommandedBackoffYields(bool,bool,bool){return false;}}}
struct AI {bool assigned=true,unsafe=true,neglect=false,group=true,high=true;int aoe=100;Bot bot;
 bool HasStrategy(std::string,int){return false;}Bot* GetMaster(){return nullptr;}Bot* GetBot(){return &bot;}
 int value(std::string n){return n=="neglect threat"?neglect:n=="group"?group:n=="aoe"?aoe:n=="high threat"?high:0;}};
Unit* GetAssignedHuhuranTankTarget(AI* ai){static Unit boss;return ai->assigned?&boss:nullptr;}
bool RaidHealerShouldHoldDamage(AI* ai){return ai->unsafe;}
template<class T>T fixtureValue(AI* ai,std::string name){if constexpr(std::is_pointer_v<T>)return nullptr;else return static_cast<T>(ai->value(name));}
#define AI_VALUE(type,name) fixtureValue<type>(ai,name)
#define AI_VALUE2(type,name,qualifier) static_cast<type>(ai->value(qualifier))
struct Multiplier {AI* ai;Bot* bot=ai->GetBot();float GetValue(Action* action) BODY};
int main(){AI ai;Multiplier m{&ai};CastSpellAction damage;
 assert(m.GetValue(&damage)==1); // assigned backup tank must build threat
 ai.assigned=false;assert(m.GetValue(&damage)==0); // ordinary DPS still backs off
 CastHealingSpellAction heal;assert(m.GetValue(&heal)==1);
}
'''
        function = body(text, 'float ThreatMultiplier::GetValue(')
        run(cpp.replace('BODY', function[function.index('{'):]))

    def test_wiring(self):
        r = ROOT/'ai/playerbot/strategy'
        for path,names in [('StrategyContext.h',['huhuran tank','huhuran ranged']),
                           ('actions/ActionContext.h',['huhuran tank engage','huhuran ranged move']),
                           ('triggers/TriggerContext.h',['huhuran tank engage needed','huhuran ranged too close'])]:
            for name in names:self.assertIn('creators["'+name+'"]',(r/path).read_text())
        tank = body((r/'values/TankTargetValue.cpp').read_text(),'Unit* TankTargetValue::Calculate(')
        self.assertLess(tank.index('GetExplicitAttackTarget'),tank.index('GetAssignedHuhuranTankTarget'))
        self.assertLess(tank.index('GetAssignedHuhuranTankTarget'),tank.index('FindTankTargetSmartStrategy strategy'))
        threat = body((r/'generic/ThreatStrategy.cpp').read_text(),'float ThreatMultiplier::GetValue(')
        self.assertLess(threat.index('GetAssignedHuhuranTankTarget'),threat.index('"high threat"'))
        source = (r/'actions/HuhuranActions.cpp').read_text()
        self.assertNotIn('ai->CastSpell',source)
        self.assertIn('HuhuranTankStrategy::InitReactionMultipliers',source)
        self.assertIn('HuhuranTankStrategy::InitNonCombatTriggers',source)
        move = body(source,'bool HuhuranRangedAction::Execute(')
        for gate in ['IsMovingAllowed','hazards','IsWithinLOS','canPathTo']:
            self.assertIn(gate,move)


if __name__ == '__main__':
    unittest.main()
