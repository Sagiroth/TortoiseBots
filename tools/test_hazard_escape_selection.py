"""Run the production escape action with deterministic geometry/world stubs."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

from tools.test_nefarian_bone_target import function


class HazardEscapeSelectionTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_containing_hazard_not_nearest_outside_center(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        text = (root / 'ai/playerbot/strategy/actions/DungeonActions.cpp').read_text()
        production = function(text, 'bool MoveAwayFromHazard::Execute(')
        production += '\n' + function(text, 'bool MoveAwayFromHazard::IsHazardNearby(')
        fixture = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <list>
#include <string>
using uint8 = uint8_t;
using int8 = int8_t;
constexpr float M_PI_F = 3.14159265f;
constexpr int TEMPSPAWN_TIMED_DESPAWN = 0;
bool pathAllowed = true;
float selectedRadius = 0;
float frand(float low, float high) { if (low > 0) selectedRadius=low; return low > 0 ? (low+high)/2 : 0; }
int urand(int, int high) { return high; }
struct Unit { float x=0, y=0, z=0; };
struct Player : Unit {
    bool los = true;
    float GetDistance(float a,float b,float c) { return std::sqrt((x-a)*(x-a)+(y-b)*(y-b)+(z-c)*(z-c)); }
    bool IsWithinLOS(float,float,float) { return los; }
    float GetCollisionHeight() { return 1; }
    unsigned GetMapId() { return 309; }
    void SummonCreature(int,float,float,float,float,int,float) {}
};
struct WorldPosition {
    float x=0,y=0,z=0;
    WorldPosition()=default;
    WorldPosition(unsigned,float x,float y,float z):x(x),y(y),z(z){}
    explicit WorldPosition(Unit* u):x(u->x),y(u->y),z(u->z){}
    float getX() const { return x; } float getY() const { return y; } float getZ() const { return z; }
    void setZ(float value) { z=value; } float GetHeight() const { return 0; }
    float GetAngleTo(WorldPosition const& p) const { return std::atan2(p.y-y,p.x-x); }
    float distance(WorldPosition const& p) const { return std::sqrt((x-p.x)*(x-p.x)+(y-p.y)*(y-p.y)+(z-p.z)*(z-p.z)); }
    bool canPathTo(WorldPosition const&,Player*) const { return pathAllowed; }
    WorldPosition operator+(WorldPosition const& p) const { return {0,x+p.x,y+p.y,z+p.z}; }
};
using HazardPosition=std::pair<WorldPosition,float>;
struct Context {
    std::list<HazardPosition> hazards;
    Unit* target=nullptr;
    template<class T> T Read(std::string);
};
template<> std::list<HazardPosition> Context::Read(std::string) { return hazards; }
template<> Unit* Context::Read(std::string) { return target; }
#define AI_VALUE(type,name) context->Read<type>(name)
enum class BotState { BOT_STATE_COMBAT };
struct AI { bool HasStrategy(std::string,BotState) { return false; } };
struct Event {};
struct MoveAwayFromHazard {
    Player* bot; Context* context; AI* ai;
    bool reaction=false, moveAllowed=true;
    unsigned moves=0, waits=0;
    WorldPosition destination;
    bool Execute(Event&);
    bool IsHazardNearby(WorldPosition const&,std::list<HazardPosition> const&) const;
    bool IsReaction() { return reaction; }
    bool MoveTo(unsigned,float x,float y,float z,bool,bool,bool,bool) {
        ++moves; destination={0,x,y,z}; return moveAllowed;
    }
    void WaitForReach(float) { ++waits; }
};
'''
        checks = r'''
int main() {
    Player bot; Context context; AI ai; Event event;
    MoveAwayFromHazard action{&bot,&context,&ai};
    // The smaller nearby circle does NOT contain the bot; the farther one does.
    context.hazards={{{0,1,0,0},0.5f},{{0,3,0,0},4.0f}};
    assert(action.Execute(event)); // predecessor incorrectly returns false
    assert(selectedRadius==4.0f && action.moves==1);
    assert(!action.IsHazardNearby(action.destination,context.hazards));
    context.hazards.reverse(); action.moves=0;
    assert(action.Execute(event) && selectedRadius==4.0f); // independent of list order
    context.hazards={{{0,1,0,0},2.0f},{{0,3,0,0},4.0f}};
    action.moves=0;
    assert(action.Execute(event) && selectedRadius==2.0f); // nearest containing still wins
    assert(!action.IsHazardNearby(action.destination,context.hazards));
    context.hazards={{{0,4,0,0},4.0f}}; action.reaction=true; action.waits=0;
    assert(action.Execute(event) && action.waits==1); // exact boundary; reaction wait retained
    bot.los=false; action.moves=0; assert(!action.Execute(event) && action.moves==0);
    bot.los=true; pathAllowed=false; assert(!action.Execute(event) && action.moves==0);
    pathAllowed=true; action.moveAllowed=false; assert(!action.Execute(event));
    action.moveAllowed=true; action.moves=0;
    context.hazards={{{0,1,0,0},0.5f},{{0,5,0,0},1.0f}};
    assert(!action.Execute(event) && action.moves==0); // queued trigger is now stale
    context.hazards.clear(); assert(!action.Execute(event) && action.moves==0);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source = pathlib.Path(tmp) / 'fixture.cpp'
            binary = pathlib.Path(tmp) / 'fixture'
            source.write_text(fixture + production + checks)
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-parameter', '-Wno-missing-field-initializers',
                            str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
