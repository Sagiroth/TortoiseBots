"""Compile the production reach clocks and timeout gate with unit/map doubles.

The blacklist body is replaced with a return marker; the fixture also checks
that both blacklist writes and DropAttackTarget remain inside that same gate.
This does not simulate navmesh movement or the engine's next target selection.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


def body(text, start):
    start = text.index('{', start)
    end, depth = start + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


class GroupInstanceReachTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_timeout_gate(self):
        root = Path(os.environ.get('REACH_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
        source = (root/'ai/playerbot/strategy/actions/ReachTargetActions.h').read_text()
        start = source.index('if (!isFriend && !bot->IsRooted()')
        code = body(source, start)
        gate = code.index('if (', code.index('bool const stoodStill'))
        effects = body(code, gate)
        self.assertIn('"unreachable targets"', effects)
        self.assertIn('"unreachable entries"', effects)
        self.assertIn('DropAttackTarget();', effects)
        code = code.replace(effects, '{ return true; }', 1)
        code = code[:-1] + '\nreturn false;\n}'
        cpp = r'''
#include <cassert>
#include <cstdint>
#include <iostream>
using uint32=uint32_t;
struct Map { int kind=0; bool IsDungeon(){return kind==1 || kind==2;} };
struct Unit {
 bool grouped=false, rooted=false, held=false, combat=false, melee=false, runner=false, unreachable=false;
 Map* map=nullptr; Unit* victim=nullptr;
 void* GetGroup(){return grouped?this:nullptr;} Map* GetMap(){return map;}
 float GetPositionX(){return 0;} float GetPositionY(){return 0;}
 int getObjectGuid(){return 42;} Unit* GetVictim(){return victim;}
 bool HasUnitState(int){return runner;} bool IsInCombat(){return combat;}
 bool CanReachWithMeleeAutoAttack(Unit*){return melee;}
 Unit* GetCharmerOrOwnerPlayerOrPlayerItself(){return this;}
 bool IsCreature(){return true;} bool IsInSameRaidWith(Unit*){return false;}
};
using Player=Unit;
Unit* GetMaster(){return nullptr;}
const int UNIT_STAT_FLEEING=1, UNIT_STAT_CONFUSED=2;
struct WorldTimer { static uint32 getMSTime(){return 20000;} };
bool TargetCannotBeReached(Unit* t){return t->unreachable;}
bool IsWithinReach(Unit*){return false;} // local 465/477 reach check: the 40-yd target is outside the 20-yd range
bool timedOut(Unit* bot, Unit* target, uint32 noLosSinceMs, uint32 stoodSinceMs, bool inLos=true) {
 int noLosTarget=42; float noLosStartX=0,noLosStartY=0,noLosBestDist=40;
 float distanceToTarget=40,range=20;
 const uint32 kNoHeadwayMs=15000,kNoMoveMs=10000;
 BODY
}
int main(){
 Unit bot,target;Map map;bot.map=&map;
 for(int kind=0;kind<=2;++kind) for(bool grouped:{false,true}){
   map.kind=kind;bot.grouped=grouped;
   bool exempt=grouped && kind!=0;
   // No headway, with and without LOS; isolated stand-still timer.
   assert(timedOut(&bot,&target,1000,19000)==!exempt);
   assert(timedOut(&bot,&target,1000,19000,false)==!exempt);
   assert(timedOut(&bot,&target,19000,1000)==!exempt);
   assert(!timedOut(&bot,&target,19000,19000));
 }
 bot.map=nullptr;bot.grouped=true;assert(timedOut(&bot,&target,1000,1000));
 bot.map=&map;map.kind=0;
 target.runner=true;assert(!timedOut(&bot,&target,1000,1000));target.runner=false;
 target.victim=&bot;assert(!timedOut(&bot,&target,1000,1000));
 std::cout<<"grouped dungeon/raid, outdoor, solo, absent map and timer controls PASS\n";
}
'''.replace('BODY', code)
        with tempfile.TemporaryDirectory() as temp:
            src, exe = Path(temp)/'test.cpp', Path(temp)/'test'
            src.write_text(cpp)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
