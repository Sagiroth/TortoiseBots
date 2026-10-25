"""Compile active shaman trigger builders; the legacy strategy is not registered."""
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest
from tools.test_nefarian_bone_target import function

class RaidTremorWiringTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_active_specializations_schedule_tremor(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        folder = root / 'ai/playerbot/strategy/shaman'
        names = ['ShamanStrategy', 'RestorationShamanStrategy', 'ElementalShamanStrategy', 'EnhancementShamanStrategy']
        production = '\n'.join(function((folder / (name + '.cpp')).read_text(),
            'void ' + name + '::InitCombatTriggers(') for name in names)
        constants = sorted(set(re.findall(r'\bACTION_[A-Z_]+\b', production)))
        fixture = r'''
#include <cassert>
#include <cstdarg>
#include <list>
#include <string>
#include <vector>
struct NextAction {
 std::string name; float priority;
 NextAction(const char* n, float p):name(n),priority(p){}
 static NextAction** array(int sentinel,...) {
  va_list args;va_start(args,sentinel);std::vector<NextAction*> values;
  while(auto p=va_arg(args,NextAction*))values.push_back(p);
  va_end(args);auto result=new NextAction*[values.size()+1];
  for(unsigned i=0;i<values.size();++i)result[i]=values[i];
  result[values.size()]=nullptr;return result;
 }
};
struct TriggerNode {
 std::string name;NextAction** actions;
 TriggerNode(const char* n,NextAction** a):name(n),actions(a){}
 ~TriggerNode(){for(unsigned i=0;actions[i];++i)delete actions[i];delete[] actions;}
};
struct ClassStrategy {void InitCombatTriggers(std::list<TriggerNode*>&){}};
'''
        fixture += 'enum {' + ','.join(constants) + '};\n'
        for name in names:
            base = 'ClassStrategy' if name == 'ShamanStrategy' else 'ShamanStrategy'
            fixture += 'struct ' + name + ':' + base + '{void InitCombatTriggers(std::list<TriggerNode*>&);};\n'
        checks = r'''
template<class Strategy> void check(){
 Strategy strategy;std::list<TriggerNode*> nodes;strategy.InitCombatTriggers(nodes);
 unsigned matches=0;
 for(auto n:nodes){if(n->name=="raid fear tremor totem"){
  ++matches;assert(n->actions[0]->name=="tremor totem");
  assert(n->actions[0]->priority==ACTION_HIGH+1);assert(!n->actions[1]);
 }delete n;}
 assert(matches==1);
}
int main(){check<RestorationShamanStrategy>();check<ElementalShamanStrategy>();check<EnhancementShamanStrategy>();}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / 'fixture.cpp'
            binary = pathlib.Path(directory) / 'fixture'
            source.write_text(fixture + production + checks)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra',str(source),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_trigger_respects_spell_state_and_manual_totems(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        text = (root/'ai/playerbot/strategy/shaman/ShamanTriggers.h').read_text()
        production = function(text, 'class RaidFearTremorTotemTrigger') + ';\n'
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <set>
#include <string>
#include "ai/playerbot/strategy/RaidFearPolicy.h"
using namespace ai;
using uint32=std::uint32_t;
struct Unit {unsigned entry=11982;unsigned GetEntry(){return entry;}};
struct Player {unsigned map=409;unsigned GetMapId(){return map;}};
enum class BotState {BOT_STATE_COMBAT};
struct PlayerbotAI {Player player;std::set<std::string> strategies;bool HasStrategy(std::string s,BotState){return strategies.count(s);}};
struct Trigger {PlayerbotAI* ai;Player* bot;Trigger(PlayerbotAI* a,const char*,int):ai(a),bot(&a->player){}virtual bool IsActive()=0;};
Unit unit;Unit* target=&unit;unsigned spellId=1;bool hasTotem=false,ready=true;
struct {bool IsSpellReady(Player*,unsigned){return ready;}} sServerFacade;
template<class T>T value(std::string){if constexpr(std::is_same_v<T,bool>)return hasTotem;else return spellId;}
#define AI_VALUE(T,key) (::target)
#define AI_VALUE2(T,key,q) value<T>(key)
'''
        checks = r'''
int main(){PlayerbotAI botAI;RaidFearTremorTotemTrigger trigger(&botAI);
 assert(trigger.IsActive());
 for(auto s:{"totem earth stoneclaw","totem earth stoneskin","totem earth earthbind","totem earth strength"}){
  botAI.strategies={s};assert(!trigger.IsActive());
 }
 botAI.strategies={"totem earth tremor"};assert(trigger.IsActive());botAI.strategies.clear();
 hasTotem=true;assert(!trigger.IsActive());hasTotem=false;
 ready=false;assert(!trigger.IsActive());ready=true;
 spellId=0;assert(!trigger.IsActive());spellId=1;
 target=nullptr;assert(!trigger.IsActive());target=&unit;
 botAI.player.map=531;assert(!trigger.IsActive());
 botAI.player.map=409;unit.entry=15543;assert(!trigger.IsActive());
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source=pathlib.Path(directory)/'fixture.cpp';binary=pathlib.Path(directory)/'fixture'
            source.write_text(fixture+production+checks)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-I',str(root),str(source),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
