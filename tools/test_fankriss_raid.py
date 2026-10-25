"""Fankriss production scope, stop-to-heal and movement policy with core API doubles.
FANKRISS_SOURCE can point at the pre-patch source to reproduce both failures.
No navmesh or live spell execution is simulated; the native build checks that seam.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tools.test_nefarian_bone_target import function

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(os.environ.get('FANKRISS_SOURCE', ROOT))


def run(source):
    with tempfile.TemporaryDirectory() as folder:
        cpp = Path(folder) / 'fixture.cpp'
        exe = Path(folder) / 'fixture'
        cpp.write_text(source)
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-parameter', '-Wno-unused-variable', '-I', str(ROOT), str(cpp), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


@unittest.skipUnless(shutil.which('g++'), 'C++ compiler required')
class FankrissRaidTest(unittest.TestCase):
    def test_rear_and_movement_policy(self):
        run(r'''
#include "ai/playerbot/policy/raid/FankrissRaidPolicy.h"
#include <cassert>
using namespace ai;
int main(){
 assert(FankrissHealingSearchRange(80,false)==80);
 assert(FankrissHealingSearchRange(80,true)==200); // web-to-tank distance can exceed 80yd
 assert(FankrissHealingSearchRange(240,true)==240);
 auto p=FankrissRear(10,20,0,6); assert(p.x<10 && std::abs(p.y-20)<.01f);
 assert(std::hypot(p.x-10,p.y-20)<6);
 assert(FankrissAtRear(p.x+.5f,p.y,p,true,true));
 assert(!FankrissAtRear(p.x,p.y,p,true,false));
 assert(!FankrissAtRear(p.x,p.y,p,false,true));
 auto q=FankrissRear(10,20,3.14159265f,6);assert(q.x>10);
 assert(!FankrissAtRear(p.x,p.y,q,true,true)); // tank swap changes rear point
 for(auto name:{"reach melee","reach spell","set behind","follow","dps aoe"})
  assert(FankrissMovementBlocked(name,true,false,false));
 for(auto name:{"cleave","whirlwind","sweeping strikes","blade flurry","melee","fankriss rear stack","move away from hazard"})
  assert(!FankrissMovementBlocked(name,true,false,false));
 assert(FankrissMovementBlocked("follow",false,true,false));
 assert(FankrissMovementBlocked("formation station",false,true,false));
 assert(FankrissMovementBlocked("flee to master",false,true,true));
 assert(!FankrissMovementBlocked("flee to master",false,true,false));
 assert(FankrissMovementBlocked("reach party member to heal",false,true,true));
 assert(!FankrissMovementBlocked("reach party member to heal",false,true,false));
 assert(!FankrissMovementBlocked("flee",false,true,false));
 assert(!FankrissMovementBlocked("follow",false,false,true));
}
''')

    def test_production_heal_readiness_and_stop(self):
        path = SOURCE / 'ai/playerbot/strategy/actions/FankrissRaidActions.cpp'
        source = path.read_text() if path.exists() else ''
        signatures = ['bool HealInRange(', 'bool ReadyHealHere(', 'bool ai::FankrissHealReachable(',
                      'void ai::UpdateFankrissHealing(']
        methods = '\n'.join(function(source, sig) for sig in signatures) if source else '''
bool ai::FankrissHealReachable(PlayerbotAI*, Unit*) {return false;}
void ai::UpdateFankrissHealing(PlayerbotAI*) {}
'''
        run(r'''
#include <cassert>
#include <list>
#include <vector>
#include <string>
#include <algorithm>
using uint32=unsigned;
enum SpellCastResult {SPELL_CAST_OK, SPELL_FAILED_MOVING, SPELL_FAILED_NO_POWER, SPELL_FAILED_NOT_READY, SPELL_FAILED_NOT_IN_CONTROL};
enum {POINT_MOTION_TYPE, FOLLOW_MOTION_TYPE, CHASE_MOTION_TYPE, FLEEING_MOTION_TYPE, EFFECT_MOTION_TYPE};
namespace SizeFactor {constexpr int CombatReach=0;}
struct Unit {bool alive=true,los=true,map=true,friendly=true;unsigned health=43;float distance=24;
 bool IsAlive(){return alive;} bool IsInMap(Unit*){return map;}unsigned GetHealth(){return health;}unsigned GetMaxHealth(){return 100;}};
struct Spline {bool done=false;bool Finalized(){return done;}};
struct Motion {int type=POINT_MOTION_TYPE;int GetCurrentMovementGeneratorType(){return type;}};
struct Player:Unit {Spline spline;Spline* movespline=&spline;Motion motion;bool moving=true,falling=false,taxi=false,transport=false,known=true;
 float GetDistance(Unit* u,int){return u->distance;}bool IsWithinLOSInMap(Unit* u,bool){return u->los;}
 bool HasSpell(unsigned){return known;}bool IsFriendlyTo(Unit* u){return u->friendly;}
 bool IsFalling(){return falling;}bool IsTaxiFlying(){return taxi;}bool GetTransport(){return transport;}
 Motion* GetMotionMaster(){return &motion;}};
struct WorldPosition {float d=0;WorldPosition(Player*){}WorldPosition(float d):d(d){}float distance(WorldPosition p){return p.d;}};
using HazardPosition=std::pair<WorldPosition,float>;
struct AiObjectContext {Unit* patient=nullptr;std::list<HazardPosition> hazards;};
struct SpellEntry {};
struct {template<class T>T const* LookupEntry(unsigned){static T t;return &t;}} sSpellTemplate;
SpellCastResult coreResult=SPELL_CAST_OK;
struct Spell {Spell(Player*,SpellEntry const*,bool){}struct Targets{void setUnitTarget(Unit*){}} m_targets;
 SpellCastResult CheckCast(bool){return coreResult;}};
struct PlayerbotAI {Player bot;AiObjectContext context;bool active=true,movable=true;unsigned stops=0,delay=2000;float range=40;
 SpellCastResult reason=SPELL_FAILED_MOVING;
 Player* GetBot(){return &bot;}AiObjectContext* GetAiObjectContext(){return &context;}
 bool GetSpellRange(char const*,float* r){*r=range;return bot.known;}bool CanMove(){return movable;}
 bool CheckCanCastSpell(unsigned,Unit*,int,bool,void*,bool,bool,bool,SpellCastResult* out){*out=reason;return reason==SPELL_CAST_OK;}
 void StopMoving(){++stops;bot.moving=false;bot.spline.done=true;}
 void ClearActionClock(){delay=0;}};
struct {bool isMoving(Player* p){return p->moving;}} sServerFacade;
template<class T>T value(AiObjectContext* c,std::string name);
template<>Unit* value<Unit*>(AiObjectContext* c,std::string){return c->patient;}
template<>std::list<HazardPosition> value<std::list<HazardPosition>>(AiObjectContext* c,std::string){return c->hazards;}
#define AI_VALUE(T,N) value<T>(context,N)
#define AI_VALUE2(T,N,Q) 1u
std::vector<char const*> HealingSpells(Player*){return {"flash heal"};}
namespace ai {bool FankrissHealReachable(PlayerbotAI*,Unit*);void UpdateFankrissHealing(PlayerbotAI*);}
using namespace ai;
bool FankrissHealingActive(PlayerbotAI* a){return a->active;}
''' + methods + r'''
int main(){
 PlayerbotAI a;Unit patient;a.context.patient=&patient;a.bot.health=100;
 UpdateFankrissHealing(&a);assert(a.stops==1 && a.delay==0); // logged web return: 24yd, LOS, 43% HP
 auto reset=[&]{a.bot.moving=true;a.bot.spline.done=false;a.stops=0;a.delay=2000;};
 reset();patient.distance=55;UpdateFankrissHealing(&a);assert(!a.stops);
 patient.distance=24;patient.los=false;UpdateFankrissHealing(&a);assert(!a.stops);patient.los=true;
 for(auto failure:{SPELL_FAILED_NO_POWER,SPELL_FAILED_NOT_READY,SPELL_FAILED_NOT_IN_CONTROL}){
  a.reason=failure;UpdateFankrissHealing(&a);assert(!a.stops);}
 a.reason=SPELL_FAILED_MOVING;coreResult=SPELL_FAILED_NOT_IN_CONTROL;UpdateFankrissHealing(&a);assert(!a.stops);
 coreResult=SPELL_CAST_OK;a.context.hazards={{{0.f},5.f}};UpdateFankrissHealing(&a);assert(!a.stops);a.context.hazards.clear();
 for(int motion:{FLEEING_MOTION_TYPE,EFFECT_MOTION_TYPE}){a.bot.motion.type=motion;UpdateFankrissHealing(&a);assert(!a.stops);}
 a.bot.motion.type=POINT_MOTION_TYPE;a.active=false;UpdateFankrissHealing(&a);assert(!a.stops);a.active=true;
 a.bot.falling=true;UpdateFankrissHealing(&a);assert(!a.stops);a.bot.falling=false;
 a.movable=false;UpdateFankrissHealing(&a);assert(!a.stops);a.movable=true;
 a.bot.known=false;UpdateFankrissHealing(&a);assert(!a.stops);a.bot.known=true;
 patient.health=100;UpdateFankrissHealing(&a);assert(!a.stops);
 a.bot.health=20;a.bot.distance=0;UpdateFankrissHealing(&a);assert(a.stops==1); // self healing also stops a web return
}
''')

    def test_production_role_and_encounter_scope(self):
        source = (ROOT/'ai/playerbot/strategy/actions/FankrissRaidActions.cpp').read_text()
        methods = '\n'.join(function(source, sig) for sig in
                            ['bool Eligible(', 'bool IsGroupBoss(', 'Unit* ai::GetFankrissStackBoss(',
                             'bool ai::FankrissHealingActive('])
        run(r'''
#include <cassert>
#include <map>
#include <set>
#include <string>
#include <type_traits>
namespace BotState {constexpr int BOT_STATE_COMBAT=1;}
struct ObjectGuid {int id=0;};
struct Group {};
struct Unit {unsigned entry=15510;bool alive=true,combat=true,creature=true,evade=false,player=false,valid=true;
 Unit* victim=nullptr;unsigned map=531,instance=102;
 unsigned GetEntry(){return entry;}bool IsAlive(){return alive;}bool IsInCombat(){return combat;}
 bool IsCreature(){return creature;}bool IsInEvadeMode(){return evade;}bool IsPlayer(){return player;}
 bool IsInMap(Unit* b){return map==b->map && instance==b->instance;}Unit* GetVictim(){return victim;}};
using Creature=Unit;
struct Player:Unit {bool world=true,teleport=false,tank=false,heal=false,melee=true;Group* group=nullptr;
 unsigned GetMapId(){return map;}bool IsInWorld(){return world;}bool IsBeingTeleported(){return teleport;}
 Group* GetGroup(){return group;}};
struct Position {bool set=false;bool isSet(){return set;}};
using PositionMap=std::map<std::string,Position>;
struct AiObjectContext {PositionMap positions;Unit* explicitTarget=nullptr;Unit* mark=nullptr;Unit* boss=nullptr;};
struct PlayerbotAI {Player bot;AiObjectContext context;std::set<std::string> strategies;
 Player* GetBot(){return &bot;}AiObjectContext* GetAiObjectContext(){return &context;}
 bool HasStrategy(std::string s,int){return strategies.count(s);}
 bool IsTank(Player* p){return p->tank;}bool IsHeal(Player* p){return p->heal;}bool IsMelee(Player* p){return p->melee;}
 Unit* GetUnit(ObjectGuid){return context.explicitTarget;}};
struct {float sightDistance=100;} sPlayerbotAIConfig;
struct PossibleAttackTargetsValue {static bool IsValid(Unit* u,Player*,float,bool,bool){return u->valid;}};
template<class T>T value(AiObjectContext* c,std::string name) {
 if constexpr(std::is_same<T,PositionMap&>::value)return c->positions;
 else if constexpr(std::is_same<T,ObjectGuid>::value)return {};
 else return c->mark;
}
#define AI_VALUE(T,N) value<T>(context,N)
Unit* ActiveBoss(PlayerbotAI* a){return a->context.boss;}
namespace ai {Unit* GetFankrissStackBoss(PlayerbotAI*);bool FankrissHealingActive(PlayerbotAI*);}
using namespace ai;
''' + methods + r'''
int main(){
 Group group,other;PlayerbotAI ai;Player tank;Unit boss,add;boss.victim=&tank;add.entry=15630;
 tank.player=true;tank.group=&group;ai.bot.group=&group;ai.context.boss=&boss;ai.strategies={"fankriss stack"};
 assert(GetFankrissStackBoss(&ai)==&boss);
 ai.bot.tank=true;assert(!GetFankrissStackBoss(&ai));ai.bot.tank=false;
 ai.bot.heal=true;assert(!GetFankrissStackBoss(&ai));ai.bot.heal=false;
 ai.bot.melee=false;assert(!GetFankrissStackBoss(&ai));ai.bot.melee=true;
 for(auto name:{"stay","follow","wait for attack"}){ai.strategies.insert(name);assert(!GetFankrissStackBoss(&ai));ai.strategies.erase(name);}
 ai.context.positions["pull hold"].set=true;assert(!GetFankrissStackBoss(&ai));ai.context.positions.clear();
 ai.context.explicitTarget=&add;assert(!GetFankrissStackBoss(&ai));ai.context.explicitTarget=&boss;assert(GetFankrissStackBoss(&ai)==&boss);
 ai.context.explicitTarget=nullptr;ai.context.mark=&add;assert(!GetFankrissStackBoss(&ai));ai.context.mark=nullptr;
 boss.combat=false;assert(!GetFankrissStackBoss(&ai));boss.combat=true;
 boss.evade=true;assert(!GetFankrissStackBoss(&ai));boss.evade=false;
 boss.instance=103;assert(!GetFankrissStackBoss(&ai));boss.instance=102;
 tank.group=&other;assert(!GetFankrissStackBoss(&ai));tank.group=&group;
 ai.bot.map=1;assert(!GetFankrissStackBoss(&ai));ai.bot.map=531;
 ai.bot.teleport=true;assert(!GetFankrissStackBoss(&ai));ai.bot.teleport=false;
 ai.strategies.clear();assert(!GetFankrissStackBoss(&ai));
 ai.strategies={"fankriss healing"};assert(!FankrissHealingActive(&ai));ai.bot.heal=true;assert(FankrissHealingActive(&ai));
 boss.combat=false;assert(!FankrissHealingActive(&ai));
}
''')

    def test_wiring_and_safe_path(self):
        source = (ROOT/'ai/playerbot/strategy/actions/FankrissRaidActions.cpp').read_text()
        stack = function(source, 'Unit* ai::GetFankrissStackBoss(')
        for gate in ['"fankriss stack"', 'ai->IsTank(bot)', 'ai->IsHeal(bot)', '!ai->IsMelee(bot)',
                     'IsGroupBoss(bot, boss)', '"explicit attack target"', '"rti target"', '"pull hold"', '"stay"']:
            self.assertIn(gate, stack)
        path = function(source, 'bool FankrissStackAction::Execute(')
        self.assertLess(path.index('destination.isPathTo'), path.index('init.Launch()'))
        self.assertLess(path.index('sample.distance(hazard.first)'), path.index('init.Launch()'))
        self.assertNotIn('NearTeleport', path)
        selection = (ROOT/'ai/playerbot/strategy/values/PartyMemberToHeal.cpp').read_text()
        self.assertIn('FankrissHealingSearchRange(maxDist, FankrissHealingActive(ai))', function(selection, 'bool PartyMemberToHeal::Check('))
        self.assertIn('UpdateFankrissHealing(this);', (ROOT/'ai/playerbot/PlayerbotAI.cpp').read_text())
        for filename, names in [('strategy/StrategyContext.h', ['fankriss stack', 'fankriss healing']),
                                ('strategy/actions/ActionContext.h', ['fankriss rear stack']),
                                ('strategy/triggers/TriggerContext.h', ['fankriss rear needed']),
                                ('strategy/values/ValueContext.h', ['fankriss active boss'])]:
            text = (ROOT/'ai/playerbot'/filename).read_text()
            for name in names:
                self.assertIn('creators["'+name+'"]', text)


if __name__ == '__main__':
    unittest.main()
