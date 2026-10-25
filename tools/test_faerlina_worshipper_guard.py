"""Run production Faerlina scope/exclusion functions against unit/order seams."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


def function(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


class FaerlinaWorshipperGuardTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_scope_orders_and_controlled_adds(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / 'ai/playerbot/strategy/actions/NaxxramasDungeonActions.cpp').read_text()
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <list>
#include <string>
#include <type_traits>
using ObjectGuid=uint64_t;
struct Player;
struct Unit {
    unsigned entry=0,map=533; ObjectGuid guid=1; bool alive=true,combat=true,friendly=false;
    unsigned GetEntry(){return entry;} ObjectGuid GetObjectGuid(){return guid;}
    bool IsAlive(){return alive;} bool IsInCombat(){return combat;}
    bool IsInMap(Player*); bool IsFriendlyTo(Player*){return friendly;}
};
struct Group {ObjectGuid mark=0; ObjectGuid GetTargetIcon(int){return mark;}};
struct Player : Unit {
    Group group; bool world=true,teleport=false;
    unsigned GetMapId(){return map;} bool IsInWorld(){return world;}
    bool IsBeingTeleported(){return teleport;} Group* GetGroup(){return &group;}
};
bool Unit::IsInMap(Player* p){return map==p->map;}
struct AiObjectContext {
    std::list<ObjectGuid> attackers{99}; ObjectGuid explicitTarget=0;
    template<class T> T Value(const char*) {
        if constexpr (std::is_same_v<T,std::list<ObjectGuid>>) return attackers;
        else if constexpr (std::is_same_v<T,ObjectGuid>) return explicitTarget;
        else return "skull";
    }
};
#define AI_VALUE(type,key) context->Value<type>(key)
enum class BotState{BOT_STATE_COMBAT};
struct PlayerbotAI {
    Player bot; Unit boss; AiObjectContext context; bool enabled=true;
    unsigned lookups=0;
    PlayerbotAI(){boss.entry=15953;boss.guid=99;}
    Player* GetBot(){return &bot;}
    bool HasStrategy(std::string name,BotState){assert(name=="faerlina worshipper guard");return enabled;}
    AiObjectContext* GetAiObjectContext(){return &context;}
    Unit* GetUnit(ObjectGuid guid){++lookups;return guid==99 ? &boss : nullptr;}
};
struct RtiTargetValue {static int GetRtiIndex(std::string){return 7;}};
namespace ai {
Unit* GetActiveFaerlinaWorshipperGuard(PlayerbotAI*);
bool IsFaerlinaWorshipperTargetExcluded(PlayerbotAI*,Unit*);
}
using namespace ai;
'''
        fixture += function(source, 'Unit* ai::GetActiveFaerlinaWorshipperGuard(')
        fixture += function(source, 'bool ai::IsFaerlinaWorshipperTargetExcluded(')
        fixture += r'''
int main() {
    PlayerbotAI ai; Unit worshipper; worshipper.entry=16506;
    assert(IsFaerlinaWorshipperTargetExcluded(&ai,&worshipper));
    ai.context.explicitTarget=worshipper.guid;
    assert(!IsFaerlinaWorshipperTargetExcluded(&ai,&worshipper));
    ai.context.explicitTarget=0; ai.bot.group.mark=worshipper.guid;
    assert(!IsFaerlinaWorshipperTargetExcluded(&ai,&worshipper));
    ai.bot.group.mark=0;
    // Friendly charmed adds must remain eligible for support spells.
    worshipper.friendly=true; assert(!IsFaerlinaWorshipperTargetExcluded(&ai,&worshipper));
    worshipper.friendly=false; worshipper.entry=16505;
    ai.lookups=0; assert(!IsFaerlinaWorshipperTargetExcluded(&ai,&worshipper)); assert(ai.lookups==0);
    worshipper.entry=16506; ai.enabled=false;
    assert(!IsFaerlinaWorshipperTargetExcluded(&ai,&worshipper)); assert(ai.lookups==0);
    ai.enabled=true; ai.bot.map=469;
    assert(!GetActiveFaerlinaWorshipperGuard(&ai)); assert(ai.lookups==0);
    ai.bot.map=533; ai.boss.entry=15954;
    assert(!IsFaerlinaWorshipperTargetExcluded(&ai,&worshipper));
    ai.boss.entry=15953; ai.boss.combat=false;
    assert(!IsFaerlinaWorshipperTargetExcluded(&ai,&worshipper));
    ai.boss.combat=true; ai.bot.alive=false;
    assert(!IsFaerlinaWorshipperTargetExcluded(&ai,&worshipper));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / 'guard.cpp'
            binary = pathlib.Path(tmp) / 'guard'
            src.write_text(fixture)
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', str(src), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
