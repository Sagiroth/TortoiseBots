"""Execute the production hazard-chase branch against the core motion contract."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class HazardChaseMotionTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_next_motion_update_after_hazard_spline(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
        start = source.index("bool MovementAction::ChaseTo(")
        start = source.index("if (GeneratePathAvoidingHazards(path)", start)
        start = source.index("{", start) + 1
        end = source.index("return true;", start) + len("return true;")
        branch = source[start:end]
        harness = r'''
#include <cassert>
#include <vector>
using uint32 = unsigned;
namespace G3D { struct Vector3 {}; }
constexpr int MOVE_RUN = 0;
struct MotionMaster {
    // d94947b: full Clear removes idle too; MoveIdle restores it.
    // UpdateMotion checks !empty() before its deferred-cleanup fallback.
    std::vector<int> generators{0, 1};
    void Clear(bool, bool all) {
        assert(all);
        generators.clear();
    }
    void MoveIdle() {
        if (generators.empty() || generators.back() != 0)
            generators.push_back(0);
    }
    void UpdateMotion(unsigned) { assert(!generators.empty()); }
};
struct Transport { unsigned GetGUIDLow() const { return 1; } };
struct Unit {
    MotionMaster motion;
    bool launchSucceeds = true, moving = false;
    Transport* GetTransport() { return nullptr; }
    const char* GetName() const { return "fixture"; }
    bool IsStopped() const { return !moving; }
    float GetSpeed(int) const { return 7; }
};
struct WorldPosition {
    float GetPathLength(const std::vector<WorldPosition>&) const { return 10; }
    std::vector<G3D::Vector3> toPointsArray(const std::vector<WorldPosition>& path) const {
        return std::vector<G3D::Vector3>(path.size());
    }
};
namespace Movement {
struct MoveSplineInit {
    Unit& unit;
    MoveSplineInit(Unit& unit, const char*) : unit(unit) {}
    void MovebyPath(const std::vector<G3D::Vector3>&) {}
    void SetTransport(unsigned) {}
    int Launch() {
        // Core spline launch changes the spline, not the generator stack.
        unit.moving = unit.launchSucceeds;
        return unit.launchSucceeds ? 100 : 0;
    }
};
}
struct Log { template<class... T> void outDetail(const char*, T...) {} } sLog;
struct MovementAction {
    Unit* bot;
    bool waited = false;
    void WaitForReach(float) { waited = true; }
    bool ExecuteHazardBranch(Unit* obj) {
        MotionMaster& mm = bot->motion;
        WorldPosition botPosition;
        std::vector<WorldPosition> path(3);
        const float distanceToTarget = 20;
        [[maybe_unused]] auto traceChase = [](const char*, int) {};
'''
        harness += branch + r'''
    }
};
int main() {
    for (bool succeeds : {true, false}) {
        for (bool idleOnly : {true, false}) {
            Unit bot, target;
            bot.launchSucceeds = succeeds;
            if (idleOnly) bot.motion.generators = {0};
            MovementAction action{&bot};
            assert(action.ExecuteHazardBranch(&target));
            assert(action.waited);
            assert(bot.moving == succeeds);
            // This is the next core movement tick, after ChaseTo returned.
            bot.motion.UpdateMotion(50);
            assert(bot.motion.generators.size() == 1);
            assert(bot.motion.generators.back() == 0);
        }
    }
}
'''
        with tempfile.TemporaryDirectory(prefix="hazard-chase-") as folder:
            cpp = pathlib.Path(folder) / "test.cpp"
            exe = pathlib.Path(folder) / "test"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
