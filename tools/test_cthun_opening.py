#!/usr/bin/env python3
"""Live waiting-area regression; executes extracted production gates on --root."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

def extract(source,signature):
    a=source.index(signature);b=source.index('{',a);depth=1;e=b+1
    while depth:
        depth+=(source[e]=='{')-(source[e]=='}');e+=1
    return source[a:e]

def run_cpp(source):
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
        cpp=Path(tmp)/'fixture.cpp';binary=Path(tmp)/'fixture'
        cpp.write_text(source)
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-I'+str(ROOT/'ai'),str(cpp),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)

class Opening(unittest.TestCase):
    def test_actual_activation_from_live_waiting_area(self):
        source=(ROOT/'ai/playerbot/strategy/actions/CthunSpreadActions.cpp').read_text()
        fixture=r'''
#include "playerbot/CthunSpreadPolicy.h"
#include <cassert>
using namespace ai;
using uint32=uint32_t;
namespace BotState {enum {BOT_STATE_COMBAT,BOT_STATE_NON_COMBAT};}
struct Player;struct PlayerbotAI;
struct Unit {
 bool alive=true,combat=false;unsigned map=531,instance=1,entry=15589;
 float x=kCthunCenter.x,y=kCthunCenter.y,z=100;
 bool IsAlive(){return alive;} bool IsInCombat(){return combat;}
 bool IsInMap(Unit*p){return map==p->map && instance==p->instance;}
 unsigned GetMapId(){return map;} unsigned GetEntry(){return entry;}
 float GetPositionX(){return x;}float GetPositionY(){return y;}float GetPositionZ(){return z;}
};
using Creature=Unit;
struct InstanceData {virtual ~InstanceData()=default;};
struct ScriptedInstance:InstanceData {
 Creature *eye=nullptr,*body=nullptr;
 Creature* GetSingleCreatureFromStorage(uint32 e,bool){return e==15589?eye:body;}
};
struct Map {InstanceData* data=nullptr;InstanceData* GetInstanceData(){return data;}};
struct Group {std::vector<Player*> members;};
struct Player:Unit {
 Map mapObject;Group* group=nullptr;PlayerbotAI* ai=nullptr;
 bool world=true,teleport=false,los=false;
 Map* GetMap(){return &mapObject;}Group* GetGroup(){return group;}
 bool IsInWorld(){return world;}bool IsBeingTeleported(){return teleport;}
 bool IsWithinLOSInMap(Unit*){return los;}
};
struct PullStrategy;
struct PlayerbotAI {
 Player bot;bool enabled=true;PullStrategy* pull=nullptr;
 Player* GetBot(){return &bot;}bool HasStrategy(const char*,int){return enabled;}
};
struct PullStrategy {
 bool active=false,pullback=false;Unit* target=nullptr;
 static PullStrategy* Get(PlayerbotAI*p){return p->pull;}
 bool IsCommandActive(){return active;}bool IsCommandPullback(){return pullback;}
 Unit* GetTarget(){return target;}
};
struct PlayerbotAIStorage {
 static PlayerbotAIStorage& Instance(){static PlayerbotAIStorage s;return s;}
 PlayerbotAI* GetAI(Player*p){return p->ai;}
};
std::vector<Player*> LiveGroupMembers(Group*g){return g?g->members:std::vector<Player*>{};}
namespace ai {bool CthunSpreadEnabled(PlayerbotAI*);}
'''
        for signature in ('bool Surface(Player* p)','bool Enabled(PlayerbotAI* ai)','Unit* Boss(PlayerbotAI* ai)'):
            fixture+=extract(source,signature)
        if 'bool CthunPullOrdered(' in source:
            fixture+=extract(source,'bool CthunPullOrdered(')
        fixture+=extract(source,'bool ai::CthunSpreadEnabled(PlayerbotAI* ai)')
        fixture+=r'''
int main(){
 PlayerbotAI a,tank;ScriptedInstance instance;Creature eye,body;Group raid;
 body.entry=15727;instance.eye=&eye;instance.body=&body;
 raid.members={&a.bot,&tank.bot};a.bot.group=&raid;tank.bot.group=&raid;
 a.bot.ai=&a;tank.bot.ai=&tank;a.bot.mapObject.data=&instance;
 a.bot.x=-8679.15f;a.bot.y=1975.54f;a.bot.z=108.90f;
 assert(!CthunSpreadEnabled(&a)); // do not start an idle Eye from the waiting area
 a.bot.los=true;assert(!CthunSpreadEnabled(&a)); // even if the waiting area has LOS
 a.bot.los=false;eye.combat=true;
 assert(CthunSpreadEnabled(&a)); // live failure: 37 peers wait outside 95yd, out of combat
 a.enabled=false;assert(!CthunSpreadEnabled(&a));a.enabled=true;
 a.bot.alive=false;assert(!CthunSpreadEnabled(&a));a.bot.alive=true;
 a.bot.teleport=true;assert(!CthunSpreadEnabled(&a));a.bot.teleport=false;
 a.bot.world=false;assert(!CthunSpreadEnabled(&a));a.bot.world=true;
 a.bot.group=nullptr;assert(!CthunSpreadEnabled(&a));a.bot.group=&raid;
 a.bot.z=-96;assert(!CthunSpreadEnabled(&a));a.bot.z=108.9;
 a.bot.x=-8800;assert(!CthunSpreadEnabled(&a));a.bot.x=-8679.15;
 a.bot.map=249;assert(!CthunSpreadEnabled(&a));a.bot.map=531;
 eye.combat=false;
 PullStrategy pull;tank.pull=&pull;pull.active=true;pull.target=&eye;
 assert(CthunSpreadEnabled(&a)); // accepted command: peers move before the shot/combat flag
 pull.active=false;assert(!CthunSpreadEnabled(&a)); // cancellation/expiry, no sticky permission
 pull.active=true;pull.pullback=true;assert(!CthunSpreadEnabled(&a));pull.pullback=false;
 Creature other;other.entry=15299;pull.target=&other;assert(!CthunSpreadEnabled(&a));pull.target=&eye;
 tank.bot.instance=2;assert(!CthunSpreadEnabled(&a));tank.bot.instance=1;
 tank.bot.alive=false;assert(!CthunSpreadEnabled(&a));tank.bot.alive=true;
 pull.active=false;eye.alive=false;body.combat=true;assert(CthunSpreadEnabled(&a));
 body.combat=false;assert(!CthunSpreadEnabled(&a));body.alive=false;assert(!CthunSpreadEnabled(&a));
 eye.alive=true;a.bot.x=kCthunCenter.x-90;a.bot.y=kCthunCenter.y;a.bot.z=100;
 assert(!CthunSpreadEnabled(&a));a.bot.los=true;assert(CthunSpreadEnabled(&a)); // old room entry
}
'''
        run_cpp(fixture)

    def test_actual_path_boundary_and_hazard_rejection(self):
        s=(ROOT/'ai/playerbot/strategy/actions/CthunSpreadActions.cpp').read_text()
        start=s.index('    for(auto const& waypoint:path)');end=s.index('    preparedDestination=',start)
        loop=s[start:end]
        run_cpp(r'''
#include "playerbot/CthunSpreadPolicy.h"
#include <cassert>
using namespace ai;
struct P {float x,y,z;bool hazard=false;float getX()const{return x;}float getY()const{return y;}float getZ()const{return z;}};
bool IsHazardNearPosition(P p){return p.hazard;}
bool accepted(std::vector<P> path){auto blocked=[](){return false;};
'''+loop+r'''
return true;}
int main(){
 // Actual extracted navmesh route from the old waiting point, around ramp bend.
 std::vector<P> path={{-8679.15f,1975.54f,108.979f},{-8667.999f,1961.333f,108.979f},
 {-8663.199f,1962.667f,108.979f},{-8661.333f,1963.467f,108.979f},{-8640,1975,100.579f}};
 assert(accepted(path)); // old 95yd validation rejects the first waypoint
 path[2].hazard=true;assert(!accepted(path));path[2].hazard=false;
 path[2].z=-96;assert(!accepted(path));path[2].z=108.979f;
 path[2].x=-8800;assert(!accepted(path));
}
''')

    def test_entry_route_and_stable_chart(self):
        p=(ROOT/'ai/playerbot/CthunSpreadPolicy.h').read_text()
        self.assertIn('CthunEntryStep',p)
        run_cpp(r'''
#include "playerbot/CthunSpreadPolicy.h"
#include <cassert>
using namespace ai;
int main(){
 for(unsigned sector=0;sector<8;++sector)for(unsigned rank=0;rank<5;++rank){
  auto dest=CthunChartSlot(sector,rank);auto here=kCthunWaiting;
  assert(CthunApproach(531,here.x,here.y,kCthunWaitingZ));
  auto next=CthunEntryStep(here,kCthunWaitingZ,dest,sector);
  assert(CthunDistance(next,kCthunEntry)<.01f);
  here=next;
  unsigned steps=0;
  while(CthunDistance(here,dest)>.1f && steps++<20)
   here=CthunEntryStep(here,100.5f,dest,sector);
  assert(CthunDistance(here,dest)<.1f);
  auto upper=CthunEntryStep(CthunPolar(80,157.5f*kCthunPi/180),108.57f,dest,sector);
  assert(CthunDistance(upper,kCthunEntry)<.01f); // upper ledge cannot bypass ramp mouth
 }
 assert(!CthunApproach(249,kCthunWaiting.x,kCthunWaiting.y,kCthunWaitingZ));
 assert(!CthunApproach(531,kCthunWaiting.x,kCthunWaiting.y,-96));
}
''')
        action=(ROOT/'ai/playerbot/strategy/actions/CthunSpreadActions.cpp').read_text()
        self.assertIn('CthunEntryStep(here,bot->GetPositionZ(),destination,sector)',action)
        self.assertIn('!end.isPathTo(path)',action)
        self.assertIn('player && player->IsInMap(bot) && Surface(player) && Enabled(peer)',action)

    def test_pull_hold_allows_entry_but_not_attacks(self):
        source=(ROOT/'ai/playerbot/strategy/generic/CombatStrategy.cpp').read_text()
        body=extract(source,'float WaitForAttackMultiplier::GetValue(')
        run_cpp(r'''
#include <string>
#include <cassert>
struct Action {std::string n;std::string const& getName(){return n;}};
bool TwinsOpeningTank(int*) { return false; }
struct WaitForAttackStrategy {static bool ShouldWait(int*){return true;}};
struct WaitForAttackMultiplier {int* ai=nullptr;float GetValue(Action*);};
'''+body+r'''
int main(){WaitForAttackMultiplier m;Action entry{"cthun spread move"},attack{"attack"};
 assert(m.GetValue(&entry)==1);assert(m.GetValue(&attack)==0);}
''')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path);a,rest=p.parse_known_args()
    if a.root:ROOT=a.root.resolve()
    unittest.main(argv=[__file__]+rest)
