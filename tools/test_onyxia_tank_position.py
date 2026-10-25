"""Production tank selector, movement guard and launched path with core doubles.

TBOTS_POSITION_SOURCE selects the pre-patch tree. These are deterministic
path fixtures, not a navmesh/server simulation.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


def function(source, marker):
    start = source.index(marker)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def fixture(root):
    source_path = root/'ai/playerbot/strategy/actions/OnyxiaTankPositionActions.cpp'
    source = source_path.read_text() if source_path.exists() else ''
    code = r'''
#include <algorithm>
#include <cassert>
#include <list>
#include <map>
#include <string>
#include <type_traits>
#include "playerbot/OnyxiaTankPositionPolicy.h"
using namespace ai;
using ObjectGuid=unsigned;
enum class BotState {BOT_STATE_COMBAT};
constexpr int CHASE_MOTION_TYPE=1;
struct Motion {int kind=0;int GetCurrentMovementGeneratorType(){return kind;}};
struct Unit {
 unsigned entry=10184,map=249,instance=1;float x=-25,y=-215,z=-87;
 bool alive=true,combat=true,flying=false,aura=false,valid=true;
 Unit* victim=nullptr;Motion motion;bool stopped=true;
 unsigned GetEntry(){return entry;}unsigned GetMapId(){return map;}
 bool IsAlive(){return alive;}bool IsInWorld(){return true;}bool IsInCombat(){return combat;}
 bool IsBeingTeleported(){return false;}bool IsFlying(){return flying;}
 bool HasAura(unsigned id){assert(id==17131);return aura;}
 bool IsInMap(Unit* p){return map==p->map&&instance==p->instance;}
 Unit* GetVictim(){return victim;}float GetPositionX(){return x;}float GetPositionY(){return y;}float GetPositionZ(){return z;}
 bool GetTransport(){return false;}bool IsTaxiFlying(){return false;}
 Motion* GetMotionMaster(){return &motion;}bool IsStopped(){return stopped;}
 void InterruptNonMeleeSpells(bool){}
};
using Player=Unit;
int pathMode=0;bool launchOk=true;float heightOffset=0;
struct WorldPosition {
 float x=0,y=0,z=0;
 WorldPosition()=default;WorldPosition(unsigned,float a,float b,float c):x(a),y(b),z(c){}
 WorldPosition(Unit* p):x(p->x),y(p->y),z(p->z){}
 float getX()const{return x;}float getY()const{return y;}float getZ()const{return z;}
 void setZ(float a){z=a;}float GetHeight(){return z+heightOffset;}
 float distance(WorldPosition const& p)const{return OnyxiaTankDistance({x,y,z},{p.x,p.y,p.z});}
 bool isPathTo(std::vector<WorldPosition> const& p,float r,float h)const{return !p.empty()&&distance(p.back())<r&&std::fabs(z-p.back().z)<h;}
 std::vector<WorldPosition> GetPathTo(WorldPosition end,Player*)const{
 if(pathMode==1)return {*this,{249,-30,-254.463f,-89},end};
 if(pathMode==2)return {*this,{249,-30,-177.106f,-89},end};
 if(pathMode==3)return {*this,{249,0,-215,-86}};
 if(pathMode==4)return {};return {*this,end};}
 std::vector<WorldPosition> toPointsArray(std::vector<WorldPosition> const& p){return p;}
};
using HazardPosition=std::pair<WorldPosition,float>;
struct LastMovement{std::vector<WorldPosition> lastPath{{249,0,0,0}};};
struct Position{bool set=false;bool isSet(){return set;}};
using PositionMap=std::map<std::string,Position>;
struct AiObjectContext {
 PositionMap positions;std::list<ObjectGuid> attackers{1},possible;
 ObjectGuid order=0;Unit* rti=nullptr;Unit* current=nullptr;
 std::list<HazardPosition> hazards;LastMovement last;
};
struct PlayerbotAI {
 Player* bot;Unit* boss;Unit* other;bool enabled=true,tank=true,movable=true;std::string hold;
 AiObjectContext context;int stops=0,errors=0;
 Player* GetBot(){return bot;}AiObjectContext* GetAiObjectContext(){return &context;}
 Unit* GetUnit(ObjectGuid id){return id==1?boss:id==2?other:nullptr;}
 bool IsTank(Player*){return tank;}
 bool HasStrategy(std::string s,BotState){return s=="onyxia landing tank"?enabled:s==hold;}
 bool CanMove(){return movable;}void StopMoving(){++stops;bot->motion.kind=0;bot->stopped=true;}
 Player* GetMaster(){return nullptr;}void TellError(Player*,std::string){++errors;}
};
template<class T>T value(AiObjectContext* c,std::string n){
 if constexpr(std::is_same_v<T,PositionMap&>)return c->positions;
 else if constexpr(std::is_same_v<T,std::list<ObjectGuid>>)return n=="attackers"?c->attackers:c->possible;
 else if constexpr(std::is_same_v<T,std::list<HazardPosition>>)return c->hazards;
 else if constexpr(std::is_same_v<T,LastMovement&>)return c->last;
 else if constexpr(std::is_same_v<T,ObjectGuid>)return c->order;
 else return n=="rti target"?c->rti:c->current;
}
#define AI_VALUE(T,N) value<T>(context,N)
struct {float sightDistance=60,aggroDistance=20;void logEvent(PlayerbotAI*,std::string,std::string,std::string){}}sPlayerbotAIConfig;
struct WorldTimer{static unsigned getMSTime(){return 100;}};
struct PullStrategy{static PullStrategy*Get(PlayerbotAI*){return nullptr;}bool IsCommandActive(){return false;}bool IsCommandPullback(){return false;}Unit*GetTarget(){return nullptr;}};
struct PossibleAttackTargetsValue{static bool IsValid(Unit* u,Player*,float,bool,bool){return u->valid;}};
namespace ai {Unit* GetOnyxiaTankPositionTarget(PlayerbotAI*);}
std::vector<WorldPosition> launched;
namespace Movement {struct MoveSplineInit {
 MoveSplineInit(Player&,char const*){}void MovebyPath(std::vector<WorldPosition> const& p){launched=p;}
 void SetWalk(bool walk){assert(!walk);}unsigned Launch(){return launchOk;}
};}
struct Action {std::string name;Action(std::string n):name(n){}virtual ~Action()=default;std::string const& getName(){return name;}};
struct MovementAction:Action {using Action::Action;static bool isUseful(){return true;}};
struct CastReachTargetSpellAction:Action {using Action::Action;};
struct Event{};
struct OnyxiaTankPositionAction:MovementAction {
 PlayerbotAI* ai;Player* bot;AiObjectContext* context;bool blockedReported=false,movingAllowed=true,active=false,crossed=false,crossing=false,awaiting=false;unsigned lastUpdate=0;size_t waypoint=0;std::vector<WorldPosition> openingPath;
 OnyxiaTankPositionAction(PlayerbotAI* a):MovementAction("onyxia tank position"),ai(a),bot(a->bot),context(&a->context){}
 bool isUseful();bool Execute(Event&);bool IsMovingAllowed(unsigned,float,float,float){return movingAllowed;}
 void SetDuration(unsigned){}void ClearIdleState(){}
};
struct OnyxiaTankPositionMultiplier {PlayerbotAI* ai;float GetValue(Action*);};
'''
    if source:
        for marker in ['Unit* ai::GetOnyxiaTankPositionTarget(', 'bool OnyxiaTankPositionAction::isUseful()',
                       'bool OnyxiaTankPositionAction::Execute(', 'float OnyxiaTankPositionMultiplier::GetValue(']:
            code += '\n'+function(source, marker)
    else:
        # Old strategy has no positioning action/multiplier. Its ordinary
        # chase is permitted and it never launches a fixed-spot spline.
        code += '''
Unit* ai::GetOnyxiaTankPositionTarget(PlayerbotAI*){return nullptr;}
bool OnyxiaTankPositionAction::isUseful(){return false;}
bool OnyxiaTankPositionAction::Execute(Event&){return false;}
float OnyxiaTankPositionMultiplier::GetValue(Action*){return 1;}
'''
    code += r'''
int main(int argc,char** argv){
 Player bot;Unit boss,other;boss.x=-60;other.entry=11262;PlayerbotAI ai{&bot,&boss,&other};ai.context.current=&boss;
 OnyxiaTankPositionAction action(&ai);OnyxiaTankPositionMultiplier guard{&ai};Event event;
 std::string test=argc>1?argv[1]:"movement";
 if(test=="guard"){
 MovementAction chase("reach melee"),face("dragon tank face away"),tankface("tank face away"),hazard("move away from hazard"),flee("flee"),facing("set facing");
 CastReachTargetSpellAction charge("charge");Action melee("melee"),taunt("taunt"),cast("shield slam");
 for(Action* blocked:std::vector<Action*>{&chase,&face,&tankface,&charge})assert(guard.GetValue(blocked)==0);
 for(Action* allowed:std::vector<Action*>{&action,&hazard,&flee,&facing,&melee,&taunt,&cast})assert(guard.GetValue(allowed)==1);
 boss.flying=true;assert(guard.GetValue(&chase)==1);boss.flying=false;
 ai.enabled=false;assert(guard.GetValue(&chase)==1);return 0;}
 if(test=="scope"){
 assert(GetOnyxiaTankPositionTarget(&ai)==&boss);
 ai.context.order=1;assert(GetOnyxiaTankPositionTarget(&ai)==&boss);
 ai.context.order=2;assert(!GetOnyxiaTankPositionTarget(&ai));ai.context.order=0;
 ai.context.rti=&other;assert(!GetOnyxiaTankPositionTarget(&ai));ai.context.rti=nullptr;
 for(auto hold:{"stay","follow","wait for attack","grind"}){ai.hold=hold;assert(!GetOnyxiaTankPositionTarget(&ai));}ai.hold="";
 ai.context.positions["pull hold"].set=true;assert(!GetOnyxiaTankPositionTarget(&ai));ai.context.positions.clear();
 boss.flying=true;assert(!GetOnyxiaTankPositionTarget(&ai));boss.flying=false;
 boss.aura=true;assert(!GetOnyxiaTankPositionTarget(&ai));boss.aura=false;
 boss.combat=false;assert(!GetOnyxiaTankPositionTarget(&ai));boss.combat=true;
 boss.instance=2;assert(!GetOnyxiaTankPositionTarget(&ai));boss.instance=1;
 bot.map=1;assert(!GetOnyxiaTankPositionTarget(&ai));bot.map=249;
 ai.tank=false;assert(!GetOnyxiaTankPositionTarget(&ai));ai.tank=true;
 ai.enabled=false;assert(!GetOnyxiaTankPositionTarget(&ai));ai.enabled=true;
 ai.context.current=&other;assert(GetOnyxiaTankPositionTarget(&ai)==&boss); // assignment survives threat loss/temporary add target
 assert(!GetOnyxiaTankPositionTarget(nullptr));return 0;}
 assert(action.isUseful());assert(action.Execute(event));assert(action.openingPath.size()==2);assert(action.active);assert(!action.isUseful());action.active=false;launched=action.openingPath;
 assert(std::fabs(launched.back().x-kOnyxiaTankSpot.x)<0.01f);assert(ai.context.last.lastPath.empty());
 for(pathMode=1;pathMode<=4;++pathMode){launched.clear();bot.motion.kind=1;bot.stopped=false;
 assert(!action.Execute(event));assert(launched.empty());assert(bot.motion.kind==0);}pathMode=0;
 ai.context.hazards.push_back({{249,0,-216,-86},5});launched.clear();assert(!action.Execute(event));assert(launched.empty());ai.context.hazards.clear();
 other.combat=false;other.x=0;other.y=-216;other.z=-86;ai.context.possible={2};assert(!action.Execute(event));assert(launched.empty());ai.context.possible.clear();
 heightOffset=50;assert(!action.Execute(event));assert(launched.empty());heightOffset=0;
 action.movingAllowed=false;assert(!action.Execute(event));action.movingAllowed=true;
 ai.movable=false;assert(!action.Execute(event));ai.movable=true;

 bot.x=kOnyxiaTankSpot.x;bot.y=kOnyxiaTankSpot.y;bot.z=kOnyxiaTankSpot.z;bot.motion.kind=1;
 assert(action.isUseful());assert(action.Execute(event));assert(bot.motion.kind==0);assert(!action.isUseful());
}
'''
    return code


class OnyxiaTankPositionTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_production_action(self):
        own = Path(__file__).resolve().parents[1]
        root = Path(os.environ.get('TBOTS_POSITION_SOURCE', own))
        with tempfile.TemporaryDirectory() as tmp:
            cpp, exe = Path(tmp)/'position.cpp', Path(tmp)/'position'
            cpp.write_text(fixture(root))
            subprocess.run(['g++', '-std=c++17', '-I'+str(own/'ai'), str(cpp), '-o', str(exe)], check=True)
            for case in ['guard', 'scope', 'movement']:
                with self.subTest(case=case):
                    subprocess.run([str(exe), case], check=True)

    def test_dispatch_wiring(self):
        root = Path(os.environ.get('TBOTS_POSITION_SOURCE', Path(__file__).resolve().parents[1]))
        source = (root/'ai/playerbot/strategy/generic/OnyxiasLairDungeonStrategies.cpp').read_text()
        for engine in ['Combat', 'Reaction']:
            self.assertIn('onyxia tank position', function(source, f'void OnyxiaLandingTankStrategy::Init{engine}Triggers('))
            self.assertIn('OnyxiaTankPositionMultiplier', function(source, f'void OnyxiaLandingTankStrategy::Init{engine}Multipliers('))
        self.assertIn('creators["onyxia tank position"]', (root/'ai/playerbot/strategy/actions/ActionContext.h').read_text())
        self.assertIn('creators["onyxia tank position needed"]', (root/'ai/playerbot/strategy/triggers/TriggerContext.h').read_text())


if __name__ == '__main__':
    unittest.main()
