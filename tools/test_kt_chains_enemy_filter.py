"""Exercise the production enemy-player filter for Turtle's Chains aura."""
import pathlib
import os
import shutil
import subprocess
import tempfile
import unittest


def function(src, sig):
    start = src.index(sig)
    opening = src.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (src[end] == '{') - (src[end] == '}')
        end += 1
    return src[start:end]


class ChainsEnemyFilterTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_grouped_chains_target(self):
        root = pathlib.Path(os.environ.get('TBOTS_TEST_SOURCE', pathlib.Path(__file__).resolve().parents[1]))
        source = (root / 'ai/playerbot/strategy/values/EnemyPlayerValue.cpp').read_text()
        filter_code = function(source, 'bool EnemyPlayersValue::IsValid(')
        filter_code += function(source, 'void EnemyPlayersValue::ApplyFilter(')
        fixture = r'''
#include <cassert>
#include <set>
#include <list>
#include <map>
using ObjectGuid = unsigned;using uint32=unsigned;
struct WorldTimer{static unsigned getMSTime(){return 1;}};
#include <string>
#include <iostream>
constexpr int SPELL_AURA_MOD_CHARM=6, SPELL_AURA_MOD_POSSESS=2;
struct Unit {
    virtual ~Unit()=default;
    Unit* GetVictim(){return nullptr;}
    bool friendly=false, chains=false;
    unsigned map=533;
    ObjectGuid guid=1;
    ObjectGuid getObjectGuid() { return guid; }
    std::set<int> types;
    bool HasAuraType(int t) { return types.count(t); }
    bool HasAura(unsigned id) { return id==28410 && chains; }
    unsigned GetMapId() { return map; }
};
struct Player: Unit {
    bool IsWithinLOSInMap(Unit*,bool){return true;}
    int group=1;
    bool combat=true;
    std::string GetName() { return "fixture"; }
    bool IsInCombat() { return combat; }
    std::set<Unit*> GetAttackers() { return {}; }
};
bool IsInGroup_Helper(Player* a,Player* b) { return a->group && a->group==b->group; }
struct Context{std::map<ObjectGuid,uint32> entries;template<class T>Context* GetValue(std::string){return this;}auto& Get(){return entries;}};
struct PlayerbotAI {Context context;Context* GetAiObjectContext(){return &context;}
    bool human=true, avoided=false;
    std::map<ObjectGuid,Unit*> units;
    Unit* GetUnit(ObjectGuid id) { return units[id]; }
    bool HasRealPlayerMaster() { return human; }
    bool ShouldAvoidPlayerKiller(std::string) { return avoided; }
};
struct PlayerbotAIStorage {
    PlayerbotAI ai;
    static PlayerbotAIStorage& Instance() { static PlayerbotAIStorage s; return s; }
    PlayerbotAI* GetAI(Player*) { return &ai; }
};
struct { bool IsFriendlyTo(Unit* target,Player*) { return target->friendly; } } sServerFacade;
struct EnemyPlayersValue {
    PlayerbotAI* ai; Player* bot;
    static bool IsValid(Unit*,Player*);
    bool CanEngageFromHere(Unit*){return true;}
    void ApplyFilter(std::list<ObjectGuid>&, bool, bool=false);
};
'''
        fixture += filter_code + r'''
int main() {
    Player bot, ally;
    ally.chains=true;ally.types={177};
    assert(!EnemyPlayersValue::IsValid(&ally,&bot));
    Player enemy;enemy.group=2;enemy.guid=2;
    PlayerbotAI ai;ai.units={{1,&ally},{2,&enemy}};
    EnemyPlayersValue value{&ai,&bot};
    std::list<ObjectGuid> candidates{1,2};value.ApplyFilter(candidates,false);
    assert(candidates==std::list<ObjectGuid>{2});
    candidates={1,2};value.ApplyFilter(candidates,true);
    assert(candidates==std::list<ObjectGuid>{2});
    ally.group=2;assert(EnemyPlayersValue::IsValid(&ally,&bot));ally.group=1;
    ally.map=1;assert(EnemyPlayersValue::IsValid(&ally,&bot));ally.map=533;
    ally.chains=false;assert(EnemyPlayersValue::IsValid(&ally,&bot));
    ally.types={6};assert(!EnemyPlayersValue::IsValid(&ally,&bot));
    ally.types={2};assert(!EnemyPlayersValue::IsValid(&ally,&bot));
    ally.types.clear();ally.friendly=true;assert(!EnemyPlayersValue::IsValid(&ally,&bot));
    Unit creature;assert(!EnemyPlayersValue::IsValid(&creature,&bot));
    assert(!EnemyPlayersValue::IsValid(nullptr,&bot));
    std::cout<<"PASS: Chains grouped ally excluded; unrelated player/aura/map and existing charm checks preserved\n";
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp=pathlib.Path(tmp)/'fixture.cpp';exe=pathlib.Path(tmp)/'fixture'
            cpp.write_text(fixture)
            subprocess.run(['g++','-std=c++17',str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)


if __name__ == '__main__':
    unittest.main()
