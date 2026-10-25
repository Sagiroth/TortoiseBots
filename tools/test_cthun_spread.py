#!/usr/bin/env python3
"""C'Thun entry regression and standalone production geometry fixture.

Use --root to check the frozen pre-patch tree. Its registry has no spread
strategy or entry trigger; that is the fail-before regression, independent
of the new policy's geometry tests. No server or synthetic raid is launched.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class CthunSpreadTests(unittest.TestCase):
    def test_entry_action_is_reachable(self):
        registry = (ROOT/'ai/playerbot/strategy/StrategyContext.h').read_text()
        # Real old failure: the opt-in command cannot instantiate such a
        # strategy, so even 40 clumped players get no encounter spread action.
        self.assertTrue('creators["cthun spread"]' in registry, 'batch 11h has no CThun entry/spread strategy: the clumped raid gets no encounter movement')
        source = (ROOT/'ai/playerbot/strategy/generic/CthunSpreadStrategy.cpp').read_text()
        for state in ('Combat', 'NonCombat', 'Reaction'):
            self.assertIn('Init'+state+'Triggers', source)
            self.assertIn('Init'+state+'Multipliers', source)
        self.assertIn('new NextAction("cthun spread move"', source)
        actions = (ROOT/'ai/playerbot/strategy/actions/ActionContext.h').read_text()
        triggers = (ROOT/'ai/playerbot/strategy/triggers/TriggerContext.h').read_text()
        self.assertIn('creators["cthun spread move"]', actions)
        self.assertIn('creators["cthun spread needed"]', triggers)

    @unittest.skipUnless(shutil.which('g++'), 'g++ required for action gate fixture')
    def test_failed_route_does_not_interrupt_heals(self):
        source_file=ROOT/'ai/playerbot/strategy/actions/CthunSpreadActions.cpp'
        if not source_file.exists():
            self.skipTest('pre-patch: entry regression covers absent action')
        source=source_file.read_text()
        start=source.index('bool CthunSpreadAction::isUseful()')
        brace=source.index('{',start)
        depth=1
        end=brace+1
        while depth:
            depth += (source[end]=='{')-(source[end]=='}')
            end+=1
        actual=source[start:end]
        fixture=r'''#include <cstdint>
#include <cassert>
using uint32=uint32_t;
static uint32 now=5000;
static bool enabled=true;
struct WorldTimer { static uint32 getMSTime() {return now;} };
bool CthunSpreadEnabled(int*) {return enabled;}
struct CthunSpreadAction {
    int* ai=nullptr;
    uint32 lastMove=0;
    uint32 lastCheck=0;
    bool preparedUseful=false;
    bool validRoute=false;
    int preparations=0;
    bool Prepare() {++preparations;return validRoute;}
    bool isUseful();
};
''' + actual + r'''
int main() {
 CthunSpreadAction action;
 assert(!action.isUseful()); // no valid route => no reaction/heal cancellation
 assert(action.preparations==1);
 action.validRoute=true;now+=501;
 assert(action.isUseful());
 assert(action.isUseful()); // trigger then engine in the same tick
 assert(action.preparations==2);
 enabled=false;
 assert(!action.isUseful());
 assert(action.preparations==2);
 enabled=true;action.lastMove=now;
 assert(!action.isUseful());
 assert(action.preparations==2);
 now+=1001;assert(action.isUseful());
}
'''
        with tempfile.TemporaryDirectory(prefix='cthun-gate-',dir=os.environ.get('TMPDIR')) as tmp:
            cpp=Path(tmp)/'gate.cpp';binary=Path(tmp)/'gate'
            cpp.write_text(fixture)
            subprocess.run(['g++','-std=c++17',str(cpp),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    @unittest.skipUnless(shutil.which('g++'), 'g++ required for entry fixture')
    def test_core_entry_boundary_and_instance_identity(self):
        source_file=ROOT/'ai/playerbot/strategy/actions/CthunSpreadActions.cpp'
        if not source_file.exists():
            self.skipTest('pre-patch: entry regression covers absent action')
        source=source_file.read_text()
        def extract(signature):
            start=source.index(signature);brace=source.index('{',start)
            depth=1;end=brace+1
            while depth:
                depth+=(source[end]=='{')-(source[end]=='}');end+=1
            return source[start:end]
        fixture=r'''#include "playerbot/CthunSpreadPolicy.h"
#include <cassert>
using uint32=uint32_t;
using namespace ai;
struct Player;
struct Unit {
 bool alive=true,combat=false;unsigned map=531;
 float x=kCthunCenter.x,y=kCthunCenter.y,z=100;
 bool IsAlive(){return alive;} bool IsInCombat(){return combat;}
 bool IsInMap(Player*){return map==531;}
 unsigned GetMapId(){return map;}
 float GetPositionX(){return x;} float GetPositionY(){return y;} float GetPositionZ(){return z;}
};
using Creature=Unit;
struct InstanceData {virtual ~InstanceData()=default;};
struct ScriptedInstance:InstanceData {
 Creature* eye=nullptr;Creature* body=nullptr;
 Creature* GetSingleCreatureFromStorage(uint32 entry,bool){return entry==15589?eye:entry==15727?body:nullptr;}
};
struct Map {InstanceData* data=nullptr;InstanceData* GetInstanceData(){return data;}};
struct Player:Unit {
 Map mapObject;bool los=true;
 Map* GetMap(){return &mapObject;} void* GetGroup(){return this;}
 bool IsWithinLOSInMap(Unit*){return los;}
};
struct PlayerbotAI {Player bot;Player* GetBot(){return &bot;}};
bool Enabled(PlayerbotAI*){return true;}
bool Surface(Player* p){return p->alive && CthunSurface(p->map,p->x,p->y,p->z);}
namespace ai {bool CthunSpreadEnabled(PlayerbotAI*);}
''' + extract('Unit* Boss(PlayerbotAI* ai)') + extract('bool ai::CthunSpreadEnabled(PlayerbotAI* ai)') + r'''
int main(){
 PlayerbotAI ai;ScriptedInstance instance;Creature eye,body;
 instance.eye=&eye;instance.body=&body;ai.bot.mapObject.data=&instance;
 ai.bot.x=kCthunCenter.x-94; // ordinary possible-target cache is empty here
 assert(CthunSpreadEnabled(&ai));
 ai.bot.los=false;assert(!CthunSpreadEnabled(&ai)); // waiting behind door must not pull
 body.combat=true;eye.alive=false;assert(CthunSpreadEnabled(&ai)); // body phase/raid engaged
 ai.bot.x=kCthunCenter.x-96;assert(!CthunSpreadEnabled(&ai));
 ai.bot.x=kCthunCenter.x;ai.bot.z=-96;assert(!CthunSpreadEnabled(&ai));
 ai.bot.z=100;body.alive=false;assert(!CthunSpreadEnabled(&ai));
 body.alive=true;ai.bot.map=249;assert(!CthunSpreadEnabled(&ai));
}
'''
        with tempfile.TemporaryDirectory(prefix='cthun-entry-',dir=os.environ.get('TMPDIR')) as tmp:
            cpp=Path(tmp)/'entry.cpp';binary=Path(tmp)/'entry'
            cpp.write_text(fixture)
            subprocess.run(['g++','-std=c++17','-I'+str(ROOT/'ai'),str(cpp),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    @unittest.skipUnless(shutil.which('g++'), 'g++ required for constructor fixture')
    def test_trigger_constructor(self):
        header=ROOT/'ai/playerbot/strategy/generic/CthunSpreadStrategy.h'
        if not header.exists():
            self.skipTest('pre-patch: no encounter trigger')
        def constructor(text, signature):
            start=text.index(signature);brace=text.index('{',start)
            depth=1;end=brace+1
            while depth:
                depth+=(text[end]=='{')-(text[end]=='}');end+=1
            return text[start:end]
        base=(ROOT/'ai/playerbot/strategy/Trigger.h').read_text()
        fixture=r'''#include <ctime>
#include <cstdlib>
#include <string>
#include <cassert>
struct PlayerbotAI {};
struct AiNamedObject {AiNamedObject(PlayerbotAI*,std::string){}};
struct Trigger:AiNamedObject {
 bool triggered=false;int checkInterval=0;time_t lastCheckTime=0;
''' + constructor(base, 'Trigger(PlayerbotAI* ai,') + r'''
};
struct CthunSpreadTrigger:Trigger {
''' + constructor(header.read_text(),'explicit CthunSpreadTrigger(') + r'''
};
int main(){PlayerbotAI ai;CthunSpreadTrigger trigger(&ai);assert(trigger.checkInterval==1);}
'''
        with tempfile.TemporaryDirectory(prefix='cthun-trigger-',dir=os.environ.get('TMPDIR')) as tmp:
            cpp=Path(tmp)/'trigger.cpp';binary=Path(tmp)/'trigger'
            cpp.write_text(fixture)
            subprocess.run(['g++','-std=c++17',str(cpp),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    @unittest.skipUnless(shutil.which('g++'), 'g++ required for geometry fixture')
    def test_production_geometry(self):
        policy = ROOT/'ai/playerbot/CthunSpreadPolicy.h'
        if not policy.exists():
            self.skipTest('pre-patch: no encounter planner; entry regression above must fail')
        with tempfile.TemporaryDirectory(prefix='cthun-', dir=os.environ.get('TMPDIR')) as tmp:
            binary = Path(tmp)/'cthun-test'
            subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-pedantic',
                str(ROOT/'tools/test_cthun_spread.cpp'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--root', type=Path)
    args, rest=parser.parse_known_args()
    if args.root: ROOT=args.root.resolve()
    unittest.main(argv=[__file__]+rest)
