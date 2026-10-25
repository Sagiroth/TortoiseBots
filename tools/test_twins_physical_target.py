"""Compile the production raid dispatch and DPS selector with small world stubs."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class TwinsPhysicalTargetTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_dispatch_selection_and_orders(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        actions = root / "ai/playerbot/strategy/actions"
        values = root / "ai/playerbot/strategy/values"
        dispatch = function((actions / "MoltenCoreDungeonActions.cpp").read_text(),
                            "bool ai::IsRaidDpsTargetExcluded(")
        twins = function((actions / "TwinEmperorDungeonActions.cpp").read_text(),
                         "bool ai::IsTwinPhysicalDpsTargetExcluded(")
        cthun = function((actions / "CthunDungeonActions.cpp").read_text(),
                         "bool ai::IsCthunCarapaceTargetExcluded(")
        source = (values / "DpsTargetValue.cpp").read_text()
        strategy = function(source, "class DpsTargetStrategy :") + ";\n"
        infernal = function(source, "class NefarianInfernalTargetStrategy :") + ";\n"
        selector = function(source, "Unit* DpsTargetValue::Calculate()")
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <list>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include "ai/playerbot/RaidDpsExclusionPolicy.h"
#include "ai/playerbot/strategy/NefarianAddTargetPolicy.h"
using uint32 = uint32_t;
using uint64 = uint64_t;
struct ObjectGuid {
    uint64 value = 0;
    ObjectGuid(uint64 value = 0) : value(value) {}
    bool operator==(ObjectGuid b) const { return value == b.value; }
    bool operator<(ObjectGuid b) const { return value < b.value; }
};
struct Unit {
    uint32 entry = 0, map = 531, health = 10;
    ObjectGuid guid;
    bool alive = true, combat = true, friendly = false, cc = false;
    bool carapace = false, inWorld = true;
    uint32 instance = 1;
    bool HasAura(uint32 id) const { return id == 26156 && carapace; }
    bool IsInWorld() const { return inWorld; }
    uint32 GetEntry() const { return entry; }
    uint32 GetMapId() const { return map; }
    bool IsAlive() const { return alive; }
    bool IsInCombat() const { return combat; }
    bool IsInMap(Unit* other) const { return map == other->map && instance == other->instance; }
    ObjectGuid GetObjectGuid() const { return guid; }
};
struct Group {
    uint64 icons[8] = {};
    uint64 GetTargetIcon(int index) { return icons[index]; }
};
struct Player : Unit {
    bool teleporting = false;
    bool IsBeingTeleported() const { return teleporting; }
    uint32 GetInstanceId() const { return instance; }
    Group* group = nullptr;
    Group* GetGroup() { return group; }
};
struct GameObject {
    bool disabled = false;
    bool HasFlag(uint32, uint32) const { return disabled; }
};
struct GuidPosition { GameObject* GetGameObject(uint32) const { return nullptr; } };
constexpr uint32 GAMEOBJECT_FLAGS = 0, GO_FLAG_NO_INTERACT = 1;
struct AiObjectContext {
    ObjectGuid order;
    std::string rti;
    Unit* cc = nullptr;
    std::list<ObjectGuid> attackers;
    template<class T> T Value(std::string) {
        if constexpr (std::is_same_v<T, ObjectGuid>) return order;
        else if constexpr (std::is_same_v<T, std::string>) return rti;
        else if constexpr (std::is_same_v<T, Unit*>) return cc;
        else if constexpr (std::is_same_v<T, std::list<ObjectGuid>>) return attackers;
        else return {};
    }
};
#define AI_VALUE(type, name) context->Value<type>(name)
enum class BotState { BOT_STATE_COMBAT };
struct PlayerbotAI {
    Player* bot;
    AiObjectContext context;
    std::set<std::string> strategies;
    std::map<ObjectGuid, Unit*> units;
    Unit* tankTarget = nullptr;
    bool tank = false;
    int strategyReads = 0;
    Player* GetBot() { return bot; }
    bool HasStrategy(std::string name, BotState) { ++strategyReads; return strategies.count(name); }
    bool IsTank(Player*) { return tank; }
    AiObjectContext* GetAiObjectContext() { return &context; }
    Unit* GetUnit(ObjectGuid guid) { return units[guid]; }
};
namespace ai {
    int faerlinaDispatchCalls = 0;
    bool IsFaerlinaWorshipperTargetExcluded(PlayerbotAI*, Unit* target) {
        ++faerlinaDispatchCalls; return target && target->GetEntry() == 16506;
    }
    int naxxDispatchCalls = 0;
    bool IsLoathebSporeTargetExcluded(PlayerbotAI*, Unit* target) {
        ++naxxDispatchCalls; return target && target->GetEntry() == 16286;
    }
    bool IsRaidDpsTargetExcluded(PlayerbotAI*, Unit*);
    bool IsTwinPhysicalDpsTargetExcluded(PlayerbotAI*, Unit*);
    bool IsCthunCarapaceTargetExcluded(PlayerbotAI*, Unit*);
}
using namespace ai;
struct { bool IsFriendlyTo(Player*, Unit* unit) { return unit->friendly; } } sServerFacade;
struct RtiTargetValue {
    PlayerbotAI* ai;
    static int GetRtiIndex(std::string name) { return name == "skull" ? 7 : -1; }
    Unit* Calculate() {
        auto* group = ai->bot->group;
        return group && ai->context.rti == "skull" ? ai->GetUnit(ObjectGuid(group->icons[7])) : nullptr;
    }
};
struct ThreatManager {};
struct FindNonCcTargetStrategy {
    PlayerbotAI* ai;
    explicit FindNonCcTargetStrategy(PlayerbotAI* ai) : ai(ai) {}
    bool IsCcTarget(Unit* unit) { return unit->cc; }
};
struct FindLeastHpTargetStrategy : FindNonCcTargetStrategy {
    Unit* selected = nullptr;
    using FindNonCcTargetStrategy::FindNonCcTargetStrategy;
    virtual void CheckAttacker(Unit* unit, ThreatManager*) {
        if (!IsCcTarget(unit) && (!selected || unit->health < selected->health)) selected = unit;
    }
};
struct TargetValue : RtiTargetValue {
    Player* bot;
    AiObjectContext* context;
    explicit TargetValue(PlayerbotAI* ai) : RtiTargetValue{ai}, bot(ai->bot), context(&ai->context) {}
    Unit* GetExplicitAttackTarget() { return ai->GetUnit(ai->context.order); }
    Unit* FindTarget(FindLeastHpTargetStrategy* strategy) {
        for (auto guid : context->attackers) strategy->CheckAttacker(ai->GetUnit(guid), nullptr);
        return strategy->selected;
    }
};
struct DpsTargetValue : TargetValue {
    using TargetValue::TargetValue;
    Unit* Calculate();
};
Unit* GetGroupTankTarget(PlayerbotAI* ai) { return ai->tankTarget; }
Unit* GetNefarianBoneConstructTarget(PlayerbotAI*) { return nullptr; }
Unit* GetViscidusGlobTarget(PlayerbotAI*) { return nullptr; }
Unit* GetFankrissSpawnTarget(PlayerbotAI*) { return nullptr; }
Unit* GetSkeramImageTarget(PlayerbotAI*) { return nullptr; }
Unit* GetMarliSpawnTarget(PlayerbotAI*) { return nullptr; }
Unit* GetThekalBalanceTarget(PlayerbotAI*) { return nullptr; }
Unit* GetArlokkPantherTarget(PlayerbotAI*) { return nullptr; }
Unit* GetMaexxnaRescueTarget(PlayerbotAI*) { return nullptr; }
Unit* GetOnyxiaWhelpTarget(PlayerbotAI*) { return nullptr; }
'''
        fixture += twins + cthun + dispatch + strategy + infernal + selector + r'''
int main() {
    Player bot;
    PlayerbotAI ai{&bot, {}, {}, {}, nullptr, false, 0};
    Unit veklor{15276, 531, 1, ObjectGuid(1)};
    Unit veknilash{15275, 531, 100, ObjectGuid(2)};
    Group group;
    bot.group = &group;
    ai.units = {{veklor.guid, &veklor}, {veknilash.guid, &veknilash}};
    ai.context.attackers = {veklor.guid, veknilash.guid};
    ai.tankTarget = &veklor;
    DpsTargetValue selector(&ai);
    assert(selector.Calculate() == &veklor); // Off by default.
    ai.strategies.insert("twins physical dps");
    assert(selector.Calculate() == &veknilash); // Skip the tank's immune twin.
    assert(IsRaidDpsTargetExcluded(&ai, &veklor));
    ai.tankTarget = nullptr;
    assert(selector.Calculate() == &veknilash); // Also skip the lower-HP immune twin.
    veknilash.cc = true;
    assert(selector.Calculate() == nullptr); // No allowed target: do not force a pull.
    veknilash.cc = false;
    ai.context.order = veklor.guid;
    assert(!IsRaidDpsTargetExcluded(&ai, &veklor));
    assert(selector.Calculate() == &veklor);
    ai.context.order = ObjectGuid();
    ai.context.rti = "skull";
    group.icons[7] = veklor.guid.value;
    assert(!IsRaidDpsTargetExcluded(&ai, &veklor));
    assert(selector.Calculate() == &veklor);
    group.icons[7] = 0;
    ai.context.rti.clear();
    ai.tank = true;
    assert(!IsRaidDpsTargetExcluded(&ai, &veklor));
    ai.tank = false;
    veklor.alive = false;
    assert(!IsRaidDpsTargetExcluded(&ai, &veklor));
    veklor.alive = true;
    veklor.combat = false;
    assert(!IsRaidDpsTargetExcluded(&ai, &veklor));
    veklor.combat = true;
    bot.combat = false;
    assert(!IsRaidDpsTargetExcluded(&ai, &veklor));
    bot.combat = true;
    bot.alive = false;
    assert(!IsRaidDpsTargetExcluded(&ai, &veklor));
    bot.alive = true;
    veklor.map = 0;
    assert(!IsRaidDpsTargetExcluded(&ai, &veklor));
    veklor.map = 531;
    assert(!IsRaidDpsTargetExcluded(&ai, nullptr));
    ai.strategyReads = 0;
    assert(!IsRaidDpsTargetExcluded(&ai, &veknilash));
    assert(ai.strategyReads == 0); // Unrelated entries avoid strategy/group work.
    bot.map = 0;
    assert(!IsRaidDpsTargetExcluded(&ai, &veklor));
    assert(ai.strategyReads == 0);
    bot.map = 531;
    ai.strategies.clear();
    assert(!IsRaidDpsTargetExcluded(&ai, &veklor));

    // C'Thun's shell is damage reduction, not generic immunity. Test the
    // actual DPS selector both when assisting a tank and when choosing low HP.
    Unit body{15727, 531, 1, ObjectGuid(10)};
    Unit tentacle{15726, 531, 100, ObjectGuid(11)};
    body.carapace = true;
    ai.units[body.guid] = &body;
    ai.units[tentacle.guid] = &tentacle;
    ai.context.attackers = {body.guid, tentacle.guid};
    ai.tankTarget = &body;
    assert(selector.Calculate() == &body); // Off by default.
    ai.strategies = {"cthun carapace"};
    assert(selector.Calculate() == &tentacle); // Previously attacked the shelled body.
    assert(IsRaidDpsTargetExcluded(&ai, &body));
    ai.tankTarget = nullptr;
    assert(selector.Calculate() == &tentacle);
    tentacle.cc = true;
    assert(selector.Calculate() == nullptr);
    tentacle.cc = false;
    body.carapace = false; // Flesh Tentacles killed: normal body DPS becomes legal.
    assert(!IsRaidDpsTargetExcluded(&ai, &body));
    assert(selector.Calculate() == &body);
    body.carapace = true; // Shell reapplied: ordinary retarget guard becomes active again.
    assert(IsRaidDpsTargetExcluded(&ai, &body));
    assert(selector.Calculate() == &tentacle);
    ai.context.order = body.guid;
    assert(!IsRaidDpsTargetExcluded(&ai, &body));
    assert(selector.Calculate() == &body);
    ai.context.order = ObjectGuid();
    ai.context.rti = "skull"; group.icons[7] = body.guid.value;
    assert(!IsRaidDpsTargetExcluded(&ai, &body));
    assert(selector.Calculate() == &body);
    ai.context.rti.clear(); group.icons[7] = 0;
    ai.tank = true;
    assert(!IsRaidDpsTargetExcluded(&ai, &body));
    ai.tank = false;
    for (bool* flag : {&bot.alive, &bot.combat, &bot.inWorld, &body.alive, &body.combat}) {
        *flag = false; assert(!IsRaidDpsTargetExcluded(&ai, &body)); *flag = true;
    }
    bot.teleporting = true;
    assert(!IsRaidDpsTargetExcluded(&ai, &body)); bot.teleporting = false;
    body.map = 0; assert(!IsRaidDpsTargetExcluded(&ai, &body)); body.map = 531;
    body.instance = 2; assert(!IsRaidDpsTargetExcluded(&ai, &body)); body.instance = 1;
    ai.strategyReads = 0;
    body.carapace = false;
    assert(!IsRaidDpsTargetExcluded(&ai, &body));
    assert(ai.strategyReads == 0); // No strategy work in the vulnerable window.
    body.carapace = true;
    for (uint32 entry : {15589u, 15726u, 15802u}) { // Eye, surface and stomach tentacles unchanged.
        body.entry = entry;
        assert(!IsRaidDpsTargetExcluded(&ai, &body));
        assert(ai.strategyReads == 0);
    }
    body.entry = 15727;
    bot.map = 0;
    assert(!IsRaidDpsTargetExcluded(&ai, &body));
    assert(ai.strategyReads == 0); bot.map = 531;
    assert(!IsRaidDpsTargetExcluded(&ai, nullptr));
    ai.strategies.clear();
    assert(!IsRaidDpsTargetExcluded(&ai, &body));
    assert(selector.Calculate() == &body);
    ai.strategies = {"twins physical dps", "cthun carapace"};
    assert(IsRaidDpsTargetExcluded(&ai, &body));
    assert(IsRaidDpsTargetExcluded(&ai, &veklor));

    bot.map = 533;
    Unit spore{16286, 533, 100, ObjectGuid(5)};
    assert(IsRaidDpsTargetExcluded(&ai, &spore) && naxxDispatchCalls == 1);
    Unit worshipper{16506, 533, 100, ObjectGuid(6)};
    assert(IsRaidDpsTargetExcluded(&ai, &worshipper) && faerlinaDispatchCalls == 1);

    // Original MC/BWL dispatch behavior: AQ's additive hook cannot capture it.
    Unit majordomo{12018, 409, 100, ObjectGuid(3)};
    bot.map = 409;
    ai.strategies = {"majordomo dps", "twins physical dps"};
    assert(IsRaidDpsTargetExcluded(&ai, &majordomo));
    ai.context.order = majordomo.guid;
    assert(!IsRaidDpsTargetExcluded(&ai, &majordomo));
    ai.context.order = ObjectGuid();
    ai.tank = true;
    assert(!IsRaidDpsTargetExcluded(&ai, &majordomo));
    ai.tank = false;
    Unit razorgore{12435, 469, 100, ObjectGuid(4)};
    bot.map = 469;
    ai.strategies = {"razorgore phase1", "twins physical dps"};
    assert(IsRaidDpsTargetExcluded(&ai, &razorgore));
    ai.context.order = razorgore.guid;
    assert(!IsRaidDpsTargetExcluded(&ai, &razorgore));
}
'''
        with tempfile.TemporaryDirectory(prefix="twins-physical-") as folder:
            source = pathlib.Path(folder) / "test.cpp"
            binary = pathlib.Path(folder) / "test"
            source.write_text(fixture)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I", str(root), str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
