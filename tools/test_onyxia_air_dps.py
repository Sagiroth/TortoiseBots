"""Live airborne threat and outer-floor ingress regressions."""
import os
from pathlib import Path
import unittest
from test_onyxia_air_recovery import function, run, STUB
ROOT=Path(os.environ.get('TBOTS_540_SOURCE',Path(__file__).resolve().parents[1]))
class OnyxiaAirDpsTest(unittest.TestCase):
    def test_live_outer_floor_ingress(self):
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
#include "playerbot/policy/raid/OnyxiaWhelpEggPolicy.h"
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
struct GameObject{float x=-1000,y=-1000;float GetPositionX(){return x;}float GetPositionY(){return y;}};
std::vector<GameObject*> eggs;auto CollectOnyxiaEggs(AI*){return eggs;}
enum class OnyxiaAirLog{Reach};void LogOnyxiaAir(AI*,OnyxiaAirLog,char const*,Unit*,std::string const&){}
bool IsOnyxiaAirCombatTarget(AI*,Unit*t){return t;}bool OnyxiaBreathActive(AI*a){return a->breath;}
struct WorldTimer{static unsigned getMSTime(){return 10000;}};
bool cave=false,incomplete=false;int launches=0;float launchedZ=0;float launchedX=0;float launchedY=0;
struct WorldPosition{float x,y,z;
WorldPosition(Unit*u):x(u->x),y(u->y),z(u->z){}WorldPosition(unsigned,float X,float Y,float Z):x(X),y(Y),z(Z){}
float getX()const{return x;}float getY()const{return y;}float getZ()const{return z;}void setZ(float Z){z=Z;}float GetHeight(){return -86;}
float distance(WorldPosition const&o)const{return std::sqrt((x-o.x)*(x-o.x)+(y-o.y)*(y-o.y)+(z-o.z)*(z-o.z));}
std::vector<WorldPosition> GetPathTo(WorldPosition const&p,Player*)const {if(cave)return {*this,{249,-40,-255,-86},p};return {*this,p};}
bool isPathTo(std::vector<WorldPosition>const&,float,float)const{return !incomplete;}};
namespace Movement{struct Point{float x,y,z;Point(float X,float Y,float Z):x(X),y(Y),z(Z){}};using PointsArray=std::vector<Point>;
struct MoveSplineInit{PointsArray points;MoveSplineInit(Player&,char const*){}void MovebyPath(PointsArray const&p){points=p;}void SetWalk(bool){}void SetFacingGUID(int){}
void Launch(){++launches;launchedZ=points.back().z;launchedX=points.back().x;launchedY=points.back().y;}};}
struct ReachTargetAction{Player*bot;AI*ai;Context*context;std::string spellName="frostbolt";float range=30;unsigned lastOnyxiaAirRoute=0;bool hazard=false,movement=true;
bool IsHazardNearPosition(WorldPosition const&){return hazard;}bool IsMovingAllowed(unsigned,float,float,float){return movement;}
bool ReachAirborneOnyxia(Unit*);};
'''+method+r'''
int main(){Player bot;Unit boss;AI ai;Context context;ReachTargetAction action{&bot,&ai,&context};
boss.x=100;
float starts[][3]={{-73.4726f,-243.702f,-81.3302f},{-52.4396f,-249.972f,-83.5256f},{7,-185,-86.2444f}};
for(auto const&p:starts){bot.x=p[0];bot.y=p[1];bot.z=p[2];action.lastOnyxiaAirRoute=0;
assert(action.ReachAirborneOnyxia(&boss));assert(OnyxiaSpreadFloor(launchedX,launchedY,launchedZ));}
int before=launches;action.lastOnyxiaAirRoute=0;cave=true;assert(!action.ReachAirborneOnyxia(&boss));cave=false;
action.lastOnyxiaAirRoute=0;incomplete=true;assert(!action.ReachAirborneOnyxia(&boss));incomplete=false;
action.lastOnyxiaAirRoute=0;GameObject egg;egg.x=bot.x;egg.y=bot.y;eggs.push_back(&egg);assert(!action.ReachAirborneOnyxia(&boss));eggs.clear();
action.lastOnyxiaAirRoute=0;bot.z=-60;assert(!action.ReachAirborneOnyxia(&boss));bot.z=-86;
action.lastOnyxiaAirRoute=0;bot.y=-270;assert(!action.ReachAirborneOnyxia(&boss));
assert(launches==before);

}
''')

    def test_production_threat_multiplier_and_backoff(self):
        source=(ROOT/'ai/playerbot/strategy/generic/ThreatStrategy.cpp').read_text()
        fn=function(source,'float ThreatMultiplier::GetValue(')
        code=THREAT_STUB.replace('BODY',fn[fn.index('{'):])
        run(code+r'''
int main(){AI ai;Multiplier m{&ai};SingleAction spell;Unit boss;spell.target=&boss;
assert(m.GetValue(&spell)==1); // airborne boss even with 100% relative threat
ai.air=false;assert(m.GetValue(&spell)==0); // landing restores rules
ai.air=true;spell.target=nullptr;assert(m.GetValue(&spell)==0); // other target
CastSpellAction aoe;aoe.target=&boss;assert(m.GetValue(&aoe)==0); // adds retain threat protection
CastHealingSpellAction heal;assert(m.GetValue(&heal)==1);
}
''')
        triggers=(ROOT/'ai/playerbot/strategy/triggers/GenericTriggers.cpp').read_text()
        # Execute each early exception; unrelated generic calculation returns true.
        for signature in ['bool HighThreatTrigger::IsActive(', 'bool MediumThreatTrigger::IsActive(',
                          'bool WandBlocksCastTrigger::ThreatVetoesNuke(', 'bool WandBlocksCastTrigger::RaidAggroWand(',
                          'bool HasAggroInRaidGroupTrigger::IsActive(']:
            f=function(triggers,signature)
            prefix=f[f.index('{')+1:];end=prefix.find('return false;')
            guard=prefix[:end+len('return false;')] if 'IsOnyxiaAirCombatTarget' in prefix else ''
            run('#include <cassert>\nstruct Unit{};struct AI{bool air=true;}brain;AI*ai=&brain;Unit boss;\n'
                '#define AI_VALUE(T,N) (&boss)\nbool IsOnyxiaAirCombatTarget(AI*a,Unit*){return a->air;}\n'
                'bool backoff(){'+guard+'return true;}\nint main(){assert(!backoff());brain.air=false;assert(backoff());}')

    def test_aoe_requires_nearby_whelp_cluster(self):
        source=(ROOT/'ai/playerbot/strategy/actions/OnyxiasLairDungeonActions.cpp').read_text()
        if 'bool ai::SuppressOnyxiaAirAoe(' not in source:self.fail('No airborne single-target preference')
        fn=function(source,'bool ai::SuppressOnyxiaAirAoe(').replace('ai::SuppressOnyxiaAirAoe','SuppressOnyxiaAirAoe')
        run(r'''
#include <cassert>
#include <list>
using uint32=unsigned;using ObjectGuid=int;
unsigned clockMs=1000;struct WorldTimer{static unsigned getMSTime(){return clockMs;}};
enum class ActionThreatType{ACTION_THREAT_AOE,ACTION_THREAT_SINGLE};
struct Action{virtual ~Action(){};ActionThreatType type=ActionThreatType::ACTION_THREAT_AOE;ActionThreatType GetThreatType(){return type;}};
struct CastHealingSpellAction:Action{};
struct Unit{unsigned entry=11262;bool alive=true,combat=true,cc=false;float dist=10;unsigned GetEntry(){return entry;}bool IsAlive(){return alive;}bool IsInMap(Unit*){return true;}bool IsInCombat(){return combat;}float GetDistance(Unit*u){return u->dist;}};
struct OnyxiaAirCombatState{unsigned aoeAt=0,aoeWhelps=0;};
struct AiObjectContext{OnyxiaAirCombatState state;Unit current;template<class T>T value(){if constexpr(__is_same(T,OnyxiaAirCombatState&))return state;else return &current;}};
#define AI_VALUE(T,N) context->value<T>()
struct PlayerbotAI{bool active=true;Unit bot,adds[4];AiObjectContext context;Unit*GetBot(){return &bot;}Unit*GetUnit(int i){return &adds[i];}AiObjectContext*GetAiObjectContext(){return &context;}};
bool OnyxiaRangedFocusEnabled(PlayerbotAI*a){return a->active;}bool IsOnyxiaAirCombatTarget(PlayerbotAI*a,Unit*){return a->active;}
bool IsRaidCrowdControlMarkedTarget(PlayerbotAI*,Unit*u){return u->cc;}
std::list<int> cluster;struct AoeCountValue{static std::list<int>FindMaxDensity(Unit*){return cluster;}};
'''+fn+r'''
int main(){PlayerbotAI a;Action aoe;assert(SuppressOnyxiaAirAoe(&a,&aoe));
cluster={0,1};clockMs+=1000;assert(SuppressOnyxiaAirAoe(&a,&aoe));
cluster={0,1,2};clockMs+=1000;assert(!SuppressOnyxiaAirAoe(&a,&aoe));
a.adds[2].dist=40;clockMs+=1000;assert(SuppressOnyxiaAirAoe(&a,&aoe));
a.adds[2].dist=10;a.adds[2].cc=true;clockMs+=1000;assert(SuppressOnyxiaAirAoe(&a,&aoe));
a.active=false;assert(!SuppressOnyxiaAirAoe(&a,&aoe));a.active=true;
aoe.type=ActionThreatType::ACTION_THREAT_SINGLE;assert(!SuppressOnyxiaAirAoe(&a,&aoe));
CastHealingSpellAction heal;assert(!SuppressOnyxiaAirAoe(&a,&heal));
}
''')

THREAT_STUB='\n#include <cassert>\n#include <string>\nusing uint8=unsigned char;\nnamespace BotState {constexpr int BOT_STATE_COMBAT=1;}\nenum class ActionThreatType {ACTION_THREAT_NONE,ACTION_THREAT_SINGLE,ACTION_THREAT_AOE};\nstruct Unit;struct Action {Unit* target=nullptr;Unit* GetTarget(){return target;}virtual ~Action(){};virtual std::string getName(){return "action";}virtual ActionThreatType GetThreatType(){return ActionThreatType::ACTION_THREAT_AOE;}};\nstruct SingleAction:Action {ActionThreatType GetThreatType() override{return ActionThreatType::ACTION_THREAT_SINGLE;}};\nstruct CastSpellAction:Action {std::string name;std::string getName() override{return name;}};\nstruct CastHealingSpellAction:CastSpellAction {};\nstruct ShieldAction:CastSpellAction {ActionThreatType GetThreatType() override{return ActionThreatType::ACTION_THREAT_NONE;}};\nstruct Unit {virtual ~Unit(){};Unit* GetVictim(){return nullptr;}Unit* ToPlayer(){return nullptr;}unsigned GetObjectGuid(){return 1;}};\nusing Player=Unit;\nstruct Group {bool IsMember(unsigned){return false;}};\nstruct Bot {void SendPlaySpellVisual(int){};Group* GetGroup(){return nullptr;}};\nstruct PlayerbotAI {static bool IsTank(Player*){return false;}};\nnamespace TortoiseBots {namespace BotCommands {bool IsInCommandedGroup(Bot*){return false;}}\nnamespace RemoteCommander {bool CommandedBackoffYields(bool,bool,bool){return false;}}}\nstruct AI {bool air=true,unsafe=true,neglect=false,group=true,high=true;int aoe=100;Bot bot;\n bool HasStrategy(std::string,int){return false;}Bot* GetMaster(){return nullptr;}Bot* GetBot(){return &bot;}\n int value(std::string n){return n=="neglect threat"?neglect:n=="group"?group:n=="aoe"?aoe:n=="high threat"?high:0;}};\nUnit* GetAssignedHuhuranTankTarget(AI*){return nullptr;}\nbool IsOnyxiaAirCombatTarget(AI* a,Unit* u){return a->air&&u;}\nbool RaidHealerShouldHoldDamage(AI* ai){return ai->unsafe;}\ntemplate<class T>T fixtureValue(AI* ai,std::string name){if constexpr(std::is_pointer_v<T>)return nullptr;else return static_cast<T>(ai->value(name));}\n#define AI_VALUE(type,name) fixtureValue<type>(ai,name)\n#define AI_VALUE2(type,name,qualifier) static_cast<type>(ai->value(qualifier))\nstruct Multiplier {AI* ai;Bot* bot=ai->GetBot();float GetValue(Action* action) BODY};\n'
if __name__=='__main__':unittest.main()
