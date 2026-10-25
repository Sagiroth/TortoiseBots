"""Run the production fixed-bomb Execute method with deterministic path stubs.

Exercises the launched spline, not a copy of the movement decisions. This is
not a navmesh or live-server test.
"""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest
from tools.test_geddon_bomb_wiring import function


class BombMovement(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_production_spline(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root/'ai/playerbot/strategy/actions/GeddonBombActions.cpp').read_text()
        movement_root = pathlib.Path(os.environ.get('TBOTS_MOVEMENT_SOURCE', root))
        movement_source = (movement_root/'ai/playerbot/strategy/actions/GeddonBombActions.cpp').read_text()
        code = r'''
#include <cassert>
#include <algorithm>
#include <list>
#include <vector>
#include <string>
#include <utility>
#include <iostream>
'''
        for name in ['GeddonBombPolicy', 'RaidBombRunoutPolicy', 'policy/raid/GeddonInfernoPolicy']:
            code += f'#include "{root}/ai/playerbot/{name}.h"\n'
        code += r'''
using namespace ai;
using ObjectGuid=unsigned;using SpellEffectIndex=int;
struct DynamicObject;
constexpr int UNIT_STAND_STATE_STAND=0,UNIT_STAT_ROOT=1;
struct Unit {
 float x=0,y=0,z=0,reach=2;unsigned entry=12056;bool alive=true,combat=true,inferno=false;
 float GetPositionX(){return x;}float GetPositionY(){return y;}float GetPositionZ(){return z;}
 float GetCombatReach(){return reach;}unsigned GetEntry(){return entry;}
 bool IsAlive(){return alive;}bool IsInCombat(){return combat;}bool IsInMap(Unit*){return true;}
 bool HasAura(unsigned){return inferno;}bool HasUnitState(int){return false;}
};
struct Player:Unit {
 bool attacking=true,casting=true,channel=true,autorepeat=true;unsigned guid=1;
 std::vector<DynamicObject*> areas;
 bool GetTransport(){return false;}bool IsTaxiFlying(){return false;}
 void AttackStop(){attacking=false;}
 bool IsNonMeleeSpellCasted(bool,bool,bool){return casting||channel;}
 void InterruptNonMeleeSpells(bool){casting=channel=autorepeat=false;}
 bool IsInWorld(){return true;}unsigned GetObjectGuid(){return guid;}
 void GetDynObjects(unsigned spell,int effect,std::vector<DynamicObject*>& out){assert(spell==44060&&effect==0);out=areas;}
 void HandleEmoteState(int){}void SetStandState(int){}
 unsigned GetMapId(){return 409;}float GetCollisionHeight(){return 2;}
 bool IsWithinLOS(float,float,float){return true;}int GetGroup(){return 1;}
};
struct DynamicObject:Unit {
 unsigned caster=1,spell=44060,effect=0;float radius=5;int duration=60000;
 unsigned GetCasterGuid(){return caster;}unsigned GetSpellId(){return spell;}unsigned GetEffIndex(){return effect;}
 float GetRadius(){return radius;}int GetDuration(){return duration;}
};
std::vector<Player*> members;
std::vector<Player*> LiveGroupMembers(int){return members;}
int pathMode=0;
struct WorldPosition {
 float x=0,y=0,z=0;
 WorldPosition()=default;WorldPosition(unsigned,float a,float b,float c):x(a),y(b),z(c){}
 WorldPosition(Unit* p):x(p->x),y(p->y),z(p->z){}
 float getX()const{return x;}float getY()const{return y;}float getZ()const{return z;}
 void setZ(float a){z=a;}float GetHeight(){return z;}bool isValid()const{return true;}
 float distance(WorldPosition const& p)const{return GeddonBombDistance({x,y,z},{p.x,p.y,p.z});}
 bool isPathTo(std::vector<WorldPosition> const& p,float range=1,float height=1)const{
 return !p.empty()&&distance(p.back())<range&&std::fabs(z-p.back().z)<height;}
 std::vector<WorldPosition> GetPathTo(WorldPosition p,Player*)const{
 if(pathMode==1)return {};if(pathMode==2)return {{409,100,100,z},p};return {p};}
 std::vector<WorldPosition> toPointsArray(std::vector<WorldPosition> const& p){return p;}
};
using HazardPosition=std::pair<WorldPosition,float>;
struct LastMovement{std::vector<WorldPosition> lastPath{{409,99,99,0}};};
std::list<HazardPosition> hazards;std::list<ObjectGuid> targets;LastMovement last;
template<class T>T value(std::string n){
 if constexpr(std::is_same_v<T,LastMovement&>)return last;
 else if constexpr(std::is_same_v<T,std::list<HazardPosition>>)return hazards;
 else return targets;
}
#define AI_VALUE(T,N) value<T>(N)
struct PlayerbotAI {
 Player* bot;Unit* boss;GeddonBombState state;bool movable=true;int stops=0,errors=0;
 bool CanMove(){return movable;}void InterruptSpell(bool){bot->casting=false;}
 void StopMoving(){++stops;}void* GetMaster(){return nullptr;}
 void TellError(void*,std::string){++errors;}Unit* GetUnit(ObjectGuid){return boss;}
};
GeddonBombPoint Point(WorldPosition const& p){return {p.x,p.y,p.z};}
GeddonBombState& State(PlayerbotAI* ai){return ai->state;}
bool UpdateGeddonBombMovement(PlayerbotAI* ai){return ai->state.Active();}
struct {float aggroDistance=20;void logEvent(PlayerbotAI*,std::string,std::string,std::string){}}sPlayerbotAIConfig;
std::vector<WorldPosition> launched;int launches=0;
namespace Movement {
struct MoveSplineInit {
 MoveSplineInit(Player&,char const*){}void MovebyPath(std::vector<WorldPosition> const& p){launched=p;}
 void SetWalk(bool walk){assert(!walk);}unsigned Launch(){++launches;return 1;}
};
}
struct Event{};
struct GeddonBombMoveAction {
 PlayerbotAI* ai;Player* bot;bool Execute(Event&);
 void SetDuration(unsigned){}bool IsMovingAllowed(unsigned,float,float,float){return true;}
 bool IsHazardNearPosition(WorldPosition const& p){for(auto const& h:hazards)if(p.distance(h.first)<=h.second)return true;return false;}
 void ClearIdleState(){}
};
'''
        code += function(source, 'std::list<HazardPosition> MoltenGround(')
        code += '\n'+function(movement_source, 'bool GeddonBombMoveAction::Execute(')
        code += r'''
int main(){
 Player bot,raid;bot.x=4;raid.x=0;raid.y=-5;members={&bot,&raid};Unit boss;
 PlayerbotAI ai{&bot,&boss};GeddonBombMoveAction move{&ai,&bot};Event event;
 ai.state.Arm({0,30,0},1,1);ai.state.Observe(true,1,{4,0,0},0);targets={1};
 assert(move.Execute(event)); // far-side melee takes checked boss detour
 assert(launches==1&&!bot.attacking&&!bot.casting&&!bot.channel&&!bot.autorepeat&&ai.stops==1);
 assert(launched.size()>2&&launched.front().x==4&&launched.back().y==30);
 assert(last.lastPath.empty()); // cannot substitute stale route through raid
 bot.x=0;bot.y=30;ai.state.Observe(true,1,{0,30,0},7000);
 bot.channel=bot.autorepeat=true;
 assert(move.Execute(event)&&launches==1&&!bot.channel&&!bot.autorepeat); // stationary cancellation
 ai.state.Observe(false,0,{0,30,0},8100);
 assert(move.Execute(event)&&launches==2&&launched.back().x==4&&launched.back().y==0);
 boss.inferno=true;int previous=launches;
 assert(move.Execute(event)&&launches==previous); // safe hold, no return into Inferno
 DynamicObject fresh;fresh.x=bot.x;fresh.y=bot.y;bot.areas={&fresh};
 assert(move.Execute(event)&&launches==previous+1&&ai.state.phase==GeddonBombPhase::Returning);
 assert(GeddonBombDistance(ai.state.waitingPoint,{fresh.x,fresh.y,fresh.z})>7);
 assert(ai.state.home.x==4&&ai.state.home.y==0); // do not replace return destination
 bot.areas.clear();previous=launches;
 boss.inferno=false;targets.clear();bot.x=4;bot.y=0;
 ai.state.Clear();ai.state.Arm({0,30,0},1,1);ai.state.Observe(true,2,{4,0,0},10000);
 raid.x=0;raid.y=29;
 assert(!move.Execute(event)&&launches==previous); // occupied bomb point
 raid.y=-5;pathMode=1;bot.channel=bot.autorepeat=true;
 assert(!move.Execute(event)&&launches==previous&&!bot.channel&&!bot.autorepeat); // incomplete route
 pathMode=2;
 assert(!move.Execute(event)&&launches==previous); // >50 yd detour
 pathMode=0;hazards={{{409,2,15,0},3}};
 assert(move.Execute(event)); // a nearby point may route around the hazard
 for(size_t i=1;i<launched.size();++i)
 assert(RaidRunoutSegmentDistanceSquared({launched[i-1].x,launched[i-1].y},{launched[i].x,launched[i].y},{2,15})>=9);
 previous=launches;
 hazards.clear();ai.movable=false;bot.channel=bot.autorepeat=true;
 assert(!move.Execute(event)&&launches==previous&&!bot.channel&&!bot.autorepeat); // roots do not bypass core move gate
 ai.movable=true;
 assert(move.Execute(event));
 // Real cast ownership: fire is registered on the carrier, not the boss.
 DynamicObject fire;fire.x=0;fire.y=30;raid.guid=7;fire.caster=7;raid.areas={&fire};
 auto collected=MoltenGround(&bot);assert(collected.size()==1&&collected.front().second==7);
 ai.state.hasWaitingPoint=false;
 assert(move.Execute(event));
 auto const chosen=ai.state.waitingPoint;
 assert(GeddonBombDistance(chosen,ai.state.spot)<=12.01f);
 assert(GeddonBombDistance(chosen,{0,30,0})>7);
 assert(GeddonBombDistance(chosen,{raid.x,raid.y,raid.z})>=22);
 for(size_t i=1;i<launched.size();++i)
 assert(RaidRunoutSegmentDistanceSquared({launched[i-1].x,launched[i-1].y},{launched[i].x,launched[i].y},{0,30})>=49);
 // Expired and foreign spell/owner objects must not block the marker.
 fire.duration=0;assert(MoltenGround(&bot).empty());fire.duration=60000;
 fire.spell=1;assert(MoltenGround(&bot).empty());fire.spell=44060;
 fire.caster=1;assert(MoltenGround(&bot).empty());fire.caster=7;
 // Fully covered bounded area reports failure; never drift farther away.
 fire.radius=30;ai.state.hasWaitingPoint=false;previous=launches;
 assert(!move.Execute(event)&&launches==previous);
 raid.areas.clear();boss.combat=false;boss.x=0;boss.y=30;targets={1};
 assert(!move.Execute(event)&&launches==previous); // known unpulled pack covers bomb area
 std::cout<<"Production fixed spline, melee detour, wait/return, hazard/Inferno/path guards: PASS\n";
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp = pathlib.Path(tmp)/'fixture.cpp'
            exe = pathlib.Path(tmp)/'fixture'
            cpp.write_text(code)
            subprocess.run(['g++', '-std=c++17', str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
