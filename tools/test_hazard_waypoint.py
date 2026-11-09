"""Compile the production hazard path rewrite against deterministic geometry."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class HazardWaypointTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_replacement_is_written_to_returned_path(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
        start = source.index("bool MovementAction::GeneratePathAvoidingHazards(")
        method = source[start:source.index("bool FleeAction::Execute(", start)]
        harness = r"""
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <list>
#include <vector>
using uint8 = uint8_t;
using uint32 = uint32_t;
struct Bot;
struct WorldPosition {
    float x, y, z;
    WorldPosition(unsigned = 0, float x = 0, float y = 0, float z = 0): x(x), y(y), z(z) {}
    float getX() const { return x; } float getY() const { return y; } float getZ() const { return z; }
    unsigned GetMapId() const { return 0; }
    bool operator==(WorldPosition p) const { return x == p.x && y == p.y && z == p.z; }
    WorldPosition operator-(WorldPosition p) const { return {0,x-p.x,y-p.y,z-p.z}; }
    WorldPosition operator+(WorldPosition p) const { return {0,x+p.x,y+p.y,z+p.z}; }
    WorldPosition operator*(float f) const { return {0,x*f,y*f,z*f}; }
    WorldPosition operator/(float f) const { return *this * (1/f); }
    float size() const { return std::sqrt(x*x+y*y+z*z); }
    float distance(WorldPosition p) const { return (*this-p).size(); }
    // The moved-leg check asks the navmesh; the stub answers the leg it was
    // asked for, so a reached moved point keeps the rewrite.
    std::vector<WorldPosition> GetPathTo(WorldPosition end, Bot*) const { return {*this, end}; }
};
#include "ai/playerbot/LandWalkPolicy.h"
using HazardPosition = std::pair<WorldPosition,float>;
std::list<HazardPosition> activeHazards;
#define AI_VALUE(type, name) activeHazards
namespace BotState { enum { BOT_STATE_COMBAT }; }
enum { TEMPSPAWN_TIMED_DESPAWN };
struct Bot {
    unsigned GetMapId() const { return 0; }
    void SummonCreature(int,float,float,float,float,int,float) {}
} botObject;
struct AI { bool HasStrategy(const char*, int) const { return false; } } aiObject;
struct MovementAction {
    Bot* bot = &botObject; AI* ai = &aiObject;
    bool allowLeft = true, allowRight = true;
    bool GeneratePathAvoidingHazards(std::vector<WorldPosition>&);
    WorldPosition CalculatePerpendicularPoint(WorldPosition, WorldPosition hazard, float offset, bool left) {
        return {0,hazard.x,hazard.y+(left ? offset : -offset),hazard.z};
    }
    bool IsValidPosition(WorldPosition p, WorldPosition) {
        return p.y > 0 ? allowLeft : allowRight;
    }
};
"""
        harness += method + r"""
int main() {
    MovementAction action;
    activeHazards = {{{0,0,0,0},2}};
    const std::vector<WorldPosition> original{{0,-5,0,0},{0,0,0,0},{0,5,0,0}};
    auto path = original;
    assert(action.GeneratePathAvoidingHazards(path));
    assert(path[1].y == 3); // accepted left detour must survive the function return
    assert(path.front().x == -5 && path.back().x == 5 && path.size() == 3);
    action.allowLeft = false;
    path = original;
    assert(action.GeneratePathAvoidingHazards(path));
    assert(path[1].y == -3); // rejected left, accepted right
    action.allowRight = false;
    path = original;
    assert(!action.GeneratePathAvoidingHazards(path));
    assert(path[1].y == 0); // no accepted detour: no invented movement
    activeHazards.clear();
    assert(!action.GeneratePathAvoidingHazards(path));
    assert(path[1].y == 0);
}
"""
        with tempfile.TemporaryDirectory(prefix="hazard-waypoint-") as folder:
            cpp = pathlib.Path(folder) / "test.cpp"
            exe = pathlib.Path(folder) / "test"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root),
                            str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
