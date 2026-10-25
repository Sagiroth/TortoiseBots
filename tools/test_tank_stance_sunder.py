"""Production warrior method slices; core aura/cast services are explicit doubles."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


def body(text, marker):
    start = text.index('{', text.index(marker))
    end, depth = start + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


class TankStanceSunderTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_production_methods(self):
        root = Path(os.environ.get('TANK_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
        warrior = root / 'ai/playerbot/strategy/warrior'
        triggers = (warrior/'WarriorTriggers.cpp').read_text()
        actions = (warrior/'WarriorActions.h').read_text()
        cpp = r'''
#include <cassert>
#include <string>
#include <iostream>
using uint32 = unsigned;
constexpr int CLASS_WARRIOR=1;
struct Group;
struct Unit { bool player=false; bool IsPlayer(){return player;} Group* GetGroup(){return nullptr;}
 bool IsAlive(){return true;} bool IsInWorld(){return true;} int GetMapId(){return 1;} int GetClass(){return CLASS_WARRIOR;}};
using Player=Unit;
struct GroupReference {Player* GetSource(){return nullptr;} GroupReference* next(){return nullptr;}};
struct Group {GroupReference* GetFirstMember(){return nullptr;}};
struct Aura {
 int stacks=1, duration=30000;
 int GetStackAmount(){return stacks;} int GetAuraDuration(){return duration;}
};
struct AI {
 bool tank=false, learned=true, blocked=false, buffUseful=true;
 int rage=15; Aura* aura=nullptr;
 bool ready[4]={false,false,false,false}, usable[4]={true,true,true,true};
 bool IsTank(Unit*, bool=false){return tank;}
 bool HasSpell(const char*){return learned;}
 Aura* GetAura(const char*,Unit*,bool=false){return aura;}
 bool HasAura(const char*,Unit*,bool){return aura && aura->stacks>=5;}
 bool CanCastSpell(const char*,Unit*){return false;}
 bool CanCastSpell(uint32 id,Unit*){return usable[id] && rage >= (id==3 ? 20 : 30);}
 uint32 id(std::string name){if(name=="medium rage available")return rage>=40;if(name=="light rage available")return rage>=20;return name=="bloodthirst"?1:name=="mortal strike"?2:3;}
};
bool PatchwerkThreatTank(AI*, Unit*){return false;} // separately exercised by Patchwerk opening fixture
struct Facade {
 AI* ai;
 bool IsSpellReady(Unit*,uint32 id){return ai->ready[id];}
} sServerFacade;
#define AI_VALUE2(type,key,name) ai->id(name)
struct BuffTrigger {
 AI* ai; Unit* bot; Unit* target;
 BuffTrigger(AI* a,Unit* b,Unit* t):ai(a),bot(b),target(t){}
 bool IsActive(){return ai->buffUseful;}
 Unit* GetTarget(){return target;}
 bool HasMaxDebuffs(){return ai->blocked;}
};
struct CastBuffSpellAction : BuffTrigger {
 using BuffTrigger::BuffTrigger;
 bool isUseful(){return ai->buffUseful;}
};
struct Battle : BuffTrigger { using BuffTrigger::BuffTrigger; bool IsActive() BATTLE };
struct Defensive : BuffTrigger { using BuffTrigger::BuffTrigger; bool IsActive() DEFENSIVE };
struct Berserker : BuffTrigger { using BuffTrigger::BuffTrigger; bool IsActive() BERSERKER };
struct BattleAction : CastBuffSpellAction {using CastBuffSpellAction::CastBuffSpellAction;bool isUseful(){return BATTLE_ACTION;} };
struct BerserkerAction : CastBuffSpellAction {using CastBuffSpellAction::CastBuffSpellAction;bool isUseful(){return BERSERKER_ACTION;} };
struct SunderTrigger : BuffTrigger {using BuffTrigger::BuffTrigger;bool IsActive() SUNDER_TRIGGER };
struct SunderAction : BuffTrigger {using BuffTrigger::BuffTrigger;bool isUseful() SUNDER_ACTION };
int main(int argc,char** argv){
 assert(argc==2);std::string test=argv[1]; AI ai; Unit bot,target; Aura aura; sServerFacade.ai=&ai;
 if(test=="stance") {
   ai.tank=true;
   assert(!Berserker(&ai,&bot,&target).IsActive());
   assert(!Battle(&ai,&bot,&target).IsActive());
   assert(Defensive(&ai,&bot,&target).IsActive());
   ai.buffUseful=false;assert(!Defensive(&ai,&bot,&target).IsActive());
   ai.buffUseful=true;ai.tank=false;
   assert(Berserker(&ai,&bot,&target).IsActive());assert(Battle(&ai,&bot,&target).IsActive());
   assert(!Defensive(&ai,&bot,&target).IsActive());
 } else if(test=="prerequisites") {
   ai.tank=true;assert(!BerserkerAction(&ai,&bot,&target).isUseful());assert(!BattleAction(&ai,&bot,&target).isUseful());
   ai.tank=false;assert(BerserkerAction(&ai,&bot,&target).isUseful());assert(BattleAction(&ai,&bot,&target).isUseful());
 } else if(test=="refresh") {
   ai.tank=true;ai.aura=&aura;SunderTrigger t(&ai,&bot,&target);
   for(int stacks=1;stacks<5;++stacks){aura.stacks=stacks;assert(t.IsActive());}
   aura.stacks=5;aura.duration=6001;assert(!t.IsActive());
   aura.duration=6000;assert(t.IsActive());aura.duration=1;assert(t.IsActive());
   aura.duration=-1;assert(!t.IsActive());ai.aura=nullptr;assert(t.IsActive());
   ai.learned=false;assert(!t.IsActive());ai.learned=true;t.target=nullptr;assert(!t.IsActive());
 } else if(test=="affordability") {
   ai.tank=true;SunderAction a(&ai,&bot,&target);
   ai.rage=40;ai.ready[3]=true;assert(a.isUseful());ai.ready[3]=false;
   ai.aura=&aura;aura.stacks=5;aura.duration=30000;
   for(unsigned id=1;id<=3;++id){
     ai.ready[id]=true;ai.rage=15;assert(a.isUseful());
     ai.rage=30;ai.usable[id]=false;assert(a.isUseful());
     ai.usable[id]=true;ai.rage=40;assert(!a.isUseful());ai.ready[id]=false;assert(a.isUseful());
   }
   a.target=nullptr;assert(!a.isUseful());a.target=&target;
   ai.tank=false;ai.aura=&aura;aura.stacks=5;assert(!a.isUseful());aura.stacks=1;assert(a.isUseful());
 }
 std::cout<<test<<" PASS\n";
}
'''
        for cls, label in [('BattleStance','BATTLE'),('DefensiveStance','DEFENSIVE'),('BerserkerStance','BERSERKER')]:
            marker = f'bool {cls}Trigger::IsActive()'
            cpp = cpp.replace(label+' }', (body(triggers, marker) if marker in triggers else '{return BuffTrigger::IsActive();}')+' }')
        for cls, label in [('BattleStance','BATTLE_ACTION'),('BerserkerStance','BERSERKER_ACTION')]:
            match = re.search(r'BUFF_ACTION_U\(Cast'+cls+r'Action, "[^"]+", (.*)\);', actions)
            cpp = cpp.replace(label, match.group(1) if match else 'CastBuffSpellAction::isUseful()')
        cpp = cpp.replace('SUNDER_TRIGGER', body(triggers, 'bool SunderArmorDebuffTrigger::IsActive()'))
        cpp = cpp.replace('SUNDER_ACTION', body(actions.split('class CastSunderArmorAction',1)[1], 'virtual bool isUseful()'))
        with tempfile.TemporaryDirectory() as temp:
            source, exe = Path(temp)/'test.cpp', Path(temp)/'test'
            source.write_text(cpp)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],check=True)
            for case in ['stance','prerequisites','refresh','affordability']:
                with self.subTest(case=case):
                    subprocess.run([str(exe),case],check=True)
        # Ensure the role-aware maintenance trigger reaches Fury as well as Prot.
        strategy = (warrior/'WarriorStrategy.cpp').read_text()
        common = body(strategy, 'void WarriorStrategy::InitCombatTriggers')
        self.assertIn('new NextAction("defensive stance", ACTION_MOVE)', common)
        self.assertIn('BuffStrategy::InitCombatTriggers', (warrior/'FuryWarriorStrategy.cpp').read_text())


if __name__ == '__main__':
    unittest.main()
