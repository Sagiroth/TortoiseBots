"""Compile the actual healer reach dispatch; core chase/pathfinding are boundaries."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


def body(source, marker):
    start = source.index('{', source.index(marker))
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class HealerReachTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_production_healer_movement(self):
        root = Path(__file__).resolve().parents[1]
        source = Path(os.environ.get('HEAL_REACH_SOURCE', root/'ai/playerbot/strategy/actions/ReachTargetActions.h')).read_text()
        healer = source.split('class ReachPartyMemberToHealAction', 1)[1].split('class ReachPartyMemberToResurrectAction', 1)[0]
        base = body(source, 'virtual bool MoveIntoRange(Unit* target, float distance)')
        method = body(healer, 'bool MoveIntoRange(') if 'bool MoveIntoRange(' in healer else base
        cpp = r'''
#include "ai/playerbot/policy/pull/PullFiringPolicy.h"
#include <cassert>
#include <cmath>
using namespace ai;
struct Unit {float x=0,y=0,z=0;bool friendly=true;
 unsigned GetMapId(){return 533;} float getPositionX(){return x;}
 float getPositionY(){return y;} float getPositionZ(){return z;}};
struct Bot:Unit {bool los=true;bool IsWithinLOSInMap(Unit*,bool){return los;} float GetAngle(Unit*){return 0;}};
struct Facade {bool IsFriendlyTo(Bot*,Unit* u){return u->friendly;}} sServerFacade;
float floorHeight=0;
struct WorldPosition {unsigned map;float x,y,z;
 WorldPosition(Unit* u):map(u->GetMapId()),x(u->x),y(u->y),z(u->z){}
 WorldPosition(unsigned m,float a,float b,float c):map(m),x(a),y(b),z(c){}
 unsigned GetMapId(){return map;} float getX()const{return x;} float getY()const{return y;}
 float getZ()const{return z;} float GetHeight(){return floorHeight;} void setZ(float a){z=a;}};
struct Reach {
 Bot* bot; bool within=false,valid=true,moveAllowed=true,moved=false;
 unsigned chases=0,points=0;float px=0,py=0;
 bool IsWithinReach(Unit*){return within;}
 bool IsValidPosition(WorldPosition const&,WorldPosition const&){return valid;}
 // Core TargetedMovementGenerator.cpp:130-135,153-163 rejects a
 // non-melee destination inside a dungeon. Model that boundary only.
 bool ChaseTo(Unit*,float distance,float){++chases;moved=distance<=5;return true;}
 bool MoveTo(unsigned,float x,float y,float){++points;px=x;py=y;moved=moveAllowed;return moveAllowed;}
 bool Run(Unit* target,float distance)
''' + method + r'''
};
int main(){
 Bot bot; Unit patient;patient.x=40;
 Reach r{&bot};
 assert(r.Run(&patient,22));assert(r.moved);assert(r.chases==0);
 assert(r.points==1 && std::fabs(r.px-18)<0.001f && r.py==0);
 r=Reach{&bot};r.within=true;assert(r.Run(&patient,22));assert(!r.points && !r.chases);
 r=Reach{&bot};r.valid=false;assert(!r.Run(&patient,22));assert(!r.points && !r.chases);
 r=Reach{&bot};floorHeight=-5;assert(!r.Run(&patient,22));assert(!r.points);
 floorHeight=0;r=Reach{&bot};r.moveAllowed=false;assert(!r.Run(&patient,22));
 r=Reach{&bot};bot.los=false;assert(r.Run(&patient,22));assert(r.px==40 && !r.chases);
 bot.los=true;r=Reach{&bot};assert(!r.Run(nullptr,22));
 patient.friendly=false;assert(!r.Run(&patient,22));assert(!r.points);
 patient.friendly=true;patient.x=0;r=Reach{&bot};assert(!r.Run(&patient,22));assert(!r.points);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            src=Path(folder)/'test.cpp';exe=Path(folder)/'test'
            src.write_text(cpp)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-I',str(root),str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

if __name__=='__main__': unittest.main()
