"""Compile actual fissure path validation and dispatch with movement seams."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


def function(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


class FissureDispatchTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_route_and_dispatch(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / 'ai/playerbot/strategy/actions/KelThuzadFissureActions.cpp').read_text()
        fixture = (root / 'ai/playerbot/KelThuzadFissurePolicy.h').read_text().replace('#pragma once', '')
        fixture += r'''
#include <cassert>
#include <list>
#include <type_traits>
using namespace ai;
constexpr float M_PI_F=3.141592654f;
constexpr unsigned UNIT_STAND_STATE_STAND=0;
struct Player {
    float x=0,y=0,z=0; bool transport=false,taxi=false;
    void HandleEmoteState(int){} unsigned GetStandState(){return 0;}
    void SetStandState(unsigned){} bool IsNonMeleeSpellCasted(bool,bool,bool){return false;}
    bool GetTransport(){return transport;} bool IsTaxiFlying(){return taxi;}
    float GetCollisionHeight(){return 2;} bool IsWithinLOS(float,float,float){return true;}
};
struct WorldPosition {
    float x=0,y=0,z=0;
    WorldPosition()=default;
    WorldPosition(Player* p):x(p->x),y(p->y),z(p->z){}
    WorldPosition(unsigned,float a,float b,float c):x(a),y(b),z(c){}
    float getX()const{return x;} float getY()const{return y;} float getZ()const{return z;}
    void setZ(float v){z=v;} float GetHeight(){return 0;}
    WorldPosition operator+(WorldPosition b)const{return {533,x+b.x,y+b.y,z+b.z};}
    float distance(WorldPosition b)const{return std::hypot(std::hypot(x-b.x,y-b.y),z-b.z);}
    std::vector<WorldPosition> GetPathTo(WorldPosition,Player*)const;
    bool isPathTo(std::vector<WorldPosition> const& p)const{return !p.empty();}
    std::vector<WorldPosition> toPointsArray(std::vector<WorldPosition> const& p){return p;}
};
using HazardPosition=std::pair<WorldPosition,float>;
std::list<HazardPosition> hazards;
std::vector<WorldPosition> dispatched;
bool unsafeMesh=false, excessiveLength=false, shiftAtStop=false, launchFails=false;
std::vector<WorldPosition> WorldPosition::GetPathTo(WorldPosition end,Player*)const {
    if(unsafeMesh) return {*this,{533,-10,0,0},end};
    if(excessiveLength) return {*this,{533,0,30,0},end};
    return {*this,end};
}
struct LastMovement {std::vector<int> lastPath{1,2};};
LastMovement last;
template<class T> T& value(){
    if constexpr(std::is_same_v<T,LastMovement&>) return last;
    else return hazards;
}
#define AI_VALUE(type,key) value<type>()
struct PlayerbotAI {
    Player* bot; bool canMove=true,inside=true;
    bool CanMove(){return canMove;} void InterruptSpell(bool){}
    void StopMoving(){if(shiftAtStop) bot->x=4;}
};
bool InsideKelThuzadFissure(PlayerbotAI* ai){return ai->inside;}
std::list<HazardPosition> GetKelThuzadFissures(PlayerbotAI*){return hazards;}
struct Event{};
struct KelThuzadFissureEscapeAction {
    Player* bot; PlayerbotAI* ai; bool allowed=true;
    bool Execute(Event&);
    bool IsMovingAllowed(unsigned,float,float,float){return allowed;}
    bool IsHazardNearPosition(WorldPosition){return false;}
    void ClearIdleState(){} void WaitForReach(std::vector<WorldPosition> const&){}
};
namespace Movement {
struct MoveSplineInit {
    std::vector<WorldPosition> points;
    MoveSplineInit(Player&,const char*){}
    void MovebyPath(std::vector<WorldPosition> const& p){points=p;}
    void SetWalk(bool v){assert(!v);}
    unsigned Launch(){if(launchFails)return 0;dispatched=points;return 100;}
};
}
'''
        fixture += function(source, 'bool FissurePathSafe(')
        fixture += function(source, 'bool KelThuzadFissureEscapeAction::Execute(')
        fixture += r'''
int main(){
    Player bot; PlayerbotAI ai{&bot}; Event event;
    KelThuzadFissureEscapeAction action{&bot,&ai};
    hazards={{{533,0,0,0},7}};
    assert(action.Execute(event)); assert(!dispatched.empty());
    assert(FissurePathSafe(WorldPosition(&bot),dispatched,hazards));
    assert(last.lastPath.empty()); dispatched.clear();
    // Starting off-center, crossing inward through the blast must be rejected.
    bot.x=3; unsafeMesh=true;
    assert(!action.Execute(event)); assert(dispatched.empty()); unsafeMesh=false;
    excessiveLength=true; assert(!action.Execute(event)); excessiveLength=false;
    // A stop that changes the start must be checked again before dispatch.
    bot.x=0; shiftAtStop=true;
    assert(!action.Execute(event)); assert(dispatched.empty()); shiftAtStop=false;
    bot.x=0; action.allowed=false; assert(!action.Execute(event)); action.allowed=true;
    ai.canMove=false; assert(!action.Execute(event)); ai.canMove=true;
    bot.transport=true; assert(!action.Execute(event)); bot.transport=false;
    ai.inside=false; assert(!action.Execute(event)); ai.inside=true;
    launchFails=true; last.lastPath={1}; assert(!action.Execute(event)); assert(!last.lastPath.empty());
    std::vector<WorldPosition> endInside{{533,1,0,0}};
    assert(!FissurePathSafe(WorldPosition(&bot),endInside,hazards));
    std::vector<WorldPosition> wrongFloor{{533,8,0,4}};
    assert(!FissurePathSafe(WorldPosition(&bot),wrongFloor,hazards));
    // A fissure four yards above can still hit within its five-yard sphere.
    hazards={{{533,0,0,4},7}};
    assert(!FissurePathSafe(WorldPosition(&bot),{{533,2,0,0}},hazards));
    // The route can rise into a hazard outside the start's vertical range.
    hazards={{{533,0,0,0},7},{{533,4,0,7.5f},7}};
    assert(!FissurePathSafe(WorldPosition(&bot),{{533,0,0,0},{533,4,0,3},{533,8,0,0}},hazards));
    hazards={{{533,0,0,0},7},{{533,4,0,-7.5f},7}};
    assert(!FissurePathSafe(WorldPosition(&bot),{{533,0,0,0},{533,4,0,-3},{533,8,0,0}},hazards));
    // An elevated hazard is not a containing hazard eligible for outward escape.
    hazards={{{533,0,0,0},7},{{533,0,0,7.5f},7}};
    assert(!FissurePathSafe(WorldPosition(&bot),{{533,1,0,3},{533,8,0,0}},hazards));
    // Outward XY motion must not move deeper vertically into a containing sphere.
    hazards={{{533,0,0,0},7},{{533,0,0,6},7}};
    assert(!FissurePathSafe(WorldPosition(&bot),{{533,1,0,3},{533,8,0,0}},hazards));
    // A hazard entirely beyond the allowed height band remains irrelevant.
    hazards={{{533,0,0,0},7},{{533,4,0,11},7}};
    assert(FissurePathSafe(WorldPosition(&bot),{{533,0,0,0},{533,4,0,3},{533,8,0,0}},hazards));
    hazards={{{533,0,0,0},7}};
    hazards.push_back({{533,8,0,0},7});
    assert(!FissurePathSafe(WorldPosition(&bot),{{533,16,0,0}},hazards));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / 'dispatch.cpp'
            binary = pathlib.Path(tmp) / 'dispatch'
            src.write_text(fixture)
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(src), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
