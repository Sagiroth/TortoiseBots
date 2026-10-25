"""Run production assignment, participation, victim-role and return checks."""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

from tools.test_nefarian_bone_target import function


class GarrAssignmentCallerTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_roles_holds_cc_and_shared_cache(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        text = (root / "ai/playerbot/strategy/actions/GarrTankActions.cpp").read_text()
        header = (root / "ai/playerbot/strategy/values/GarrTankAssignmentValue.h").read_text()
        policy = (root / "ai/playerbot/RaidTankAssignmentPolicy.h").read_text()
        value = (root / "ai/playerbot/strategy/Value.h").read_text()
        get = function(value, "virtual T Get() override")
        # Substitute only the clock so the real cache predicate is deterministic.
        refresh = function(value, "virtual void Refresh()")
        refresh = refresh.replace("time_t now = time(0);", "time_t now = fixtureNow;")
        fixture = policy.replace("#pragma once", "").replace("PlanGolemaggTanks(", "RealPlanGolemaggTanks(")
        fixture += r'''
#include <cassert>
#include <ctime>
#include <list>
#include <map>
#include <set>
#include <string>
#include <type_traits>
using namespace ai;
using int32=int;using uint32=unsigned;
struct Aura{unsigned GetStackAmount(){return 0;}};
int planCalls = 0, targetReads = 0;
time_t fixtureNow = 100;
namespace ai {
auto PlanGolemaggTanks(std::vector<AssignedRaidTankCandidate> tanks,
    std::uint64_t boss, std::vector<std::uint64_t> adds) {
    ++planCalls;
    return RealPlanGolemaggTanks(tanks, boss, adds);
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
    unsigned entry = 0, map = 409;
    float health = 100; float GetHealthPercent() const { return health; }
    bool alive = true, combat = true, valid = true, reserved = false, controlled = false;
    bool interrupted = false, stopped = false;
    void InterruptNonMeleeSpells(bool) { interrupted=true; }
    void AttackStop() { stopped=true; victim=nullptr; }
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
template<class T> struct Value { virtual ~Value() = default; virtual T Get() = 0; virtual void Reset() {} virtual void Set(T) {} };
template<class T> struct Manual : Value<T> {
    T value{};
    bool reset = false;
    void Reset() override { reset=true; value=T{}; }
    void Set(T v) override { value=v; }
    T Get() override { if constexpr (std::is_same_v<T,std::list<ObjectGuid>>) ++targetReads; return value; }
};
'''
        fixture += function(header, "struct GarrTankAssignment") + ";\n"
        fixture += r'''
struct AiObjectContext {
    PositionMap positions;
    std::map<std::string, Manual<Unit*>> units;
    Manual<int32> integer;
    Manual<bool> boolean;
    Manual<ObjectGuid> explicitTarget;
    Manual<std::list<ObjectGuid>> possible;
    Value<GarrTankAssignment>* assignment = nullptr;
    template<class T> Value<T>* GetValue(std::string key, std::string = "") {
        if constexpr (std::is_same_v<T,int32>) return &integer;
        else if constexpr (std::is_same_v<T,bool>) return &boolean;
        else if constexpr (std::is_same_v<T,Unit*>) return &units[key];
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
#define AI_VALUE2(type, key, qualifier) fixtureHoldsCurrent
#define SET_AI_VALUE(type, key, value) context->GetValue<type>(key)->Set(value)
struct Event {};
bool fixtureHoldsCurrent = false;
enum class BotState { BOT_STATE_COMBAT };
std::map<ObjectGuid,Unit*> world;
struct PlayerbotAI {
    Player* bot;
    AiObjectContext context;
    bool enabled = false, moving = true;
    void StopMoving() { moving=false; }
    bool DoSpecificAction(std::string, Event&, bool);
    std::set<std::string> strategies;
    explicit PlayerbotAI(Player* bot) : bot(bot) { bot->ai = this; }
    Aura* GetAura(uint32,Player*){return nullptr;}
    Player* GetBot() const { return bot; }
    AiObjectContext* GetAiObjectContext() { return &context; }
    Unit* GetUnit(ObjectGuid guid) { return world[guid]; }
    bool IsTank(Player* p) const { return p->tank; }
    bool HasStrategy(std::string name, BotState) const {
        return enabled && strategies.count(name) != 0;
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
bool IsRaidCrowdControlMarkedTarget(PlayerbotAI*, Unit* u) { return u->reserved; }
struct { float sightDistance = 60, lowHealth = 20; } sPlayerbotAIConfig;
template<class T> class CalculatedValue : public Value<T> {
public:
    PlayerbotAI* ai;
    bool keepLegacyRefresh=false;
    int checkInterval;
    time_t lastCheckTime = 0;
    T value{};
    CalculatedValue(PlayerbotAI* ai, std::string, int interval) : ai(ai), checkInterval(interval) {}
    virtual T Calculate() = 0;
    void Reset() { lastCheckTime = 0; }
'''
        fixture = (root / "ai/playerbot/strategy/ValueRefreshPolicy.h").read_text().replace("#pragma once", "") + fixture
        fixture += refresh + "\n" + get + "\n};\n"
        fixture += function(header, "class GarrTankAssignmentValue") + ";\n"
        fixture += r'''
struct GarrTankRetargetAction { PlayerbotAI* ai; AiObjectContext* context; Player* bot=ai->GetBot(); bool isUseful(); bool Execute(Event&); };
struct TankAssistTrigger { AiObjectContext* context;PlayerbotAI* ai;Player* bot=ai->GetBot(); bool IsActive(); };
'''
        for signature in ["bool GarrHoldOrder(", "Unit* GarrExplicitTarget(",
                          "bool GarrCcTarget(", "bool GarrAssignmentActive(",
                          "bool ai::IsGarrTankOptedIn(",
                          "GarrTankAssignment GarrTankAssignmentValue::Calculate()",
                          "Unit* ai::GetAssignedGarrTankTarget(",
                          "bool ai::IsGarrTankTargetExcluded(",
                          "bool GarrTankRetargetAction::isUseful()",
                          "bool GarrTankRetargetAction::Execute("]:
            fixture += function(text, signature).replace("ai::", "") + "\n"
        triggers = (root / "ai/playerbot/strategy/triggers/GenericTriggers.cpp").read_text()
        fixture = (root / "ai/playerbot/FourHorsemenRotationPolicy.h").read_text().replace("#pragma once", "") + fixture
        fixture += function(triggers, "bool TankAssistTrigger::IsActive()")
        fixture += r'''
bool PlayerbotAI::DoSpecificAction(std::string name, Event&, bool force) {
    assert(name=="tank assist" && force);
    assert(context.units["tank target"].reset);
    context.units["current target"].value=GetAssignedGarrTankTarget(this);
    return context.units["current target"].value!=nullptr;
}
int main() {
    Player mainTank, first, second, third, healer;
    mainTank.guid=5; first.guid=10; second.guid=20; third.guid=30; healer.guid=40; healer.tank=false;
    PlayerbotAI m(&mainTank), a(&first), b(&second), c(&third);
    GarrTankAssignmentValue mv(&m), av(&a), bv(&b), cv(&c);
    m.context.assignment=&mv; a.context.assignment=&av; b.context.assignment=&bv; c.context.assignment=&cv;
    Group group{{&third,&first,&healer,&mainTank,&second}};
    for(Player* p:group.members) p->group=&group;
    Unit boss; boss.guid=100; boss.entry=12057; boss.victim=&mainTank; mainTank.victim=&boss;
    world[100]=&boss;
    Unit adds[8];
    for(int i=0;i<8;++i) { adds[i].guid=101+i; adds[i].entry=12099; adds[i].victim=&healer; world[101+i]=&adds[i]; }
    m.enabled=a.enabled=b.enabled=c.enabled=true;
    m.strategies={"garr boss tank"};
    a.strategies=b.strategies=c.strategies={"garr add tank"};
    // Different order and subsets must still yield one shared owner map.
    m.context.possible.value={100};a.context.possible.value={101,103,105,107};
    b.context.possible.value={108,106,104,102};c.context.possible.value={108,101};
    auto reset=[&]{mv.Reset();av.Reset();bv.Reset();cv.Reset();};
    auto cp=cv.Get();auto ap=av.Get();auto bp=bv.Get();auto mp=mv.Get();
    assert(planCalls==1 && targetReads==4);
    assert(mp.targets.size()==1 && mp.targets[0]==boss.guid);
    assert(ap.targets.size()==3 && bp.targets.size()==3 && cp.targets.size()==2);
    assert(GetAssignedGarrTankTarget(&m)==&boss);
    assert(GetAssignedGarrTankTarget(&a)==&adds[0]);
    assert(GetAssignedGarrTankTarget(&b)==&adds[1]);
    assert(GetAssignedGarrTankTarget(&c)==&adds[2]);
    assert(IsGarrTankTargetExcluded(&a,&boss));
    assert(IsGarrTankTargetExcluded(&a,&adds[1]));
    assert(!IsGarrTankTargetExcluded(&a,&adds[0]));
    // Tank holds one low-health add while another assigned add attacks priest.
    adds[0].victim=&first;adds[0].health=10;a.context.units["current target"].value=&adds[0];
    assert(GetAssignedGarrTankTarget(&a)==&adds[3]);
    a.context.units["tank target"].value=&adds[3];a.context.boolean.value=true;
    fixtureHoldsCurrent=true;
    TankAssistTrigger old{&a.context,&a};GarrTankRetargetAction revised{&a,&a.context};
#ifdef LEGACY
    assert(old.IsActive()); // Deployed generic low-health gate strands the priest.
#else
    assert(!old.IsActive());assert(revised.isUseful());
#endif
    Event event;
    assert(revised.Execute(event));
    assert(first.interrupted && first.stopped && !a.moving);
    assert(a.context.units["current target"].value==&adds[3]);
    // Also recover from the original wrong target while it attacks another tank.
    a.context.units["current target"].value=&boss;fixtureHoldsCurrent=false;
    assert(!old.IsActive());assert(revised.isUseful());
    adds[3].reserved=true;assert(GetAssignedGarrTankTarget(&a)==&adds[6]);
    adds[6].controlled=true;assert(GetAssignedGarrTankTarget(&a)==&adds[0]);
    adds[0].valid=false;assert(!GetAssignedGarrTankTarget(&a));
    adds[0].valid=true;adds[3].reserved=false;adds[6].controlled=false;
    a.context.explicitTarget.value=boss.guid;
    assert(!GetAssignedGarrTankTarget(&a));assert(!IsGarrTankTargetExcluded(&a,&boss));
    a.context.explicitTarget.value=0;a.context.units["rti target"].value=&boss;
    assert(!revised.isUseful());a.context.units["rti target"].value=nullptr;
    for(std::string hold:{"stay","follow","wait for attack","grind"}) {
        a.strategies.insert(hold);assert(!GetAssignedGarrTankTarget(&a));assert(!revised.isUseful());a.strategies.erase(hold);
    }
    a.context.positions["pull hold"].set=true;assert(!revised.isUseful());a.context.positions.clear();
    a.enabled=false;assert(!revised.isUseful());a.enabled=true;
    first.map=1;assert(!revised.isUseful());first.map=409;
    first.tank=false;assert(!revised.isUseful());first.tank=true;
    boss.alive=false;assert(!revised.isUseful());boss.alive=true;
    boss.combat=false;assert(!revised.isUseful());boss.combat=true;
    // Dead participant no longer owns adds; leader handover still has one boss.
    a.context.possible.value={101,102,103,104,105,106,107,108};
    second.alive=false;reset();ap=av.Get();cp=cv.Get();
    assert(ap.targets.size()==4 && cp.targets.size()==4);
    mainTank.ai=nullptr;reset();ap=av.Get();cp=cv.Get();
    assert(ap.targets.size()==4 && cp.targets.size()==4); // Human holding Garr reserved.
    assert(av.Get().owners[0].second==mainTank.guid);
    // Stop combat: no stale plan can cause a fresh pull or dead-add selection.
    for(auto& add:adds) add.combat=false;
    assert(!GetAssignedGarrTankTarget(&a));
    assert(IsGarrTankTargetExcluded(&a,&adds[0]));
}
'''
        with tempfile.TemporaryDirectory(prefix="garr-assignment-") as folder:
            source = pathlib.Path(folder) / "test.cpp"
            binary = pathlib.Path(folder) / "test"
            source.write_text(fixture)
            flags = ["-DLEGACY"] if os.environ.get("TBOTS_GARR_LEGACY") else []
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", *flags,
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
