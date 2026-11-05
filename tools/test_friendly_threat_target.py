"""Exercise production threat helpers after redirecting a friendly target."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class FriendlyThreatTargetTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_friendly_target_without_victim(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / "ai/playerbot/strategy/values/ThreatValues.cpp").read_text()
        start = source.index("float ThreatValue::GetThreat(")
        methods = source[start:source.index("uint8 ThreatValue::Calculate(Unit*", start)]
        harness = r"""
#include <cassert>
#include <vector>
struct Guid { bool player = false; bool IsPlayer() const { return player; } };
struct Unit {
    virtual ~Unit() = default;
    unsigned map = 1; bool friendly = false; Unit* victim = nullptr; Guid guid;
    unsigned GetMapId() const { return map; }
    Unit* GetVictim() const { return victim; }
    Guid getObjectGuid() const { return guid; }
};
struct Creature : Unit {
    bool immune = false;
    bool HasImmunityFlag(int) const { return immune; }
};
enum { CREATURE_IMMUNITY_TAUNT = 0x02 };
struct Player;
struct Group { std::vector<Player*> members; };
struct Player : Unit {
    Group* group = nullptr; bool tank = false, alive = true, safe = true;
    bool IsBeingTeleported() const { return false; }
    Group* GetGroup() const { return group; }
};
struct PlayerbotAI {
    Player* bot;
    Player* GetBot() const { return bot; }
    bool IsSafe(Player* p) const { return p->safe; }
    bool IsTank(Player* p) const { return p->tank; }
};
struct ThreatManager { float getThreat(Player*) const { return 42; } };
struct Facade {
    bool IsFriendlyTo(Unit* unit, Player*) const { return unit->friendly; }
    bool IsAlive(Player* p) const { return p->alive; }
    ThreatManager GetThreatManager(Unit*) const { return {}; }
} sServerFacade;
std::vector<Player*> LiveGroupMembers(Group* g) { return g->members; }
struct ThreatValue {
    static float GetThreat(Player*, Unit*);
    static float GetTankThreat(PlayerbotAI*, Unit*);
};
#include "ThreatCapPolicy.h"
bool TargetTauntImmune(Unit* target);
using namespace ai;
"""
        harness += methods + r"""
int main() {
    Player bot, tank; tank.tank = true;
    Group group{{&bot, &tank}}; bot.group = &group;
    PlayerbotAI ai{&bot};
    Unit friendly, enemy;
    friendly.friendly = true;
    assert(ThreatValue::GetThreat(&bot, &friendly) == 0);
    assert(ThreatValue::GetTankThreat(&ai, &friendly) == 0);
    friendly.victim = &enemy;
    assert(ThreatValue::GetThreat(&bot, &friendly) == 42);
    assert(ThreatValue::GetTankThreat(&ai, &friendly) == 42);
    assert(ThreatValue::GetThreat(&bot, &enemy) == 42);
    assert(ThreatValue::GetTankThreat(&ai, &enemy) == 42);
    assert(ThreatValue::GetThreat(&bot, nullptr) == 0);
    assert(ThreatValue::GetTankThreat(&ai, nullptr) == 0);
    enemy.guid.player = true;
    assert(ThreatValue::GetThreat(&bot, &enemy) == 0);
    assert(ThreatValue::GetTankThreat(&ai, &enemy) == 0);
}
"""
        with tempfile.TemporaryDirectory(prefix="friendly-threat-") as folder:
            cpp = pathlib.Path(folder) / "test.cpp"
            exe = pathlib.Path(folder) / "test"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I", str(root / "ai/playerbot"),
                            str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
