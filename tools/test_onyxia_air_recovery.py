"""Run production air-target recovery with an empty attacker cache and grounded routes."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(os.environ.get('TBOTS_538_SOURCE', Path(__file__).resolve().parents[1]))
def function(text, signature):
    a = text.index(signature); b = text.index('{', a) + 1; depth = 1
    while depth:
        depth += (text[b] == '{') - (text[b] == '}'); b += 1
    return text[a:b]
def run(code):
    with tempfile.TemporaryDirectory() as d:
        p = Path(d); (p/'test.cpp').write_text(code)
        subprocess.run(['g++', '-std=c++17', '-I'+str(ROOT/'ai'), str(p/'test.cpp'), '-o', str(p/'test')], check=True)
        subprocess.run([str(p/'test')], check=True)

STUB = r'''
#include <cassert>
#include <list>
#include <string>
#include <vector>
#include <type_traits>
#include "playerbot/OnyxiaAirPolicy.h"
using namespace ai;
struct ObjectGuid{unsigned long long raw=0;ObjectGuid(unsigned long long v=0):raw(v){}operator unsigned long long()const{return raw;}unsigned long long GetRawValue()const{return raw;}};
struct WorldTimer{static unsigned getMSTime(){return 10000;}};
struct OnyxiaAirCombatState{unsigned long long target=0,recovered=0;unsigned choiceAt=0;};
enum class OnyxiaAirLog{Recovery,State,Reach,Cast};
using uint32=unsigned;constexpr int CURRENT_AUTOREPEAT_SPELL=2;
struct Spell{struct Targets{ObjectGuid guid;ObjectGuid getUnitTargetGuid(){return guid;}}m_targets;};
enum class BotState {BOT_STATE_COMBAT,BOT_STATE_NON_COMBAT};
struct Player; struct Unit;
struct Threat {float value=1;float getThreat(Player*){return value;}};
struct Unit {
 virtual ~Unit(){} unsigned entry=10184,map=249,instance=1;bool alive=true,combat=true,hover=true,flying=true,world=true;
 int guid=99;Unit*victim=nullptr;Threat threat;
 bool IsAlive(){return alive;}bool IsInWorld(){return world;}bool IsInCombat(){return combat;}
 unsigned GetEntry(){return entry;}bool IsInMap(Unit*b){return map==b->map&&instance==b->instance;}
 bool HasAura(unsigned){return hover;}bool IsFlying(){return flying;}Threat&GetThreatManager(){return threat;}
 Unit*GetVictim(){return victim;}ObjectGuid getObjectGuid(){return guid;}ObjectGuid GetObjectGuid(){return guid;}std::string GetName(){return "Onyxia";}
};
struct Creature:Unit {bool evade=false;bool IsInEvadeMode(){return evade;}};
struct Player:Unit {Spell*repeat=nullptr;int interrupts=0;Spell*GetCurrentSpell(int){return repeat;}void InterruptSpell(int){repeat=nullptr;++interrupts;}bool teleported=false,attackable=true;int selection=0;std::vector<Player*>group;
 unsigned GetMapId(){return map;}bool IsBeingTeleported(){return teleported;}bool IsValidAttackTarget(Unit*){return attackable;}
 std::vector<Player*>*GetGroup(){return &group;}int GetSelectionGuid(){return selection;}void SetSelectionGuid(int g){selection=g;}
 void SetInCombatWith(Unit*){combat=true;}
};
auto LiveGroupMembers(std::vector<Player*>*g){return *g;}
template<class T>struct Value{T v{};int resets=0;T Get(){return v;}void Set(T a){v=a;}void Reset(){++resets;v={};}};
struct StateValue{OnyxiaAirCombatState v;OnyxiaAirCombatState&Get(){return v;}};
struct AiObjectContext {StateValue state;Value<unsigned>active;Value<bool>invalid;Value<Unit*>dps;Value<Unit*>boss,current,marked;Value<ObjectGuid>order;std::list<int>attackers;
 template<class T>auto GetValue(std::string key,std::string qualifier=""){if constexpr(std::is_same_v<T,unsigned>)return &active;else if constexpr(std::is_same_v<T,OnyxiaAirCombatState&>)return &state;else if constexpr(std::is_same_v<T,bool>)return &invalid;else if constexpr(std::is_same_v<T,Unit*>)return key=="boss target"?&boss:key=="rti target"?&marked:key=="dps target"?&dps:&current;else return &order;}
 template<class T>T Read(std::string key){if constexpr(std::is_same_v<T,std::list<int>>)return attackers;else return GetValue<T>(key)->Get();}
};
#define AI_VALUE(T,N) context->Read<T>(N)
struct PlayerbotAI {Player bot;AiObjectContext context;Creature boss;bool ranged=true,tank=false,heal=false,enabled=true;int clocks=0,starts=0;
 BotState state=BotState::BOT_STATE_NON_COMBAT;std::string strategy="onyxia air spread";
 PlayerbotAI(){bot.combat=false;context.boss.v=&boss;}
 Player*GetBot(){return &bot;}AiObjectContext*GetAiObjectContext(){return &context;}Unit*GetUnit(int g){return g==99?&boss:nullptr;}
 bool IsRanged(Player*){return ranged;}bool IsTank(Player*){return tank;}bool IsHeal(Player*){return heal;}
 bool HasStrategy(std::string name,BotState){return enabled&&name==strategy;}BotState GetState(){return state;}
 void OnCombatStarted(){++starts;state=BotState::BOT_STATE_COMBAT;}void ClearActionClock(){++clocks;}
};
struct Config {float sightDistance=60;void logEvent(PlayerbotAI*,std::string,std::string,std::string){}}sPlayerbotAIConfig;
struct PossibleAttackTargetsValue{static bool IsValid(Unit*,Player*,float,bool,bool){return true;}};
void LogOnyxiaAir(PlayerbotAI*,OnyxiaAirLog,char const*,Unit* =nullptr,std::string const& = ""){}
namespace ai {Unit*GetAirborneOnyxia(PlayerbotAI*);bool OnyxiaRangedFocusEnabled(PlayerbotAI*);bool IsOnyxiaAirCombatTarget(PlayerbotAI*,Unit*);void UpdateOnyxiaAirCombat(PlayerbotAI*);
Unit*GetOnyxiaRangedTarget(PlayerbotAI*a){return GetAirborneOnyxia(a);}}
'''
class OnyxiaAirRecoveryTest(unittest.TestCase):
    def test_empty_attackers_and_cleared_combat_recover(self):
        source=(ROOT/'ai/playerbot/strategy/actions/OnyxiasLairDungeonActions.cpp').read_text()
        code=STUB
        for name in ['bool EngagedAirborneOnyxia(', 'bool OnyxiaAirDamageRole(']:
            if name in source: code+=function(source,name)
        code+=function(source,'Unit* ai::GetAirborneOnyxia(')
        code+=function(source,'bool ai::OnyxiaRangedFocusEnabled(')
        if 'void ai::UpdateOnyxiaAirCombat(' in source:
            code+=function(source,'bool ai::IsOnyxiaAirCombatTarget(')
            code+=function(source,'void ai::UpdateOnyxiaAirCombat(')
        else: code+='void ai::UpdateOnyxiaAirCombat(PlayerbotAI*){}\nbool ai::IsOnyxiaAirCombatTarget(PlayerbotAI*,Unit*){return false;}'
        run(code+r'''
int main(){PlayerbotAI a;
assert(a.context.attackers.empty()&&!a.bot.IsInCombat());
assert(GetAirborneOnyxia(&a)==&a.boss);assert(OnyxiaRangedFocusEnabled(&a));
UpdateOnyxiaAirCombat(&a);assert(a.bot.combat&&a.starts==1&&a.context.current.v==&a.boss&&a.bot.selection==99);
int clocks=a.clocks;UpdateOnyxiaAirCombat(&a);assert(a.clocks==clocks&&a.starts==1);
a.strategy="onyxia's lair";assert(OnyxiaRangedFocusEnabled(&a));
a.boss.threat.value=0;assert(!GetAirborneOnyxia(&a));
Player member;a.bot.group.push_back(&member);a.boss.victim=&member;assert(GetAirborneOnyxia(&a));
member.instance=2;assert(!GetAirborneOnyxia(&a));member.instance=1;
a.boss.evade=true;assert(!GetAirborneOnyxia(&a));a.boss.evade=false;
a.boss.hover=a.boss.flying=false;assert(!GetAirborneOnyxia(&a));a.boss.hover=true;
a.boss.combat=false;assert(!GetAirborneOnyxia(&a));a.boss.combat=true;
a.boss.alive=false;assert(!GetAirborneOnyxia(&a));a.boss.alive=true;
a.bot.map=1;assert(!GetAirborneOnyxia(&a));a.bot.map=249;
for(int i=0;i<3;++i){a.ranged=i!=0;a.heal=i==1;a.tank=i==2;a.bot.combat=false;a.context.current.v=nullptr;
UpdateOnyxiaAirCombat(&a);assert(!a.bot.combat&&!a.context.current.v);}
a.ranged=true;a.heal=a.tank=false;
Unit marked;a.context.marked.v=&marked;UpdateOnyxiaAirCombat(&a);assert(!a.bot.combat&&!a.context.current.v);
a.context.marked.v=nullptr;a.enabled=false;assert(!OnyxiaRangedFocusEnabled(&a));
}
''')
    def test_west_route_rejects_real_cave_detours(self):
        run(r'''
#include "playerbot/OnyxiaTankPositionPolicy.h"
#include <cassert>
using namespace ai;
int main(){OnyxiaTankPoint start{-83.7f,-215,-82.547f},end{22.8763f,-217.152f,-84.861f};
assert(OnyxiaTankRouteSafe(start,{start,end},end));
OnyxiaTankPoint south{-110,-232,-93.117f},north{-110,-198,-92.895f};
assert(!OnyxiaTankRouteSafe(south,{south,{-85.333f,-256,-93.847f},end},end));
assert(!OnyxiaTankRouteSafe(north,{north,{-85.333f,-170.667f,-93.847f},end},end));
assert(!OnyxiaTankRouteSafe(start,{start,{-65,-215,-84.247f}},end));
}
''')
    def test_recovery_is_before_action_delay(self):
        s=(ROOT/'ai/playerbot/PlayerbotAI.cpp').read_text()
        self.assertIn('UpdateOnyxiaAirCombat(this);',s)
        self.assertLess(s.index('UpdateOnyxiaAirCombat(this);'),s.index('UpdateFankrissHealing(this);'))
    def test_ground_spell_approach_and_safety(self):
        p=ROOT/'ai/playerbot/strategy/actions/OnyxiaAirReachAction.cpp'
        method=function(p.read_text(),'bool ReachTargetAction::ReachAirborneOnyxia(') if p.exists() else 'bool ReachTargetAction::ReachAirborneOnyxia(Unit*){return false;}'
        run(r'''
#include <cassert>
#include <cmath>
#include <map>
#include <list>
#include <vector>
#include <string>
#include <algorithm>
#include "playerbot/OnyxiaAirPolicy.h"
#include "playerbot/OnyxiaTankPositionPolicy.h"
using namespace ai;using uint32=unsigned;
struct Unit{float x=22.8763f,y=-217.152f,z=-60,reach=22;float GetPositionX(){return x;}float GetPositionY(){return y;}float GetPositionZ(){return z;}float GetCollisionHeight(){return 2;}float GetCombatReach(){return reach;}struct Guid{int GetRawValue(){return 99;}};Guid GetObjectGuid(){return {};}};
struct Spline{bool final=true;bool Finalized(){return final;}};
struct Player:Unit{Spline spline,*movespline=&spline;bool casting=false;
Player(){x=-50;y=-215;z=-86;reach=1.5f;}
bool GetTransport(){return false;}bool IsTaxiFlying(){return false;}bool IsFalling(){return false;}bool IsNonMeleeSpellCasted(bool,bool,bool){return casting;}
float GetCombatDistance(Unit*t){return std::sqrt((x-t->x)*(x-t->x)+(y-t->y)*(y-t->y)+(z-t->z)*(z-t->z))-reach-t->reach;}
bool IsWithinLOSInMap(Unit*,bool){return true;}bool IsWithinLOSAtPosition(float,float,float,float,float,float,bool,float){return true;}};
struct Position{bool set=false;bool isSet(){return set;}};using PositionMap=std::map<std::string,Position>;
struct Context{PositionMap positions;};
#define AI_VALUE(T,N) context->positions
struct AI{float minimum=0;bool GetSpellRange(std::string,float*max,float*min){*max=30;*min=minimum;return true;}bool move=true,stay=false,breath=false;bool CanMove(){return move;}int GetState(){return 0;}bool HasStrategy(std::string n,int){return stay&&n=="stay";}};
enum class OnyxiaAirLog{Reach};void LogOnyxiaAir(AI*,OnyxiaAirLog,char const*,Unit*,std::string const&){}
bool IsOnyxiaAirCombatTarget(AI*,Unit*t){return t;}bool OnyxiaBreathActive(AI*a){return a->breath;}
struct WorldTimer{static unsigned getMSTime(){return 10000;}};
bool cave=false,incomplete=false;int launches=0;float launchedZ=0;float launchedX=0;
struct WorldPosition{float x,y,z;
WorldPosition(Unit*u):x(u->x),y(u->y),z(u->z){}WorldPosition(unsigned,float X,float Y,float Z):x(X),y(Y),z(Z){}
float getX()const{return x;}float getY()const{return y;}float getZ()const{return z;}void setZ(float Z){z=Z;}float GetHeight(){return -86;}
float distance(WorldPosition const&o)const{return std::sqrt((x-o.x)*(x-o.x)+(y-o.y)*(y-o.y)+(z-o.z)*(z-o.z));}
std::vector<WorldPosition> GetPathTo(WorldPosition const&p,Player*)const {if(cave)return {*this,{249,-40,-255,-86},p};return {*this,p};}
bool isPathTo(std::vector<WorldPosition>const&,float,float)const{return !incomplete;}};
namespace Movement{struct Point{float x,y,z;Point(float X,float Y,float Z):x(X),y(Y),z(Z){}};using PointsArray=std::vector<Point>;
struct MoveSplineInit{PointsArray points;MoveSplineInit(Player&,char const*){}void MovebyPath(PointsArray const&p){points=p;}void SetWalk(bool){}void SetFacingGUID(int){}
void Launch(){++launches;launchedZ=points.back().z;launchedX=points.back().x;}};}
struct ReachTargetAction{Player*bot;AI*ai;Context*context;std::string spellName="frostbolt";float range=30;unsigned lastOnyxiaAirRoute=0;bool hazard=false,movement=true;
bool IsHazardNearPosition(WorldPosition const&){return hazard;}bool IsMovingAllowed(unsigned,float,float,float){return movement;}
bool ReachAirborneOnyxia(Unit*);};
'''+method+r'''
int main(){Player bot;Unit boss;AI ai;Context context;ReachTargetAction action{&bot,&ai,&context};
assert(bot.GetCombatDistance(&boss)>30);assert(action.ReachAirborneOnyxia(&boss));assert(launches==1&&launchedZ==-86&&launchedX>bot.x);
action.lastOnyxiaAirRoute=0;cave=true;assert(!action.ReachAirborneOnyxia(&boss)&&launches==1);cave=false;
action.lastOnyxiaAirRoute=0;incomplete=true;assert(!action.ReachAirborneOnyxia(&boss));incomplete=false;
action.lastOnyxiaAirRoute=0;action.hazard=true;assert(!action.ReachAirborneOnyxia(&boss));action.hazard=false;
action.lastOnyxiaAirRoute=0;action.movement=false;assert(!action.ReachAirborneOnyxia(&boss));action.movement=true;
action.lastOnyxiaAirRoute=0;ai.stay=true;assert(!action.ReachAirborneOnyxia(&boss));ai.stay=false;
ai.breath=true;assert(!action.ReachAirborneOnyxia(&boss));ai.breath=false;
bot.casting=true;assert(!action.ReachAirborneOnyxia(&boss));bot.casting=false;
context.positions["pull hold"].set=true;assert(!action.ReachAirborneOnyxia(&boss));context.positions.clear();
bot.x=launchedX;assert(!action.ReachAirborneOnyxia(&boss));assert(launches==1);
// Hunter minimum range: move away on the floor rather than idle inside the dead zone.
action.lastOnyxiaAirRoute=0;ai.minimum=8;bot.x=boss.x;bot.y=boss.y;boss.z=-65;
assert(action.ReachAirborneOnyxia(&boss));assert(launches==2&&launchedX<bot.x);
// The already validated central lane connects western staging to spread floor.
action.lastOnyxiaAirRoute=0;ai.minimum=0;bot.x=-83.7f;bot.y=-215;
assert(action.ReachAirborneOnyxia(&boss));assert(launches==3);

}
''')

if __name__=='__main__':unittest.main()
