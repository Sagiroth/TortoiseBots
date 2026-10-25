"""Production ZG selectors, DPS precedence and existing retarget trigger."""
from tools.raid_dps_fixture import tournament, world
import pathlib
import shutil
import subprocess
import tempfile
import unittest

from tools.test_nefarian_bone_target import function


class ArlokkTargetTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_encounter_target_selection_and_retargeting(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / 'ai/playerbot/strategy/values/DpsTargetValue.cpp').read_text()
        triggers = (root / 'ai/playerbot/strategy/triggers/GenericTriggers.cpp').read_text()
        selector = 'Unit* GetArlokkPantherTarget(PlayerbotAI*) { return nullptr; }'
        if 'Unit* GetArlokkPantherTarget(' in source:
            selector = function(source, 'Unit* GetArlokkPantherTarget(')
        production = (function(source, 'class DpsTargetStrategy') + ';\n' + tournament(source) + selector + '\n' +
                      function(source, 'Unit* DpsTargetValue::Calculate()') + '\n' +
                      function(triggers, 'bool NotDpsTargetActiveTrigger::IsActive()'))
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <list>
#include <map>
#include <set>
#include <string>
using uint32=std::uint32_t;
struct ObjectGuid {
    unsigned value=0;
    ObjectGuid(unsigned value=0):value(value){}
    bool operator<(ObjectGuid b) const { return value<b.value; }
    bool operator==(ObjectGuid b) const { return value==b.value; }
};
struct Unit {
    ObjectGuid guid;
    unsigned entry, health;
    bool alive=true, combat=true, valid=true, reserved=false, excluded=false, controlled=false;
    unsigned map=309;
    uint32 GetEntry() const { return entry; }
    uint32 GetHealth() const { return health; }
    bool IsAlive() const { return alive; }
    bool IsInCombat() const { return combat; }
    bool IsPlayer() const { return false; }
    bool IsInMap(Unit* b) const { return map==b->map; }
    ObjectGuid GetObjectGuid() const { return guid; }
};
struct Player : Unit {
    Player():Unit{{0},0,100}{}
    bool tank=false;
    unsigned GetMapId() const { return map; }
};
struct AiObjectContext {
    std::list<ObjectGuid> possible;
    Unit *cc=nullptr, *current=nullptr, *dps=nullptr;
    unsigned listReads=0;
    template<class T> T Value(std::string);
};
template<> std::list<ObjectGuid> AiObjectContext::Value(std::string) { ++listReads; return possible; }
template<> Unit* AiObjectContext::Value(std::string key) {
    if (key=="cc target") return cc;
    if (key=="current target") return current;
    if (key=="dps target") return dps;
    return nullptr;
}
#define AI_VALUE(type,key) context->Value<type>(key)
enum class BotState { BOT_STATE_COMBAT };
struct PlayerbotAI {
    Player bot;
    AiObjectContext context;
    std::set<std::string> strategies;
    std::map<ObjectGuid, Unit*> units;
    Player* GetBot() { return &bot; }
    bool IsTank(Player* b) const { return b->tank; }
    bool HasStrategy(std::string name, BotState) const { return strategies.count(name); }
    AiObjectContext* GetAiObjectContext() { return &context; }
    Unit* GetUnit(ObjectGuid id) { return units[id]; }
};
struct { float sightDistance=60; } sPlayerbotAIConfig;
struct ThreatManager {};
struct {
    bool IsAlive(Unit* u) { return u->IsAlive(); }
    ThreatManager& GetThreatManager(Unit*) { static ThreatManager threat; return threat; }
} sServerFacade;
struct PossibleAttackTargetsValue {
    static unsigned checks;
    static bool IsValid(Unit* u, Player*, float, bool, bool) { ++checks; return u->valid && !u->controlled; }
};
unsigned PossibleAttackTargetsValue::checks=0;
struct FindNonCcTargetStrategy {
    PlayerbotAI* ai;
    Unit* result=nullptr;
    explicit FindNonCcTargetStrategy(PlayerbotAI* ai):ai(ai){}
    bool IsCcTarget(Unit* u) { return u->reserved; }
    Unit* GetResult() { return result; }
    virtual void CheckAttacker(Unit*, ThreatManager*)=0;
};
struct FindLeastHpTargetStrategy : FindNonCcTargetStrategy {
    using FindNonCcTargetStrategy::FindNonCcTargetStrategy;
    void CheckAttacker(Unit* u, ThreatManager*) override {
        if (!u->reserved && (!result || u->health<result->health)) result=u;
    }
};
struct NefarianInfernalTargetStrategy : FindLeastHpTargetStrategy { using FindLeastHpTargetStrategy::FindLeastHpTargetStrategy; };
constexpr unsigned NEFARIAN_CORRUPTED_INFERNAL_ENTRY=14668;
bool IsNefarianClassCallAdd(unsigned,unsigned,unsigned) { return false; }
bool IsRaidDpsTargetExcluded(PlayerbotAI*,Unit* u) { return u->excluded; }
Unit *ordered=nullptr, *rti=nullptr, *tankTarget=nullptr;
bool SelectCthunSpreadTarget(PlayerbotAI*,Unit*&) { return false; }
Unit* GetNefarianBoneConstructTarget(PlayerbotAI*) { return nullptr; }
Unit* GetFankrissSpawnTarget(PlayerbotAI*) { return nullptr; }
Unit* GetAyamissLarvaTarget(PlayerbotAI*) { return nullptr; }
Unit* GetSkeramImageTarget(PlayerbotAI*) { return nullptr; }
Unit* GetMarliSpawnTarget(PlayerbotAI*) { return nullptr; }
Unit* GetThekalBalanceTarget(PlayerbotAI*) { return nullptr; }
Unit* GetViscidusGlobTarget(PlayerbotAI*) { return nullptr; }
Unit* GetMaexxnaRescueTarget(PlayerbotAI*) { return nullptr; }
Unit* GetOnyxiaWhelpTarget(PlayerbotAI*) { return nullptr; }
Unit* GetGroupTankTarget(PlayerbotAI*) { return tankTarget; }
struct TargetValue {
    Unit* GetExplicitAttackTarget() { return ordered; }
    Unit* FindTarget(FindNonCcTargetStrategy*) { return nullptr; }
};
struct RtiTargetValue : TargetValue { Unit* Calculate() { return rti; } };
struct DpsTargetValue : RtiTargetValue {
    PlayerbotAI* ai;
    Player* bot;
    AiObjectContext* context;
    explicit DpsTargetValue(PlayerbotAI* ai):ai(ai),bot(&ai->bot),context(&ai->context){}
    Unit* Calculate();
};
struct NotDpsTargetActiveTrigger {
    AiObjectContext* context;
    bool IsActive();
};
'''
        checks = r'''
int main() {
    PlayerbotAI ai;
    DpsTargetValue value(&ai);
    NotDpsTargetActiveTrigger trigger{&ai.context};
    Unit arlokk{{10},14515,1000}, low{{11},15101,20}, high{{12},15101,50};
    ai.units[arlokk.guid]=&arlokk; ai.units[low.guid]=&low; ai.units[high.guid]=&high;
    ai.context.possible={arlokk.guid,low.guid,high.guid}; tankTarget=&arlokk;
    ai.context.listReads=0;
    assert(value.Calculate()==&arlokk && ai.context.listReads==0);
    ai.strategies={"arlokk panther focus"};
    assert(value.Calculate()==&low); // baseline stays on the boss while marked players are attacked
    ai.context.current=&arlokk; ai.context.dps=value.Calculate(); assert(trigger.IsActive());
    ordered=&arlokk; assert(value.Calculate()==ordered); ordered=nullptr;
    rti=&arlokk; assert(value.Calculate()==rti); rti=nullptr;
    low.reserved=true; assert(value.Calculate()==&high); low.reserved=false;
    ai.context.cc=&low; assert(value.Calculate()==&high); ai.context.cc=nullptr;
    low.controlled=true; assert(value.Calculate()==&high); low.controlled=false;
    low.valid=false; assert(value.Calculate()==&high); low.valid=true;
    low.excluded=true; assert(value.Calculate()==&high); low.excluded=false;
    low.alive=false; assert(value.Calculate()==&high); high.alive=false;
    assert(value.Calculate()==&arlokk); low.alive=high.alive=true;
    ai.context.possible={low.guid,high.guid}; tankTarget=nullptr;
    assert(value.Calculate()==&low); // boss vanished: engaged panthers remain actionable
    low.combat=false; assert(value.Calculate()==&high); low.combat=true;
    low.map=1; assert(value.Calculate()==&high); low.map=309;
    low.entry=1; assert(value.Calculate()==&high); low.entry=15101;
    low.valid=high.valid=false; assert(GetArlokkPantherTarget(&ai)==nullptr);
    low.valid=high.valid=true; tankTarget=&arlokk;
    ai.bot.tank=true; ai.context.listReads=0; assert(value.Calculate()==&arlokk && ai.context.listReads==0);
    ai.bot.tank=false; ai.bot.map=1; assert(GetArlokkPantherTarget(&ai)==nullptr && ai.context.listReads==0);
    ai.bot.map=309; ai.bot.combat=false; assert(GetArlokkPantherTarget(&ai)==nullptr && ai.context.listReads==0);
    ai.bot.combat=true; ai.bot.alive=false; assert(GetArlokkPantherTarget(&ai)==nullptr && ai.context.listReads==0);
    ai.bot.alive=true; ai.strategies.clear(); assert(value.Calculate()==&arlokk);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source = pathlib.Path(tmp) / 'fixture.cpp'
            binary = pathlib.Path(tmp) / 'fixture'
            source.write_text(world(fixture) + production + checks)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
