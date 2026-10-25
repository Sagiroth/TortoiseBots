"""Run the production DPS orchestration and Infernal filter with target fixtures."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

from tools.test_nefarian_bone_target import function


class NefarianInfernalSelectionTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_toggle_orders_and_construct_priority(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        text = (root / "ai/playerbot/strategy/values/DpsTargetValue.cpp").read_text()
        dps = function(text, "class DpsTargetStrategy") + ";\n"
        infernal = function(text, "class NefarianInfernalTargetStrategy") + ";\n"
        calculate = function(text, "Unit* DpsTargetValue::Calculate()")
        policy = (root / "ai/playerbot/strategy/NefarianAddTargetPolicy.h").read_text()
        fixture = policy.replace("#pragma once", "") + r'''
#include <cassert>
#include <string>
#include <vector>
using namespace ai;
struct Unit { unsigned entry; int health = 100; bool reserved = false, excluded = false;
    unsigned GetEntry() const { return entry; } };
struct Player { unsigned map = 469; bool tank = false; unsigned GetMapId() const { return map; } };
struct AiObjectContext {
    Unit* cc = nullptr;
    template<class T> T Value(std::string) { return cc; }
};
#define AI_VALUE(type, key) context->Value<type>(key)
enum class BotState { BOT_STATE_COMBAT };
struct PlayerbotAI {
    Player bot;
    AiObjectContext context;
    bool enabled = false;
    bool HasStrategy(std::string name, BotState) const {
        assert(name == "nefarian infernal focus");
        return enabled;
    }
    bool IsTank(Player* player) const { return player->tank; }
    AiObjectContext* GetAiObjectContext() { return &context; }
};
struct ThreatManager {};
struct FindNonCcTargetStrategy {
    PlayerbotAI* ai;
    Unit* result = nullptr;
    explicit FindNonCcTargetStrategy(PlayerbotAI* ai) : ai(ai) {}
    bool IsCcTarget(Unit* unit) { return unit->reserved; }
    virtual void CheckAttacker(Unit*, ThreatManager*) = 0;
};
struct FindLeastHpTargetStrategy : FindNonCcTargetStrategy {
    using FindNonCcTargetStrategy::FindNonCcTargetStrategy;
    void CheckAttacker(Unit* unit, ThreatManager*) override {
        if (!IsCcTarget(unit) && (!result || unit->health < result->health)) result = unit;
    }
};
bool IsRaidDpsTargetExcluded(PlayerbotAI*, Unit* unit) { return unit->excluded; }
Unit *explicitTarget = nullptr, *raidTarget = nullptr, *construct = nullptr, *tankTarget = nullptr;
std::vector<Unit*> possible;
Unit* GetArlokkPantherTarget(PlayerbotAI*) { return nullptr; }
Unit* GetFankrissSpawnTarget(PlayerbotAI*) { return nullptr; }
Unit* GetAyamissLarvaTarget(PlayerbotAI*) { return nullptr; }
Unit* GetSkeramImageTarget(PlayerbotAI*) { return nullptr; }
Unit* GetMarliSpawnTarget(PlayerbotAI*) { return nullptr; }
Unit* GetThekalBalanceTarget(PlayerbotAI*) { return nullptr; }
Unit* GetNefarianBoneConstructTarget(PlayerbotAI*) { return construct; }
Unit* GetViscidusGlobTarget(PlayerbotAI*) { return nullptr; }
Unit* GetMaexxnaRescueTarget(PlayerbotAI*) { return nullptr; }
Unit* GetOnyxiaWhelpTarget(PlayerbotAI*) { return nullptr; }
Unit* GetGroupTankTarget(PlayerbotAI*) { return tankTarget; }
struct TargetValue {
    Unit* GetExplicitAttackTarget() { return explicitTarget; }
    Unit* FindTarget(FindNonCcTargetStrategy* strategy) {
        for (Unit* unit : possible) strategy->CheckAttacker(unit, nullptr);
        return strategy->result;
    }
};
struct RtiTargetValue : TargetValue { Unit* Calculate() { return raidTarget; } };
struct DpsTargetValue : RtiTargetValue {
    PlayerbotAI* ai;
    Player* bot;
    AiObjectContext* context;
    explicit DpsTargetValue(PlayerbotAI* ai) : ai(ai), bot(&ai->bot), context(&ai->context) {}
    Unit* Calculate();
};
'''
        fixture += dps + infernal + calculate + r'''
int main() {
    PlayerbotAI ai;
    DpsTargetValue value(&ai);
    Unit boss{11583, 1000}, low{14668, 10}, high{14668, 20}, bone{14605}, ordered{1};
    possible = {&boss, &high, &low};
    tankTarget = &boss;
    assert(value.Calculate() == &boss); // No opt-in: preserve ordinary tank assistance.
    ai.enabled = true;
    assert(value.Calculate() == &low);
    ai.bot.tank = true;
    assert(value.Calculate() == &boss);
    ai.bot.tank = false;
    ai.bot.map = 1;
    assert(value.Calculate() == &boss);
    ai.bot.map = 469;
    tankTarget = &ordered;
    assert(value.Calculate() == &ordered);
    tankTarget = &boss;
    construct = &bone; // Phase-three helper already established its own eligibility.
    assert(value.Calculate() == &bone);
    raidTarget = &ordered;
    assert(value.Calculate() == &ordered);
    explicitTarget = &high;
    assert(value.Calculate() == &high);
    explicitTarget = raidTarget = construct = nullptr;
    ai.context.cc = &low;
    assert(value.Calculate() == &high);
    high.reserved = true;
    assert(value.Calculate() == &boss);
    ai.context.cc = nullptr;
    low.excluded = true;
    assert(value.Calculate() == &boss);
}
'''
        with tempfile.TemporaryDirectory(prefix="nefarian-infernal-") as folder:
            source = pathlib.Path(folder) / "test.cpp"
            binary = pathlib.Path(folder) / "test"
            source.write_text(fixture)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
