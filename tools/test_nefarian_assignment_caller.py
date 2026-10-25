"""Run production assignment, participation, victim-role and return checks."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

from tools.test_nefarian_bone_target import function


class NefarianAssignmentCallerTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_roles_holds_cc_and_shared_cache(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        text = (root / "ai/playerbot/strategy/actions/BlackwingLairDungeonActions.cpp").read_text()
        header = (root / "ai/playerbot/strategy/values/NefarianAddAssignmentValue.h").read_text()
        policy = (root / "ai/playerbot/NefarianAddAssignmentPolicy.h").read_text()
        value = (root / "ai/playerbot/strategy/Value.h").read_text()
        get = function(value, "virtual T Get() override")
        # Substitute only the clock so the real cache predicate is deterministic.
        refresh = function(value, "virtual void Refresh()")
        refresh = refresh.replace("time_t now = time(0);", "time_t now = fixtureNow;")
        fixture = policy.replace("#pragma once", "").replace("PlanNefarianAdds(", "RealPlanNefarianAdds(")
        fixture += r'''
#include <cassert>
#include <ctime>
#include <list>
#include <map>
#include <set>
#include <string>
#include <type_traits>
using namespace ai;
int planCalls = 0, targetReads = 0;
time_t fixtureNow = 100;
namespace ai {
auto PlanNefarianAdds(std::vector<NefarianAddTankCandidate> tanks,
    std::vector<std::uint64_t> adds, std::vector<std::pair<std::uint64_t,std::uint64_t>> locks) {
    ++planCalls;
    return RealPlanNefarianAdds(tanks, adds, locks);
}
}
struct ObjectGuid {
    std::uint64_t raw = 0;
    ObjectGuid(std::uint64_t raw = 0) : raw(raw) {}
    std::uint64_t GetRawValue() const { return raw; }
    bool IsEmpty() const { return !raw; }
    bool operator==(ObjectGuid b) const { return raw == b.raw; }
    bool operator!=(ObjectGuid b) const { return raw != b.raw; }
    bool operator<(ObjectGuid b) const { return raw < b.raw; }
};
struct Unit {
    virtual ~Unit() = default;
    ObjectGuid guid;
    unsigned entry = 0, map = 469;
    bool alive = true, combat = true, valid = true, reserved = false, controlled = false;
    Unit* victim = nullptr;
    bool IsAlive() const { return alive; }
    bool IsInCombat() const { return combat; }
    bool IsInMap(Unit* unit) const { return map == unit->map; }
    unsigned GetEntry() const { return entry; }
    unsigned GetMapId() const { return map; }
    ObjectGuid GetObjectGuid() const { return guid; }
    Unit* GetVictim() const { return victim; }
    float GetDistance(Unit* unit) const { return float(unit->guid.raw); }
};
struct PlayerbotAI;
struct Player;
struct Group { std::vector<Player*> members; };
struct Player : Unit {
    bool tank = true, inWorld = true, teleporting = false;
    PlayerbotAI* ai = nullptr;
    Group* group = nullptr;
    bool IsInWorld() const { return inWorld; }
    bool IsBeingTeleported() const { return teleporting; }
    Group* GetGroup() const { return group; }
};
auto const& LiveGroupMembers(Group* group) { return group->members; }
struct Position { bool set = false; bool isSet() const { return set; } };
using PositionMap = std::map<std::string, Position>;
template<class T> struct Value { virtual ~Value() = default; virtual T Get() = 0; };
template<class T> struct Manual : Value<T> {
    T value{};
    T Get() override { if constexpr (std::is_same_v<T,std::list<ObjectGuid>>) ++targetReads; return value; }
};
'''
        fixture += function(header, "struct NefarianAddAssignment") + ";\n"
        fixture += r'''
struct AiObjectContext {
    PositionMap positions;
    std::map<std::string, Manual<Unit*>> units;
    Manual<ObjectGuid> explicitTarget;
    Manual<std::list<ObjectGuid>> possible;
    Value<NefarianAddAssignment>* assignment = nullptr;
    template<class T> Value<T>* GetValue(std::string key) {
        if constexpr (std::is_same_v<T,Unit*>) return &units[key];
        else if constexpr (std::is_same_v<T,ObjectGuid>) return &explicitTarget;
        else if constexpr (std::is_same_v<T,std::list<ObjectGuid>>) return &possible;
        else return assignment;
    }
    template<class T> T Read(std::string key) {
        if constexpr (std::is_same_v<T,PositionMap&>) return positions;
        else return GetValue<T>(key)->Get();
    }
};
#define AI_VALUE(type, key) context->Read<type>(key)
enum class BotState { BOT_STATE_COMBAT };
std::map<ObjectGuid,Unit*> world;
struct PlayerbotAI {
    Player* bot;
    AiObjectContext context;
    bool enabled = false;
    std::set<std::string> strategies;
    explicit PlayerbotAI(Player* bot) : bot(bot) { bot->ai = this; }
    Player* GetBot() const { return bot; }
    AiObjectContext* GetAiObjectContext() { return &context; }
    Unit* GetUnit(ObjectGuid guid) { return world[guid]; }
    bool IsTank(Player* p) const { return p->tank; }
    bool HasStrategy(std::string name, BotState) const {
        return name == "nefarian add tank" ? enabled : strategies.count(name) != 0;
    }
};
struct PlayerbotAIStorage {
    static PlayerbotAIStorage& Instance() { static PlayerbotAIStorage storage; return storage; }
    PlayerbotAI* GetAI(Player* player) { return player->ai; }
};
struct ThreatManager {};
struct FindNonCcTargetStrategy {
    explicit FindNonCcTargetStrategy(PlayerbotAI*) {}
    virtual void CheckAttacker(Unit*, ThreatManager*) = 0;
    bool IsCcTarget(Unit* unit) const { return unit->reserved; }
};
struct PossibleAttackTargetsValue {
    static bool IsValid(Unit* unit, Player*, float, bool, bool) { return unit->valid && !unit->controlled; }
    static bool HasBreakableCC(Unit* unit, Player*) { return unit->controlled; }
    static bool HasUnBreakableCC(Unit*, Player*) { return false; }
};
struct { float sightDistance = 60; } sPlayerbotAIConfig;
template<class T> class CalculatedValue : public Value<T> {
public:
    PlayerbotAI* ai;
    int checkInterval;
    time_t lastCheckTime = 0;
    T value{};
    CalculatedValue(PlayerbotAI* ai, std::string, int interval) : ai(ai), checkInterval(interval) {}
    virtual T Calculate() = 0;
    void Reset() { lastCheckTime = 0; }
'''
        fixture += refresh + "\n" + get + "\n};\n"
        fixture += function(header, "class NefarianAddAssignmentValue") + ";\n"
        fixture += function(text, "class NefarianCcReservation") + ";\n"
        for signature in ["bool EbonrocHoldOrder(", "bool IsNefarianAddCcReserved(",
                          "Player* GetNefarianTankVictim(", "bool ai::IsNefarianAddTankOptedIn(",
                          "NefarianAddAssignment NefarianAddAssignmentValue::Calculate()",
                          "Unit* ai::GetAssignedNefarianAddTarget(",
                          "bool ai::IsNefarianAddTargetExcluded("]:
            fixture += function(text, signature).replace("ai::", "") + "\n"
        fixture += r'''
int main() {
    Player first, second, healer, otherTank;
    first.guid = 10; second.guid = 20; healer.guid = 30; healer.tank = false; otherTank.guid = 40;
    PlayerbotAI a(&first), b(&second), c(&otherTank);
    NefarianAddAssignmentValue av(&a), bv(&b), cv(&c);
    a.context.assignment = &av; b.context.assignment = &bv; c.context.assignment = &cv;
    Group group{{&second, &healer, &otherTank, &first}};
    for (Player* player : group.members) player->group = &group;
    Unit boss, add, another;
    boss.guid = 100; boss.entry = 11583;
    add.guid = 101; add.entry = 14605; add.victim = &healer;
    another.guid = 102; another.entry = 14668; another.victim = &healer;
    first.victim = &boss;
    world = {{100,&boss}, {101,&add}, {102,&another}};
    a.context.possible.value = {100,101,102}; b.context.possible.value = {100,101,102};
    b.enabled = true; // A participating peer must not activate the requester.
    assert(!av.Get().active && targetReads == 0 && planCalls == 0);
    a.enabled = b.enabled = true; av.Reset();
    auto bp = bv.Get(); auto ap = av.Get();
    assert(planCalls == 1 && targetReads == 2); // One elected planner, not one per tank.
    assert(GetAssignedNefarianAddTarget(&a, ap) == &add);
    assert(GetAssignedNefarianAddTarget(&b, bp) == &another);
    assert(GetNefarianTankVictim(&a, &add) == nullptr); // Healer is not a tank lock.
    assert(!IsNefarianAddTargetExcluded(&a, &add));
    assert(IsNefarianAddTargetExcluded(&a, &another));
    ++fixtureNow; bv.Get(); av.Get();
    assert(planCalls == 2 && targetReads == 4);

    for (std::string hold : {"stay", "follow", "wait for attack", "grind"}) {
        a.strategies.insert(hold);
        assert(!GetAssignedNefarianAddTarget(&a, ap));
        assert(!IsNefarianAddTargetExcluded(&a, &another));
        int before = targetReads; av.Reset(); assert(!av.Get().active && targetReads == before);
        a.strategies.clear();
    }
    a.context.positions["pull hold"].set = true;
    assert(!GetAssignedNefarianAddTarget(&a, ap));
    a.context.positions.clear();
    a.enabled = false; assert(!GetAssignedNefarianAddTarget(&a, ap)); a.enabled = true;
    a.context.units["current target"].value = &add;
    a.context.units["cc target"].value = &add;
    assert(!GetAssignedNefarianAddTarget(&a, ap));
    a.context.units["cc target"].value = nullptr;
    add.reserved = true;
    assert(!GetAssignedNefarianAddTarget(&a, ap)); // Reservation changes after cached planning.
    av.Reset(); bv.Reset(); ap = av.Get(); bp = bv.Get();
    for (auto const& owner : ap.owners) assert(owner.first != add.guid);
    add.reserved = false;
    add.victim = &otherTank; av.Reset(); bv.Reset(); ap = av.Get(); bp = bv.Get();
    assert(GetNefarianTankVictim(&a, &add) == &otherTank);
    assert(ap.owners[0].second == otherTank.guid);
    assert(!cv.Get().active); // Existing victim ownership must not enable a nonparticipant.
    NefarianAddAssignment stale; stale.active = true; stale.targets = {add.guid};
    assert(!GetAssignedNefarianAddTarget(&c, stale));
    add.victim = &healer; av.Reset(); bv.Reset();
    a.context.explicitTarget.value = boss.guid;
    ap = av.Get(); bp = bv.Get();
    assert(ap.targets.empty() && bp.targets.size() == 2); // Explicit boss order reserves that tank.
    a.context.explicitTarget.value = 0;
    a.enabled = false; bv.Reset();
    int before = planCalls; bp = bv.Get();
    assert(planCalls == before + 1 && bp.targets.size() == 2); // Planner handover.
}
'''
        with tempfile.TemporaryDirectory(prefix="nefarian-assignment-") as folder:
            source = pathlib.Path(folder) / "test.cpp"
            binary = pathlib.Path(folder) / "test"
            source.write_text(fixture)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
