"""Exercise production threat helpers after redirecting a friendly target, and the tank reference split."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class FriendlyThreatTargetTest(unittest.TestCase):
    @staticmethod
    def _source_methods():
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / "ai/playerbot/strategy/values/ThreatValues.cpp").read_text()
        start = source.index("float ThreatValue::GetThreat(")
        methods = source[start:source.index("uint8 ThreatValue::Calculate(Unit*", start)]
        harness = r"""
#include <cassert>
#include <vector>
struct Guid { bool player = false; bool empty = true; bool IsPlayer() const { return player; } bool IsEmpty() const { return empty; } };
using ObjectGuid = Guid;
struct Unit {
    virtual ~Unit() = default;
    unsigned map = 1; unsigned entry = 1; bool friendly = false; Unit* victim = nullptr; Guid guid;
    unsigned GetMapId() const { return map; }
    unsigned GetEntry() const { return entry; }
    Unit* GetVictim() const { return victim; }
    bool IsInWorld() const { return true; }
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
    Group* group = nullptr; bool tank = false, assigned = false, alive = true, safe = true;
    float threat = 42; Guid charmer;
    bool IsBeingTeleported() const { return false; }
    Group* GetGroup() const { return group; }
    Guid GetCharmerGuid() const { return charmer; }
};
struct PlayerbotAI {
    Player* bot;
    Player* GetBot() const { return bot; }
    bool IsSafe(Player* p) const { return p->safe; }
    bool IsTank(Player* p) const { return p->tank; }
    bool HasAura(unsigned, Player*) const { return false; }
};
struct ThreatManager { float getThreat(Player* p) const { return p->threat; } };
struct RaidTankRoles { std::vector<Player*> tanks; };
RaidTankRoles GetRaidTankRoles(PlayerbotAI* ai)
{
    RaidTankRoles roles;
    Player* bot = ai->GetBot();
    if (bot->assigned)
        roles.tanks.push_back(bot);
    if (bot->group)
        for (Player* member : bot->group->members)
            if (member != bot && member->assigned)
                roles.tanks.push_back(member);
    return roles;
}
bool gThreatSuccessor = false;
struct BossTacticsStub { bool HasThreatSuccessor(unsigned, unsigned) const { return gThreatSuccessor; } };
BossTacticsStub GetBossTactics() { return {}; }
struct Facade {
    bool IsFriendlyTo(Unit* unit, Player*) const { return unit->friendly; }
    bool IsAlive(Player* p) const { return p->alive; }
    ThreatManager GetThreatManager(Unit*) const { return {}; }
} sServerFacade;
std::vector<Player*> LiveGroupMembers(Group* g) { return g->members; }
struct ConfigStub { bool dungeonRunThreatSuccessorNotHolder = false; } sPlayerbotAIConfig;
struct ThreatValue {
    static float GetThreat(Player*, Unit*);
    static float GetTankThreat(PlayerbotAI*, Unit*);
};
#include "ThreatCapPolicy.h"
// The bot_events row of the successor cap is not part of these stubs: the extracted method only has to compile it.
void LogThreatCap(PlayerbotAI*, Player*, const ai::ThreatCapPick&, Unit*) {}
void LogHexSnapshot(PlayerbotAI*, Unit*, const RaidTankRoles&, Group*) {}
namespace ai { bool TargetTauntImmune(Unit* target); bool ThreatSuccessorTarget(Unit* target); }
using namespace ai;
"""
        return root, harness + methods

    def _compile_and_run(self, main_body):
        root, harness = self._source_methods()
        with tempfile.TemporaryDirectory(prefix="friendly-threat-") as folder:
            cpp = pathlib.Path(folder) / "test.cpp"
            exe = pathlib.Path(folder) / "test"
            cpp.write_text(harness + main_body)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I", str(root / "ai/playerbot"),
                            str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_friendly_target_without_victim(self):
        self._compile_and_run(r"""
int main() {
    Player bot, tank; tank.tank = true; tank.assigned = true;
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
""")

    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_tank_threat_keeps_the_is_tank_max_without_the_row(self):
        # Only a target whose boss data asks for a threat successor reads the raid's tank order. Every other target
        # keeps the plain IsTank max it always had: a tank the raid order does not list still counts there, and the
        # charm check is the successor path's alone. Before the split the raid order answered every target.
        self._compile_and_run(r"""
int main() {
    Player bot, main, off, other;
    main.tank = true; main.assigned = true; main.threat = 1000;
    off.assigned = true; off.threat = 600;
    other.tank = true; other.threat = 1500; // a tank the raid's order does not list
    Group group{{&bot, &main, &off, &other}}; bot.group = &group;
    PlayerbotAI ai{&bot};
    Unit enemy;
    gThreatSuccessor = false;
    assert(ThreatValue::GetTankThreat(&ai, &enemy) == 1500);  // IsTank-only max
    gThreatSuccessor = true;
    assert(ThreatValue::GetTankThreat(&ai, &enemy) == 600);   // the assigned off-tank's threat
    // A charmed main tank's threat is gone; the off-tank still answers as the successor.
    main.threat = 0; main.charmer = Guid{false, false};
    assert(ThreatValue::GetTankThreat(&ai, &enemy) == 600);
    gThreatSuccessor = false;
    assert(ThreatValue::GetTankThreat(&ai, &enemy) == 1500);  // the old loop counts the charmed tank at 0 too
}
""")


if __name__ == "__main__":
    unittest.main()
