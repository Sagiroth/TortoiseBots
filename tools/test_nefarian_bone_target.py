"""Exercise the production Bone Construct selector and CC reservation lookup."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class NefarianBoneTargetTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_uncontrolled_reserved_construct_is_skipped(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        values = root / "ai/playerbot/strategy/values"
        select = function((values / "DpsTargetValue.cpp").read_text(),
                          "Unit* GetNefarianBoneConstructTarget(")
        reserved = function((values / "TargetValue.cpp").read_text(),
                            "bool FindNonCcTargetStrategy::IsCcTarget(")
        policy = (root / "ai/playerbot/RaidDpsExclusionPolicy.h").read_text()
        active = function(policy, "inline bool NefarianBoneFocusActive(")
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <vector>
using uint64 = uint64_t;
struct ObjectGuid {
    uint64 value = 0;
    ObjectGuid(uint64 value = 0) : value(value) {}
    bool operator==(ObjectGuid other) const { return value == other.value; }
    bool operator<(ObjectGuid other) const { return value < other.value; }
};
struct Unit {
    ObjectGuid guid;
    unsigned entry;
    bool alive = true, combat = true, valid = true;
    float health = 10;
    unsigned GetEntry() const { return entry; }
    bool IsAlive() const { return alive; }
    bool IsInCombat() const { return combat; }
    float GetHealthPercent() const { return health; }
    ObjectGuid GetObjectGuid() const { return guid; }
    ObjectGuid getObjectGuid() const { return guid; }
};
struct Group {
    struct Slot { ObjectGuid guid; };
    using MemberSlotList = std::vector<Slot>;
    using member_citerator = MemberSlotList::const_iterator;
    MemberSlotList members;
    uint64 icons[8] = {};
    MemberSlotList const& GetMemberSlots() const { return members; }
    uint64 GetTargetIcon(int index) const { return icons[index]; }
};
struct PlayerbotAI;
struct Player : Unit {
    Group* group = nullptr;
    PlayerbotAI* ai = nullptr;
    unsigned map = 469;
    bool tank = false;
    Unit* rtiCcTarget = nullptr;
    std::string rtiCc;
    Group* GetGroup() const { return group; }
    unsigned GetMapId() const { return map; }
    template<class T> T Value(std::string key);
};
template<> Unit* Player::Value<Unit*>(std::string) { return rtiCcTarget; }
template<> std::string Player::Value<std::string>(std::string) { return rtiCc; }
struct AiObjectContext {
    std::list<ObjectGuid> possible;
    Unit* cc = nullptr;
    template<class T> T Value(std::string key);
};
template<> Unit* AiObjectContext::Value<Unit*>(std::string) { return cc; }
template<> std::list<ObjectGuid> AiObjectContext::Value<std::list<ObjectGuid>>(std::string) { return possible; }
#define AI_VALUE(type, key) context->Value<type>(key)
#define PAI_VALUE(type, key) player->Value<type>(key)
enum class BotState { BOT_STATE_COMBAT };
struct PlayerbotAI {
    Player* bot;
    AiObjectContext context;
    std::map<ObjectGuid, Unit*> units;
    bool enabled = true;
    Player* GetBot() const { return bot; }
    bool IsTank(Player* p) const { return p->tank; }
    bool IsSafe(Player*) const { return true; }
    bool HasStrategy(std::string, BotState) const { return enabled; }
    AiObjectContext* GetAiObjectContext() { return &context; }
    Unit* GetUnit(ObjectGuid guid) { return units[guid]; }
};
struct Storage {
    Storage& Instance() { return *this; }
    PlayerbotAI* GetAI(Player* player) { return player->ai; }
};
struct PlayerbotAIStorage {
    static Storage& Instance() { static Storage storage; return storage; }
};
struct {
    std::map<ObjectGuid, Player*> players;
    Player* GetPlayer(ObjectGuid guid) { return players[guid]; }
} sObjectMgr;
struct { bool IsAlive(Player* p) const { return p->alive; } } sServerFacade;
struct { float sightDistance = 60; } sPlayerbotAIConfig;
struct RtiTargetValue {
    static int GetRtiIndex(std::string mark) { return mark == "triangle" ? 3 : -1; }
};
struct PossibleAttackTargetsValue {
    static bool IsValid(Unit* unit, Player*, float, bool, bool) { return unit->valid; }
};
struct FindNonCcTargetStrategy {
    PlayerbotAI* ai;
    bool IsCcTarget(Unit* attacker);
};
struct DpsTargetStrategy : FindNonCcTargetStrategy {
    DpsTargetStrategy(PlayerbotAI* ai) : FindNonCcTargetStrategy{ai} {}
};
'''
        fixture += active + reserved + select + r'''
int main() {
    Player bot;
    PlayerbotAI ai{&bot, {}, {}};
    bot.ai = &ai;
    Unit boss{{100}, 11583}, low{{1}, 14605}, high{{2}, 14605};
    ai.units = {{boss.guid, &boss}, {low.guid, &low}, {high.guid, &high}};
    ai.context.possible = {high.guid, boss.guid, low.guid};
    Group group;
    bot.group = &group;
    group.members = {{ObjectGuid(10)}};
    sObjectMgr.players[ObjectGuid(10)] = &bot;
    assert(GetNefarianBoneConstructTarget(&ai) == &low);
    // Moon reserves Shackle before any aura or rti-cc calculated target exists.
    group.icons[4] = low.guid.value;
    assert(GetNefarianBoneConstructTarget(&ai) == &high);
    group.icons[4] = 0;
    bot.rtiCc = "triangle";
    group.icons[3] = low.guid.value;
    assert(GetNefarianBoneConstructTarget(&ai) == &high);
    group.icons[3] = 0;
    bot.rtiCcTarget = &low;
    assert(GetNefarianBoneConstructTarget(&ai) == &high);
    bot.rtiCcTarget = nullptr;
    ai.context.cc = &low;
    assert(GetNefarianBoneConstructTarget(&ai) == &high);
    group.icons[4] = high.guid.value;
    assert(GetNefarianBoneConstructTarget(&ai) == nullptr);
    group.icons[4] = 0;
    ai.context.cc = nullptr;
    low.valid = false; // Existing CC aura rejected by the engine validity seam.
    assert(GetNefarianBoneConstructTarget(&ai) == &high);
    low.valid = true;
    ai.enabled = false;
    assert(GetNefarianBoneConstructTarget(&ai) == nullptr);
    ai.enabled = true;
    bot.tank = true;
    assert(GetNefarianBoneConstructTarget(&ai) == nullptr);
    bot.tank = false;
    boss.health = 20;
    assert(GetNefarianBoneConstructTarget(&ai) == nullptr);
}
'''
        with tempfile.TemporaryDirectory(prefix="nefarian-bone-") as folder:
            source = pathlib.Path(folder) / "test.cpp"
            binary = pathlib.Path(folder) / "test"
            source.write_text(fixture)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
