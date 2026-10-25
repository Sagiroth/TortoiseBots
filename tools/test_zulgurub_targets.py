"""Production ZG selectors, DPS precedence and existing retarget trigger."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

from tools.test_nefarian_bone_target import function


class ZulGurubTargetTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_encounter_target_selection_and_retargeting(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / 'ai/playerbot/strategy/values/DpsTargetValue.cpp').read_text()
        triggers = (root / 'ai/playerbot/strategy/triggers/GenericTriggers.cpp').read_text()
        selector = ('Unit* GetThekalBalanceTarget(PlayerbotAI*) { return nullptr; }')
        if 'Unit* GetThekalBalanceTarget(' in source:
            selector = function(source, 'Unit* GetThekalBalanceTarget(')
        marli = 'Unit* GetMarliSpawnTarget(PlayerbotAI*) { return nullptr; }'
        if 'Unit* GetMarliSpawnTarget(' in source:
            marli = function(source, 'Unit* GetMarliSpawnTarget(')
        production = (function(source, 'class DpsTargetStrategy') + ';\n' + selector + '\n' + marli + '\n' +
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
Unit* GetNefarianBoneConstructTarget(PlayerbotAI*) { return nullptr; }
Unit* GetViscidusGlobTarget(PlayerbotAI*) { return nullptr; }
Unit* GetArlokkPantherTarget(PlayerbotAI*) { return nullptr; }
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
    Unit thekal{{1},14599,100}, lorkhan{{2},11347,300}, zath{{3},11348,200}, trash{{4},1,10000};
    ai.units={{thekal.guid,&thekal},{lorkhan.guid,&lorkhan},{zath.guid,&zath},{trash.guid,&trash}};
    ai.context.possible={trash.guid,thekal.guid,lorkhan.guid,zath.guid};
    tankTarget=&thekal;
    DpsTargetValue value(&ai);
    assert(value.Calculate()==&thekal && ai.context.listReads==0); // baseline boss assist
    ai.strategies.insert("thekal balance");
    assert(value.Calculate()==&lorkhan); // preserve weak priest while reducing the others
    ai.context.current=&lorkhan; lorkhan.health=120;
    ai.context.dps=value.Calculate();
    assert(ai.context.dps==&zath);
    NotDpsTargetActiveTrigger trigger{&ai.context};
    assert(trigger.IsActive()); // existing DPS-assist trigger actually requests a switch
    ai.context.current=&zath; assert(!trigger.IsActive());
    lorkhan.health=200; assert(value.Calculate()==&lorkhan); // deterministic equal-health tie
    ordered=&thekal; assert(value.Calculate()==ordered); ordered=nullptr;
    rti=&zath; assert(value.Calculate()==rti); rti=nullptr;
    lorkhan.reserved=true; assert(value.Calculate()==&zath); lorkhan.reserved=false;
    ai.context.cc=&lorkhan; assert(value.Calculate()==&zath); ai.context.cc=nullptr;
    lorkhan.controlled=true; assert(value.Calculate()==&zath); lorkhan.controlled=false;
    lorkhan.valid=false; assert(value.Calculate()==&zath); lorkhan.valid=true;
    lorkhan.excluded=true; assert(value.Calculate()==&zath); lorkhan.excluded=false;
    lorkhan.alive=false; assert(value.Calculate()==&zath);
    zath.alive=false; assert(value.Calculate()==&thekal); // finish remaining phase-one target
    thekal.alive=false; tankTarget=nullptr; assert(value.Calculate()==nullptr); // fake-dead trio
    thekal.alive=true; thekal.entry=14509; tankTarget=&thekal;
    assert(GetThekalBalanceTarget(&ai)==nullptr && value.Calculate()==&thekal); // tiger phase
    thekal.entry=14599; lorkhan.alive=zath.alive=true;
    ai.bot.tank=true; ai.context.listReads=0; assert(value.Calculate()==&thekal && ai.context.listReads==0);
    ai.bot.tank=false; ai.bot.map=1; assert(GetThekalBalanceTarget(&ai)==nullptr && ai.context.listReads==0);
    ai.bot.map=309; ai.bot.combat=false; assert(GetThekalBalanceTarget(&ai)==nullptr && ai.context.listReads==0);
    ai.bot.combat=true; ai.bot.alive=false; assert(GetThekalBalanceTarget(&ai)==nullptr && ai.context.listReads==0);
    ai.bot.alive=true; ai.context.possible={trash.guid}; PossibleAttackTargetsValue::checks=0;
    assert(GetThekalBalanceTarget(&ai)==nullptr && PossibleAttackTargetsValue::checks==0);
    ai.strategies.clear(); ai.context.possible={lorkhan.guid};
    assert(value.Calculate()==&thekal); // immediate opt-out

    Unit marli{{10},14510,1000}, low{{11},15041,20}, high{{12},15041,50};
    ai.units[marli.guid]=&marli; ai.units[low.guid]=&low; ai.units[high.guid]=&high;
    ai.context.possible={marli.guid,low.guid,high.guid}; tankTarget=&marli;
    ai.context.listReads=0;
    assert(value.Calculate()==&marli && ai.context.listReads==0);
    ai.strategies={"marli spawn focus"};
    assert(value.Calculate()==&low); // baseline keeps the boss while the spawn grows
    ai.context.current=&marli; ai.context.dps=value.Calculate(); assert(trigger.IsActive());
    ordered=&marli; assert(value.Calculate()==ordered); ordered=nullptr;
    rti=&marli; assert(value.Calculate()==rti); rti=nullptr;
    low.reserved=true; assert(value.Calculate()==&high); low.reserved=false;
    ai.context.cc=&low; assert(value.Calculate()==&high); ai.context.cc=nullptr;
    low.controlled=true; assert(value.Calculate()==&high); low.controlled=false;
    low.valid=false; assert(value.Calculate()==&high); low.valid=true;
    low.excluded=true; assert(value.Calculate()==&high); low.excluded=false;
    low.alive=false; assert(value.Calculate()==&high); high.alive=false;
    assert(value.Calculate()==&marli); low.alive=high.alive=true;
    marli.combat=false; assert(GetMarliSpawnTarget(&ai)==nullptr); marli.combat=true;
    marli.alive=false; assert(GetMarliSpawnTarget(&ai)==nullptr); marli.alive=true;
    marli.entry=14507; assert(GetMarliSpawnTarget(&ai)==nullptr); marli.entry=14510;
    ai.bot.tank=true; ai.context.listReads=0; assert(value.Calculate()==&marli && ai.context.listReads==0);
    ai.bot.tank=false; ai.bot.map=1; assert(GetMarliSpawnTarget(&ai)==nullptr && ai.context.listReads==0);
    ai.bot.map=309; ai.bot.combat=false; assert(GetMarliSpawnTarget(&ai)==nullptr && ai.context.listReads==0);
    ai.bot.combat=true; ai.bot.alive=false; assert(GetMarliSpawnTarget(&ai)==nullptr && ai.context.listReads==0);
    ai.bot.alive=true; ai.strategies.clear(); assert(value.Calculate()==&marli);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source = pathlib.Path(tmp) / 'fixture.cpp'
            binary = pathlib.Path(tmp) / 'fixture'
            source.write_text(fixture + production + checks)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
