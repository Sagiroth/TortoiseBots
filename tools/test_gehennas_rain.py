"""Extract the production caster-owned rain collector; engine objects are stubs."""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def fixture(root):
    source = (root / 'ai/playerbot/strategy/actions/MoltenCoreDungeonActions.cpp').read_text()
    code = r'''
#include <cassert>
#include <cmath>
#include <list>
#include <map>
#include <string>
#include <vector>
using ObjectGuid = unsigned;
using SpellEffectIndex = unsigned;
enum class BotState { BOT_STATE_COMBAT };
struct DynamicObject;
struct Unit {
 unsigned entry=12259,map=409,instance=1,guid=1;
 bool alive=true,combat=true,world=true,teleport=false;
 float x=0; std::vector<DynamicObject*> areas;
 unsigned GetEntry(){return entry;}unsigned GetMapId(){return map;}
 unsigned GetObjectGuid(){return guid;}bool IsAlive(){return alive;}
 bool IsInCombat(){return combat;}bool IsInWorld(){return world;}
 bool IsBeingTeleported(){return teleport;}
 bool IsInMap(Unit* u){return map==u->map&&instance==u->instance;}
 void GetDynObjects(unsigned, unsigned, std::vector<DynamicObject*>& out){out=areas;}
};
using Player=Unit;
struct DynamicObject:Unit {
 unsigned caster=1,spell=19717,effect=0;int duration=1000;float radius=10;
 unsigned GetCasterGuid(){return caster;}unsigned GetSpellId(){return spell;}
 unsigned GetEffIndex(){return effect;}int GetDuration(){return duration;}
 float GetRadius(){return radius;}
};
struct WorldPosition {
 float x;WorldPosition(Unit* u):x(u->x){} float distance(const WorldPosition& p){return std::fabs(x-p.x);}
};
using HazardPosition=std::pair<WorldPosition,float>;
struct Position {bool set=false;bool isSet(){return set;}};
using PositionMap=std::map<std::string,Position>;
struct AiObjectContext {PositionMap positions;std::list<ObjectGuid> attackers{1};};
struct PlayerbotAI {
 Player* bot;Unit* boss;bool enabled=true;std::string hold;AiObjectContext context;
 Player* GetBot(){return bot;}AiObjectContext* GetAiObjectContext(){return &context;}
 Unit* GetUnit(ObjectGuid){return boss;}
 bool HasStrategy(std::string s,BotState){return s=="gehennas rain"?enabled:s==hold;}
};
template<class T>T value(AiObjectContext*);
template<>PositionMap& value<PositionMap&>(AiObjectContext* c){return c->positions;}
template<>std::list<ObjectGuid> value<std::list<ObjectGuid>>(AiObjectContext* c){return c->attackers;}
#define AI_VALUE(T,N) value<T>(context)
namespace ai {
 std::list<HazardPosition> GetGehennasRainHazards(PlayerbotAI*);
 bool GehennasRainTooClose(PlayerbotAI*);
}
'''
    code += function(source, 'std::list<HazardPosition> ai::GetGehennasRainHazards(')
    code += '\n' + function(source, 'bool ai::GehennasRainTooClose(')
    code += r'''
int main(){
 Player bot;Unit boss;DynamicObject rain;boss.areas={&rain};PlayerbotAI ai{&bot,&boss};
 assert(ai::GetGehennasRainHazards(&ai).size()==1);
 assert(ai::GehennasRainTooClose(&ai));bot.x=13;assert(!ai::GehennasRainTooClose(&ai));bot.x=0;
 ai.enabled=false;assert(ai::GetGehennasRainHazards(&ai).empty());ai.enabled=true;
 for(auto hold:{"stay","follow","wait for attack","grind"}){
  ai.hold=hold;assert(ai::GetGehennasRainHazards(&ai).empty());
 }ai.hold="";
 ai.context.positions["pull hold"].set=true;assert(ai::GetGehennasRainHazards(&ai).empty());
 ai.context.positions.clear();
 bot.map=1;assert(ai::GetGehennasRainHazards(&ai).empty());bot.map=409;
 boss.instance=2;assert(ai::GetGehennasRainHazards(&ai).empty());boss.instance=1;
 boss.combat=false;assert(ai::GetGehennasRainHazards(&ai).empty());boss.combat=true;
 boss.entry=12098;assert(ai::GetGehennasRainHazards(&ai).empty());boss.entry=12259;
 rain.caster=2;assert(ai::GetGehennasRainHazards(&ai).empty());rain.caster=1;
 rain.spell=19718;assert(ai::GetGehennasRainHazards(&ai).empty());rain.spell=19717;
 rain.effect=1;assert(ai::GetGehennasRainHazards(&ai).empty());rain.effect=0;
 rain.duration=0;assert(ai::GetGehennasRainHazards(&ai).empty());rain.duration=1000;
 rain.radius=NAN;assert(ai::GetGehennasRainHazards(&ai).empty());rain.radius=10;
 boss.areas.clear();assert(ai::GetGehennasRainHazards(&ai).empty());
 assert(ai::GetGehennasRainHazards(nullptr).empty());
}
'''
    return code


class GehennasRainTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_production_collector(self):
        root = pathlib.Path(os.environ.get('TBOTS_TEST_SOURCE', pathlib.Path(__file__).resolve().parents[1]))
        with tempfile.TemporaryDirectory() as tmp:
            cpp, exe = pathlib.Path(tmp)/'rain.cpp', pathlib.Path(tmp)/'rain'
            cpp.write_text(fixture(root))
            subprocess.run(['g++','-std=c++17',str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)


if __name__ == '__main__':
    unittest.main()
