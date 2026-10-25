"""Exercise the production no-destination branch and basic flee selection gate."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


def block(text, marker):
    start = text.index(marker)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


class FailedFleeRetryTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_failed_search_yields_to_casting(self):
        own = Path(__file__).resolve().parents[1]
        root = Path(os.environ.get('TBOTS_FLEE_SOURCE', own))
        source = (root/'ai/playerbot/strategy/actions/MovementActions.cpp').read_text()
        search = block(source, 'if (!manager.CalculateDestination(&rx, &ry, &rz, &fleeMovement.fleeFailures, nowMs))')
        useful = (block(source, 'bool FleeAction::isUseful()') if 'bool FleeAction::isUseful()' in source else
                  'bool FleeAction::isUseful(){return MovementAction::isUseful();}')
        code = r'''
#include <cassert>
#include <cstdint>
#include <string>
#include "playerbot/FleeRetryPolicy.h"
using uint32=uint32_t;
using namespace ai;
struct LastMovement {FleeRetryState fleeRetry;int fleeFailures=0;};
struct WorldTimer {static uint32 now;static uint32 getMSTime(){return now;}};
uint32 WorldTimer::now=0;
LastMovement movement;
#define AI_VALUE(T,N) movement
struct MovementAction {bool baseUseful=true;bool isUseful(){return baseUseful;}};
struct FleeAction:MovementAction {bool isUseful();};
struct AI {int reports=0;void TellError(void*,std::string text){assert(text=="Nowhere to flee");++reports;}} fake;
void* GetMaster(){return nullptr;}
struct Manager {bool safe=false;int searches=0;bool CalculateDestination(float*,float*,float*,int*,uint32){++searches;return safe;}} manager;
bool search(){
 AI* ai=&fake;LastMovement& fleeMovement=movement;uint32 nowMs=WorldTimer::now;float rx,ry,rz;
'''+search+'\nreturn true;\n}\n'+useful+r'''
int main(){
 FleeAction action;
 assert(action.isUseful());assert(!search());assert(fake.reports==1);
 // A failed route must not remain the highest useful movement action.
 unsigned casts=0;
 for(uint32 now=100;now<3000;now+=100){
   WorldTimer::now=now;
   assert(!action.isUseful());
   if(!action.isUseful())++casts;
 }
 assert(casts==29 && manager.searches==1);
 // The normal rotation can finish a cast before another attempt.
 for(uint32 now:{3000u,6000u,9000u}){
   WorldTimer::now=now;assert(action.isUseful());assert(!search());
 }
 assert(fake.reports==1);
 WorldTimer::now=12000;assert(action.isUseful());assert(!search());assert(fake.reports==2);
 // Recovery re-enables fleeing; successes do not start another delay.
 WorldTimer::now=15000;manager.safe=true;assert(action.isUseful());assert(search());assert(action.isUseful());
 action.baseUseful=false;assert(!action.isUseful());action.baseUseful=true;
 // Independent hazard escape remains eligible throughout this basic-flee pause.
 MovementAction hazard;movement.fleeRetry.Failed(15000);assert(hazard.isUseful());assert(!action.isUseful());
 // Per-bot state, copy/reset, and uint32 clock wrap (including a failure at 0).
 LastMovement fresh;assert(fresh.fleeRetry.CanRetry(15000));
 LastMovement copied=movement;assert(!copied.fleeRetry.CanRetry(15001));
 movement=LastMovement();WorldTimer::now=0;manager.safe=false;assert(!search());assert(!action.isUseful());
 movement=LastMovement();WorldTimer::now=0xffffff00u;assert(!search());
 WorldTimer::now=0x100;assert(!action.isUseful());
 WorldTimer::now=uint32(0xffffff00u+3000u);assert(action.isUseful());
 assert(!movement.fleeRetry.ShouldReport(uint32(0xffffff00u+9999u)));
 assert(movement.fleeRetry.ShouldReport(uint32(0xffffff00u+10000u)));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp, exe = Path(tmp)/'retry.cpp', Path(tmp)/'retry'
            cpp.write_text(code)
            subprocess.run(['g++', '-std=c++17', '-I'+str(own/'ai'), str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_production_failure_sites_and_state(self):
        root = Path(os.environ.get('TBOTS_FLEE_SOURCE', Path(__file__).resolve().parents[1]))
        source = (root/'ai/playerbot/strategy/actions/MovementActions.cpp').read_text()
        flee = block(source, 'bool MovementAction::Flee(Unit *target)')
        self.assertIn('if (!fleeMovement.fleeRetry.CanRetry(nowMs))', flee)
        self.assertIn('fleeMovement.fleeRetry.Failed(nowMs)', block(flee, 'if (!manager.isUseful())'))
        self.assertIn('if (!moved) fleeMovement.fleeRetry.Failed(nowMs)', flee)
        self.assertIn('if (!succeeded) fleeMovement.fleeRetry.Failed(nowMs)', flee)
        self.assertIn('fleeRetry = other.fleeRetry', (root/'ai/playerbot/strategy/values/LastMovementValue.h').read_text())
        self.assertIn('fleeRetry = FleeRetryState()', (root/'ai/playerbot/strategy/values/LastMovementValue.h').read_text())


if __name__ == '__main__':
    unittest.main()
