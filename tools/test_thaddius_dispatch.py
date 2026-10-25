"""Compile the production action with movement seams; no server/navmesh simulation."""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest


def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


class ThaddiusDispatchTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_checked_route_is_the_launched_route(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = pathlib.Path(os.environ.get('THADDIUS_SOURCE', root / 'ai/playerbot/strategy/actions/ThaddiusPolarityActions.cpp')).read_text()
        policy = (root / 'ai/playerbot/policy/raid/ThaddiusPolarityPolicy.h').read_text().replace('#pragma once', '')
        fixture = policy + r'''
#include <cassert>
#include <vector>
#include <string>
using namespace ai;
struct Unit { float x=0,y=0; float GetPositionX(){return x;} float GetPositionY(){return y;} };
struct Player;
struct Ref { Player* p; Player* getSource() const {return p;} };
struct Map { using PlayerList=std::vector<Ref>; PlayerList players; const PlayerList& GetPlayers(){return players;} };
constexpr unsigned UNIT_STAND_STATE_STAND=0;
struct Player : Unit {
    void HandleEmoteState(int){}
    unsigned GetStandState(){return 0;}
    void SetStandState(unsigned){}
    bool IsNonMeleeSpellCasted(bool,bool,bool){return false;}
    Map map;
    bool transport=false, taxi=false;
    ThaddiusPolaritySide side=ThaddiusPolaritySide::Positive;
    unsigned GetMapId(){return 533;}
    Map* GetMap(){return &map;}
    bool IsAlive(){return true;}
    bool GetTransport(){return transport;}
    bool IsTaxiFlying(){return taxi;}
    float GetCollisionHeight(){return 2;}
    bool IsWithinLOS(float,float,float){return true;}
};
struct WorldPosition {
    unsigned map=533; float x=0,y=0,z=0;
    WorldPosition()=default;
    WorldPosition(Player* p):x(p->x),y(p->y){}
    WorldPosition(unsigned m,float a,float b,float c):map(m),x(a),y(b),z(c){}
    unsigned GetMapId()const{return map;}
    float getX()const{return x;} float getY()const{return y;} float getZ()const{return z;}
    void setZ(float a){z=a;} float GetHeight(){return 0;}
    std::vector<WorldPosition> GetPathTo(WorldPosition,Player*)const;
    bool isPathTo(const std::vector<WorldPosition>& p)const{return !p.empty();}
    std::vector<WorldPosition> toPointsArray(const std::vector<WorldPosition>& p){return p;}
};
std::vector<WorldPosition> preliminary, cachedOrRewritten, dispatched;
std::vector<WorldPosition> WorldPosition::GetPathTo(WorldPosition,Player*)const{return preliminary;}
ThaddiusPolaritySide GetSide(Player* p){return p->side;}
struct LastMovement {std::vector<int> lastPath;};
LastMovement last;
#define AI_VALUE(type,key) last
struct PlayerbotAI {void StopMoving(){} void InterruptSpell(bool){} };
Unit boss;
Unit* GetActiveThaddius(PlayerbotAI*){return &boss;}
struct Event {};
struct ThaddiusPolarityStackAction {
    Player* bot; PlayerbotAI* ai; bool allowed=true;
    bool Execute(Event&);
    bool IsHazardNearPosition(WorldPosition){return false;}
    bool IsReaction(){return false;}
    bool IsMovingAllowed(unsigned,float,float,float){return allowed;}
    bool MoveTo(unsigned,float,float,float,bool,bool,bool,bool) {
        // Generic movement may replace the previously checked route.
        dispatched=cachedOrRewritten; return true;
    }
    void ClearIdleState(){}
    void WaitForReach(const std::vector<WorldPosition>&){}
};
namespace Movement {
struct MoveSplineInit {
    std::vector<WorldPosition> points;
    MoveSplineInit(Player&,const char*){}
    void MovebyPath(const std::vector<WorldPosition>& p){points=p;}
    void SetWalk(bool walk){assert(!walk);}
    unsigned Launch(){dispatched=points;return 100;}
};
}
'''
        fixture += function(source, 'bool PathClearsOppositeCharges(')
        fixture += function(source, 'bool ThaddiusPolarityStackAction::Execute(')
        fixture += r'''
int main() {
    Player bot, opposite; PlayerbotAI ai; Event event;
    auto p=GetThaddiusPolarityPoint(0,0,ThaddiusPolaritySide::Positive);
    auto n=GetThaddiusPolarityPoint(0,0,ThaddiusPolaritySide::Negative);
    bot.x=p.x+30; bot.y=p.y;
    opposite.x=n.x; opposite.y=n.y; opposite.side=ThaddiusPolaritySide::Negative;
    bot.map.players={{&bot},{&opposite}};
    WorldPosition start(&bot), end(533,p.x,p.y,0), danger(&opposite);
    preliminary={start,end}; cachedOrRewritten={start,danger,end};
    assert(PathClearsOppositeCharges(&bot,start,preliminary));
    assert(!PathClearsOppositeCharges(&bot,start,cachedOrRewritten));
    ThaddiusPolarityStackAction action{&bot,&ai};
    assert(action.Execute(event));
    assert(!dispatched.empty());
    assert(PathClearsOppositeCharges(&bot,start,dispatched));
    assert(dispatched.back().x==end.x && dispatched.back().y==end.y);
    // An unsafe mesh route must fail without starting another spline.
    preliminary=cachedOrRewritten; dispatched.clear();
    assert(!action.Execute(event)); assert(dispatched.empty());
    // Bypassing MoveTo must retain the ordinary movement/transport gates.
    preliminary={start,end}; action.allowed=false;
    assert(!action.Execute(event)); assert(dispatched.empty());
    action.allowed=true; bot.transport=true;
    assert(!action.Execute(event)); assert(dispatched.empty());
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / 'dispatch.cpp'
            binary = pathlib.Path(tmp) / 'dispatch'
            src.write_text(fixture)
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', str(src), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
