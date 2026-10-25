"""Execute the production Sartura assignment/recovery and escape multipliers.
Only world/spell/navigation operations are doubles; --root tests the old tree.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

class SarturaMainTank(unittest.TestCase):
    def compile_run(self, code):
        self.assertIsNotNone(shutil.which('g++'), 'C++ compiler required')
        with tempfile.TemporaryDirectory() as tmp:
            cpp, exe = Path(tmp)/'fixture.cpp', Path(tmp)/'fixture'
            cpp.write_text(code)
            subprocess.run(['g++', '-std=c++17', '-I', str(ROOT), str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_assignment_and_recovery(self):
        path = ROOT/'ai/playerbot/strategy/actions/SarturaTankActions.cpp'
        source = path.read_text() if path.exists() else ''
        code = r'''
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <vector>
#include "ai/playerbot/EbonrocSwapPolicy.h"
using uint32=unsigned;
constexpr int CLASS_WARRIOR=1, CLASS_DRUID=11, UNIT_STAT_MELEE_ATTACKING=1;
constexpr int EFFECT_INDEX_0=0, EFFECT_INDEX_2=2;
constexpr int SPELL_EFFECT_ATTACK_ME=1, SPELL_EFFECT_APPLY_AURA=2, SPELL_AURA_MOD_TAUNT=3;
using SpellEffectIndex=int;
namespace BotState {constexpr int BOT_STATE_COMBAT=0;}
struct ObjectGuid {unsigned id=0;bool operator<(ObjectGuid b)const{return id<b.id;} bool operator==(ObjectGuid b)const{return id==b.id;}};
struct Group;
struct Unit {
    virtual ~Unit()=default;
    unsigned entry=15516,map=531,instance=4;ObjectGuid guid{100};
    bool alive=true,world=true,combat=true,valid=true,player=false,immune=false;unsigned aura=0;
    Unit* victim=nullptr;
    bool IsCreature(){return !player;}bool IsPlayer(){return player;}bool IsInWorld(){return world;}
    bool IsAlive(){return alive;}bool IsInCombat(){return combat;}
    bool IsInMap(Unit* other){return other && map==other->map && instance==other->instance;}
    bool HasAura(unsigned id){return aura==id;}unsigned GetEntry(){return entry;}
    ObjectGuid GetObjectGuid(){return guid;}Unit* GetVictim(){return victim;}
    bool IsImmuneToSpellEffect(void const*,int,bool){return immune;}
};
struct Creature:Unit{bool evade=false;bool IsInEvadeMode(){return evade;}};
struct Player:Unit{
    Player(){player=true;entry=0;}
    bool tank=true,teleport=false,reach=true,swing=true,learned=true;Group* group=nullptr;
    unsigned GetMapId(){return map;}Group* GetGroup(){return group;}bool IsBeingTeleported(){return teleport;}
    bool IsValidAttackTarget(Unit* other){return other->valid;}int GetClass(){return CLASS_WARRIOR;}
    bool HasSpell(unsigned){return learned;}bool HasUnitState(int){return swing;}
    bool CanReachWithMeleeAutoAttack(Unit*){return reach;}
};
struct Group{std::vector<Player*> members;};
struct Position{bool set=false;bool isSet(){return set;}};using PositionMap=std::map<std::string,Position>;
struct AiObjectContext{};struct Event{};
namespace ai {
struct PlayerbotAI {
    Player* bot;bool assigned=false,stance=true,hazard=false,ready=true,los=true,possible=true;
    unsigned cast=0;std::string action;Unit* current=nullptr;ObjectGuid order;
    std::map<std::string,bool> strategies;PositionMap positions;std::list<ObjectGuid> targets{{100},{200},{201},{202}};
    Player* GetBot(){return bot;}bool IsTank(Player* b,bool){return b->tank;}
    bool HasStrategy(std::string n,int){return n=="sartura main tank"?assigned:strategies[n];}
    bool HasAura(char const*,Player*){return stance;}AiObjectContext* GetAiObjectContext(){return nullptr;}
    bool CanCastSpell(unsigned,Unit*){return ready && los && bot->reach;}
    bool CastSpell(unsigned id,Unit*){cast=id;return true;}
    bool DoSpecificAction(char const* name,Event&,bool){action=name;return true;}
    Unit* GetUnit(ObjectGuid);
    template<class T>T value(std::string);
};
Unit* GetAssignedSarturaTankTarget(PlayerbotAI*);
}
using namespace ai;
std::map<unsigned,Unit*> world;
Unit* PlayerbotAI::GetUnit(ObjectGuid g){auto it=world.find(g.id);return it==world.end()?nullptr:it->second;}
template<> Unit* PlayerbotAI::value<Unit*>(std::string){return current;}
template<> ObjectGuid PlayerbotAI::value<ObjectGuid>(std::string){return order;}
template<> std::list<ObjectGuid> PlayerbotAI::value<std::list<ObjectGuid>>(std::string){return targets;}
template<> PositionMap& PlayerbotAI::value<PositionMap&>(std::string){return positions;}
#define AI_VALUE(type,key) ai->value<type>(key)
#define AI_VALUE2(type,key,name) 355u
struct SpellEntry{int Effect[3]={SPELL_EFFECT_ATTACK_ME,0,0};int EffectApplyAuraName[3]={};};
struct Templates{SpellEntry spell;template<class T>T const* LookupEntry(unsigned){return &spell;}}sSpellTemplate;
struct Config{float reactDistance=100;}sPlayerbotAIConfig;
struct PossibleAttackTargetsValue{static bool IsValid(Unit* u,Player*,float,bool,bool){return u->valid;}};
struct PlayerbotAIStorage{
    std::map<Player*,PlayerbotAI*> ais;
    static PlayerbotAIStorage& Instance(){static PlayerbotAIStorage s;return s;}
    PlayerbotAI* GetAI(Player* p){auto i=ais.find(p);return i==ais.end()?nullptr:i->second;}
};
auto LiveGroupMembers(Group* group){return group->members;}
bool SarturaWhirlwindTooClose(PlayerbotAI* ai){return ai->hazard;}
struct Action {
    virtual ~Action()=default;std::string name;Unit* target=nullptr;
    std::string getName(){return name;}virtual Unit* GetTarget(){return target;}
};
struct MovementAction:Action{};struct CastReachTargetSpellAction:Action{};struct CastSpellAction:Action{};
struct AttackAction:MovementAction {
    PlayerbotAI* ai;Player* bot;
    explicit AttackAction(PlayerbotAI* a):ai(a),bot(a->bot){}
    bool isPossible(){return ai->possible;}Player* GetMaster(){return nullptr;}
    bool Attack(Player*,Unit* u){ai->current=u;bot->victim=u;bot->swing=true;return true;}
};
struct SarturaTankRecoverAction:AttackAction {
    using AttackAction::AttackAction;Unit* GetTarget() override;bool isUseful();bool Execute(Event&);
};
struct SarturaTankMultiplier {PlayerbotAI* ai;float GetValue(Action*);};
'''
        if source:
            header=(ROOT/'ai/playerbot/strategy/actions/SarturaTankActions.h').read_text()
            code += function(header, 'Unit* GetTarget()').replace('Unit* GetTarget() override', 'Unit* SarturaTankRecoverAction::GetTarget()')+'\n'
            for sig in ['bool SarturaTankEligible(', 'bool SarturaOrdered(', 'bool SarturaEngaged(',
                        'bool SarturaSpinning(', 'char const* SarturaMissingStance(', 'uint32 SarturaReadyTaunt(',
                        'Unit* ai::GetAssignedSarturaTankTarget(', 'bool SarturaTankRecoverAction::isUseful(',
                        'bool SarturaTankRecoverAction::Execute(', 'float SarturaTankMultiplier::GetValue(']:
                code += function(source, sig)+'\n'
        else:
            code += '''Unit* ai::GetAssignedSarturaTankTarget(PlayerbotAI*){return nullptr;}
            Unit* SarturaTankRecoverAction::GetTarget(){return nullptr;}
            bool SarturaTankRecoverAction::isUseful(){return false;}
            bool SarturaTankRecoverAction::Execute(Event&){return false;}
            float SarturaTankMultiplier::GetValue(Action*){return 1.0f;}'''
        code += r'''
int main(){
 Group raid;Player main,a,b,c,healer;PlayerbotAI am{&main},aa{&a},ab{&b},ac{&c},ah{&healer};
 raid.members={&c,&main,&healer,&b,&a};unsigned id=1;
 for(auto* ai:{&am,&aa,&ab,&ac,&ah}){
    ai->bot->guid={id++};ai->bot->group=&raid;PlayerbotAIStorage::Instance().ais[ai->bot]=ai;
 }
 healer.tank=false;am.assigned=true;
 Creature boss,g1,g2,g3;g1.entry=g2.entry=g3.entry=15984;g1.guid={200};g2.guid={201};g3.guid={202};
 world={{100,&boss},{200,&g1},{201,&g2},{202,&g3},{5,&healer}};
 for(auto* u:{&boss,&g1,&g2,&g3})u->victim=&healer;
 assert(GetAssignedSarturaTankTarget(&am)==&boss); // Old tournament has no Sartura pin.
 assert(GetAssignedSarturaTankTarget(&aa)==&g1);
 assert(GetAssignedSarturaTankTarget(&ab)==&g2);
 assert(GetAssignedSarturaTankTarget(&ac)==&g3);
 assert(!GetAssignedSarturaTankTarget(&ah));
 // Duplicate opt-in is ambiguous: fail closed, never silently choose a different main.
 aa.assigned=true;assert(!GetAssignedSarturaTankTarget(&aa));aa.assigned=false;
 am.assigned=false;assert(!GetAssignedSarturaTankTarget(&aa));am.assigned=true;
 main.alive=false;assert(!GetAssignedSarturaTankTarget(&aa));main.alive=true;
 main.teleport=true;assert(!GetAssignedSarturaTankTarget(&am));main.teleport=false;
 main.tank=false;assert(!GetAssignedSarturaTankTarget(&am));main.tank=true;
 main.map=509;assert(!GetAssignedSarturaTankTarget(&am));main.map=531;
 main.instance=5;assert(!GetAssignedSarturaTankTarget(&am));main.instance=4;
 for(auto name:{"stay","follow","wait for attack","grind"}){
    am.strategies[name]=true;assert(!GetAssignedSarturaTankTarget(&am));am.strategies[name]=false;
 }
 am.positions["pull hold"].set=true;assert(!GetAssignedSarturaTankTarget(&am));am.positions.clear();
 am.order={200};assert(!GetAssignedSarturaTankTarget(&am));am.order={100};
 assert(GetAssignedSarturaTankTarget(&am)==&boss);assert(GetAssignedSarturaTankTarget(&aa)==&g1);am.order={};
 aa.order={100};assert(!GetAssignedSarturaTankTarget(&aa));aa.order={};
 boss.combat=false;assert(!GetAssignedSarturaTankTarget(&am));boss.combat=true;
 boss.evade=true;assert(!GetAssignedSarturaTankTarget(&am));boss.evade=false;
 boss.alive=false;assert(!GetAssignedSarturaTankTarget(&am));boss.alive=true;
 Group other;healer.group=&other;assert(!GetAssignedSarturaTankTarget(&am));healer.group=&raid;
 g1.alive=false;g2.alive=false;g3.alive=false;assert(!GetAssignedSarturaTankTarget(&aa));
 g1.alive=g2.alive=g3.alive=true;
 Event event;SarturaTankRecoverAction recovery{&am};SarturaTankMultiplier multiplier{&am};
 am.current=&g1;main.victim=&g1;assert(recovery.GetTarget()==&boss);assert(multiplier.GetValue(&recovery)==1);assert(recovery.isUseful());assert(recovery.Execute(event));assert(am.current==&boss);
 // Recover lost threat with a real-ready taunt; cooldown/LOS/immunity gates refuse it.
 assert(recovery.Execute(event));assert(am.cast==355);am.cast=0;
 am.ready=false;assert(!recovery.isUseful());am.ready=true;
 am.los=false;assert(!recovery.isUseful());am.los=true;
 boss.immune=true;assert(!recovery.isUseful());boss.immune=false;
 boss.victim=&main;assert(!recovery.isUseful());boss.victim=&healer;
 main.reach=false;assert(recovery.Execute(event));assert(am.action=="reach melee");main.reach=true;
 am.stance=false;assert(recovery.Execute(event));assert(am.action=="defensive stance");am.stance=true;
 // The selector retains responsibility during a spin, but no attack/reach/taunt fights escape.
 boss.aura=26083;assert(GetAssignedSarturaTankTarget(&am)==&boss);assert(!recovery.isUseful());assert(!recovery.Execute(event));
 MovementAction reach;reach.name="reach melee";reach.target=&boss;assert(multiplier.GetValue(&reach)==0);
 for(auto name:{"avoid aoe","sartura whirlwind escape","move away from hazard","flee"}){
    MovementAction escape;escape.name=name;assert(multiplier.GetValue(&escape)==1);
 }
 CastSpellAction taunt;taunt.name="taunt";taunt.target=&boss;assert(multiplier.GetValue(&taunt)==0);
 Action defensive;defensive.name="shield wall";defensive.target=&main;assert(multiplier.GetValue(&defensive)==1);
 boss.aura=0;assert(recovery.isUseful());assert(multiplier.GetValue(&taunt)==1);
 am.hazard=true;assert(!recovery.Execute(event));assert(multiplier.GetValue(&reach)==0);am.hazard=false;
 taunt.target=&g1;assert(multiplier.GetValue(&taunt)==0);
 SarturaTankMultiplier off{&aa};taunt.target=&boss;assert(off.GetValue(&taunt)==0);
 taunt.target=&g1;assert(off.GetValue(&taunt)==1);g1.aura=26038;assert(off.GetValue(&taunt)==0);g1.aura=0;
 taunt.name="challenging shout";taunt.target=&a;assert(off.GetValue(&taunt)==0);
 // A queued recovery rechecks manual orders and live aura state at execution.
 am.order={200};assert(!recovery.Execute(event));assert(multiplier.GetValue(&reach)==1);am.order={};
 am.assigned=false;assert(off.GetValue(&taunt)==1);
}
'''
        self.compile_run(code)

    def test_upstream_stepout_remains_allowed(self):
        source=(ROOT/'ai/playerbot/strategy/generic/SarturaWhirlwindStrategy.cpp').read_text()
        code=r'''
#include <cassert>
#include <string>
struct PlayerbotAI{};
struct Action{virtual ~Action()=default;std::string name;std::string getName(){return name;}};
struct MovementAction:Action{};struct CastReachTargetSpellAction:Action{};
struct Multiplier{PlayerbotAI* ai;Multiplier(PlayerbotAI* a,char const*):ai(a){};virtual float GetValue(Action*)=0;};
bool SarturaWhirlwindTooClose(PlayerbotAI*){return true;}
'''+function(source,'class SarturaWhirlwindMultiplier')+';\n'+r'''
int main(){PlayerbotAI ai;SarturaWhirlwindMultiplier m(&ai);MovementAction step;
 step.name="avoid aoe";assert(m.GetValue(&step)==1);
 step.name="sartura whirlwind escape";assert(m.GetValue(&step)==1);
 step.name="reach melee";assert(m.GetValue(&step)==0);
}
'''
        self.compile_run(code)

    def test_wiring(self):
        strategy=(ROOT/'ai/playerbot/strategy/StrategyContext.h').read_text()
        self.assertIn('creators["sartura main tank"]',strategy)
        root=ROOT/'ai/playerbot/strategy'
        selector=function((root/'values/TankTargetValue.cpp').read_text(),'Unit* TankTargetValue::Calculate(')
        self.assertLess(selector.index('GetExplicitAttackTarget'),selector.index('GetAssignedSarturaTankTarget'))
        self.assertLess(selector.index('GetAssignedSarturaTankTarget'),selector.index('RtiTargetValue::Calculate'))
        self.assertIn('new SarturaTankMultiplier(ai)',(root/'generic/TankAssistStrategy.cpp').read_text())
        self.assertIn('creators["sartura tank recover"]',(root/'actions/ActionContext.h').read_text())
        self.assertIn('creators["sartura tank recover needed"]',(root/'triggers/TriggerContext.h').read_text())
        source=(root/'actions/SarturaTankActions.cpp').read_text()
        self.assertIn('"sartura tank recover", 65.0f',source)
        self.assertIn('IsImmuneToSpellEffect',source)
        self.assertIn('ai->CanCastSpell(id, boss)',source)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--root',type=Path)
    args,rest=parser.parse_known_args()
    if args.root:ROOT=args.root
    unittest.main(argv=[__file__]+rest)
