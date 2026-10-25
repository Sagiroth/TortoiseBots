"""Production group-readiness calculation with controlled party members."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class GroupReadyManaTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_empty_caster_waits(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / "ai/playerbot/strategy/values/GroupValues.cpp").read_text()
        start = source.index("bool GroupReadyValue::Calculate()")
        method = source[start:]
        harness = r'''
#include <cassert>
#include <list>
#include <string>
using ObjectGuid = unsigned;
constexpr int POWER_MANA = 0;
struct Unit {};
struct Group {};
struct Player : Unit {
    unsigned mana = 0, capacity = 100;
    bool alive = true, combat = false, grouped = true;
    float health = 100;
    Group* GetGroup() { static Group group; return grouped ? &group : nullptr; }
    float distance = 0;
    bool IsAlive() const { return alive; }
    bool IsInCombat() const { return combat; }
    float GetHealthPercent() const { return health; }
    unsigned GetPower(int) const { return mana; }
    unsigned GetMaxPower(int) const { return capacity; }
} member, leader;
enum class BotState { BOT_STATE_NON_COMBAT };
struct PlayerbotAI {
    Player* GetGroupMaster() { return &leader; }
    bool HasStrategy(char const*, BotState) { return true; }
} botAI;
struct PlayerbotAIStorage {
    static PlayerbotAIStorage& Instance() { static PlayerbotAIStorage s; return s; }
    PlayerbotAI* GetAI(Player*) { return &botAI; }
};
struct ObjectMgr { Player* GetPlayer(ObjectGuid id) { return id ? &member : nullptr; } } sObjectMgr;
struct ServerFacade { float getDistance2d(Player* p, Player*) { return p->distance; } } sServerFacade;
struct Config { float sightDistance = 60, almostFullHealth = 90, mediumMana = 40; } sPlayerbotAIConfig;
bool dungeon = false;
struct WorldPosition { WorldPosition(Player*) {} bool isOverworld() { return !dungeon; } };
std::list<ObjectGuid> members{1};
#define AI_VALUE(type, ...) (members)
#define AI_VALUE_LAZY(type, ...) static_cast<type>(0)
struct GroupReadyValue {
    Player* bot = &leader;
    PlayerbotAI* ai = &botAI;
    bool Calculate();
};
'''
        harness += method + r'''
int main() {
    GroupReadyValue value;
    assert(!value.Calculate()); // Exhausted caster must wait.
    member.mana = 1;
    assert(!value.Calculate());
    member.mana = 40;
    assert(value.Calculate());
    member.mana = 0;
    member.capacity = 0;
    assert(value.Calculate()); // Non-mana class must not block travel.
    member.capacity = 100;
    member.combat = true;
    assert(value.Calculate()); // Existing combat exemption.
    member.combat = false;
    member.distance = 70;
    assert(value.Calculate()); // Do not wait for distant members.
    member.distance = 0;
    member.mana = 100;
    member.alive = false;
    dungeon = true;
    assert(!value.Calculate());
    members = {0};
    assert(value.Calculate());
    members.clear();
    assert(value.Calculate());
    members = {1}; member.alive = true; member.health = 80;
    assert(!value.Calculate()); // Upstream grouped regeneration gate stays active.
    leader.grouped = false;
    assert(value.Calculate()); // Solo attacker-gated health behavior remains.
}
'''
        with tempfile.TemporaryDirectory(prefix="group-ready-") as folder:
            cpp = pathlib.Path(folder) / "test.cpp"
            exe = pathlib.Path(folder) / "test"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
