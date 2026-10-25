"""Run production upstream triggers against selected local raid strategies."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tools.test_nefarian_bone_target import function


class RaidTacticPrecedenceTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_selected_local_tactics_own_overlapping_automation(self):
        root = Path(__file__).resolve().parents[1]
        def read(name):
            return (root / 'ai/playerbot' / name).read_text()
        geddon = function(read('strategy/triggers/MoltenCoreDungeonTriggers.cpp'),
                          'bool GeddonInfernoTrigger::IsActive()')
        thaddius = function(read('strategy/triggers/ThaddiusDungeonTriggers.cpp'),
                            'bool ThaddiusPhaseThaddiusTrigger::IsActive()')
        def inline_trigger(file, name):
            cls = function(read(file), 'class '+name+' :')
            return 'struct '+name+' : Trigger { using Trigger::Trigger;\n'+function(cls, 'bool IsActive() override')+'\n};\n'
        ony = inline_trigger('strategy/triggers/OnyxiasLairDungeonTriggers.h', 'OnyxiaDeepBreathTrigger')
        golemagg = inline_trigger('strategy/triggers/MoltenCoreDungeonTriggers.h', 'GolemaggTankHoldTrigger')
        ossirian = function(read('policy/raid/OssirianCrystalHelper.h'), 'inline bool IsOssirianCrystalRunner(')
        policy = read('policy/raid/GeddonInfernoPolicy.h').replace('#pragma once', '')
        fixture = policy + r'''
#include <cassert>
#include <list>
#include <set>
using namespace ai;
constexpr unsigned UNIT_FIELD_FLAGS=0, UNIT_FLAG_NOT_SELECTABLE=1;
struct ObjectGuid { unsigned id; bool IsEmpty() const { return id==0; } };
struct Unit {
    unsigned entry=0; bool alive=true, aura=true, stopped=true, selectable=true;
    bool IsAlive() const { return alive; }
    unsigned GetEntry() const { return entry; }
    bool HasAura(unsigned) const { return aura; }
    bool IsStopped() const { return stopped; }
    ObjectGuid GetTargetGuid() const { return {0}; }
    bool HasFlag(unsigned,unsigned) const { return !selectable; }
};
struct Player : Unit {
    bool tank=false, heal=false;
    bool IsInWorld() const { return true; }
    bool IsBeingTeleported() const { return false; }
    bool IsWithinDist(Unit*,float) const { return true; }
};
struct AiObjectContext {
    template<class T> T Value(std::string) { return {ObjectGuid{1}}; }
};
#define AI_VALUE(type,key) context->Value<type>(key)
enum class BotState { BOT_STATE_COMBAT };
struct PlayerbotAI {
    Player* bot; Unit* target; AiObjectContext context;
    std::set<std::string> strategies;
    bool HasStrategy(std::string name,BotState) const { return strategies.count(name); }
    static bool IsTank(Player* bot) { return bot->tank; }
    static bool IsHeal(Player* bot) { return bot->heal; }
    bool HasAura(unsigned,Unit* target) const { return target->aura; }
    Unit* GetUnit(ObjectGuid) { return target; }
    AiObjectContext* GetAiObjectContext() { return &context; }
};
struct { bool IsAlive(Unit* unit) const { return unit->alive; } } sServerFacade;
struct Trigger {
    PlayerbotAI* ai; Player* bot;
    Trigger(PlayerbotAI* ai):ai(ai),bot(ai->bot){}
    virtual bool IsActive() { return false; }
};
struct GeddonInfernoTrigger : Trigger { using Trigger::Trigger; bool IsActive() override; };
struct ThaddiusPhaseThaddiusTrigger : Trigger { using Trigger::Trigger; bool IsActive() override; };
void FindThaddiusAdds(PlayerbotAI* ai,Player*,Unit*& stalagg,Unit*& feugen,Unit*& thaddius) {
    stalagg=nullptr; feugen=nullptr; thaddius=ai->target;
}
bool PetActive(Unit* pet) { return pet && pet->alive && pet->selectable; }
bool IsThaddiusPhasePet(bool a,bool b) { return a || b; }
'''
        fixture += geddon + thaddius + ony + golemagg + ossirian + r'''
int main() {
    Player bot; Unit target; PlayerbotAI ai{&bot,&target,{}, {}};
    auto selected=[&](char const* base,char const* local,Trigger& trigger) {
        ai.strategies={base}; assert(trigger.IsActive());
        ai.strategies.insert(local); assert(!trigger.IsActive());
        ai.strategies.erase(local); assert(trigger.IsActive());
    };
    target.entry=12056; GeddonInfernoTrigger geddon(&ai);
    selected("geddon","geddon inferno",geddon);
    target.entry=10184; OnyxiaDeepBreathTrigger onyxia(&ai);
    selected("onyxia","onyxia deep breath",onyxia);
    target.entry=15928; ThaddiusPhaseThaddiusTrigger thaddius(&ai);
    selected("thaddius","thaddius polarity",thaddius);
    target.entry=11672; bot.tank=true; GolemaggTankHoldTrigger golemagg(&ai);
    selected("golemagg","golemagg boss tank",golemagg);
    selected("golemagg","golemagg add tank",golemagg);
    bot.tank=false; ai.strategies.clear(); assert(IsOssirianCrystalRunner(&ai,&bot));
    ai.strategies.insert("ossirian crystal"); assert(!IsOssirianCrystalRunner(&ai,&bot));
    ai.strategies.clear(); bot.tank=true; assert(!IsOssirianCrystalRunner(&ai,&bot));
    bot.tank=false; bot.heal=true; assert(!IsOssirianCrystalRunner(&ai,&bot));
}
'''
        with tempfile.TemporaryDirectory(prefix='raid-precedence-') as directory:
            source=Path(directory)/'test.cpp'; binary=Path(directory)/'test'
            source.write_text(fixture)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter',str(source),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
