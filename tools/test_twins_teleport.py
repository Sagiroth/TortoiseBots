#!/usr/bin/env python3
"""Twins core-path fixtures: scripted relocation, freeze, proximity pickup."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
def extract(source,signature):
    start=source.index(signature);end=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
def run_cpp(code):
    with tempfile.TemporaryDirectory() as directory:
        path=Path(directory);src=path/'test.cpp';exe=path/'test'
        src.write_text(code)
        subprocess.run(['g++','-std=c++17','-I'+str(ROOT/'ai'),str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
class TwinsTeleportTest(unittest.TestCase):
    def source(self):return (ROOT/'ai/playerbot/strategy/actions/TwinsOpeningActions.cpp').read_text()
    def test_spare_warrior_is_nearest_before_swap(self):
        # Runs unchanged on 519: its standby warrior is as far as the warlock.
        body=extract(self.source(),'bool DesiredTankPosition(')
        run_cpp(r'''
#include "playerbot/TwinsOpeningPolicy.h"
#include <cassert>
using namespace ai;
struct Unit{TwinsPoint p;float radius=1.5;float GetObjectBoundingRadius(){return radius;}};
struct PlayerbotAI{Unit bot{{},.5},physical{kTwinsWest},caster{kTwinsEast};unsigned entry=15275;int side=1;bool preparing=true,local=false;Unit*GetBot(){return &bot;}};
unsigned TankEntry(PlayerbotAI*a){return a->entry;}int Side(PlayerbotAI*a){return a->side;}
Unit* LocalTankBoss(PlayerbotAI*a){return a->local?&a->physical:nullptr;}
bool OtherOrder(PlayerbotAI*,Unit*){return false;}
Unit* Boss(PlayerbotAI*a,unsigned e){return e==15275?&a->physical:&a->caster;}
TwinsPoint Point(Unit*u){return u->p;}
bool TwinsPreparing(PlayerbotAI*a){return a->preparing;}
'''+body+r'''
int main(){PlayerbotAI a;TwinsPoint warrior,warlock;
 assert(DesiredTankPosition(&a,warrior));
 // 13 yards edge clearance, closer than warlock but outside 10-yard burst.
 assert(std::abs(TwinsDistance(warrior,kTwinsEast)-15)<.01);
 a.entry=15276;assert(DesiredTankPosition(&a,warlock));
 assert(TwinsDistance(warrior,kTwinsEast)<TwinsDistance(warlock,kTwinsEast));
 a.entry=15275;a.preparing=false;assert(DesiredTankPosition(&a,warrior));
 assert(std::abs(TwinsDistance(warrior,kTwinsEast)-24)<.01); // withdraw after caster landing
 a.local=true;a.side=0;assert(DesiredTankPosition(&a,warrior));
 assert(TwinsDistance(warrior,TwinsTankCamp(0))<.01); // physical tank holds wall side
 a.entry=0;assert(!DesiredTankPosition(&a,warrior));
}
''')
    def test_scripted_position_swap_clock(self):
        run_cpp(r'''
#include "playerbot/TwinsTeleportPolicy.h"
#include <cassert>
using namespace ai;
int main(){TwinsTeleportState s;
 s.Observe(100,false,kTwinsWest,kTwinsEast,false);
 s.Observe(200,true,kTwinsWest,kTwinsEast,true);assert(!s.Holding());
 s.Observe(25199,true,kTwinsWest,kTwinsEast,true);assert(!s.Holding());
 s.Observe(25200,true,kTwinsWest,kTwinsEast,true);assert(s.Preparing());
 // No 799 cast. Actual pair exchanges coordinates; victim initially null.
 s.Observe(30200,true,kTwinsEast,kTwinsWest,false);
 assert(s.epoch==1 && s.handoff && !s.Preparing() && s.Holding());
 s.Observe(31000,true,kTwinsEast,kTwinsWest,true);assert(s.Holding()); // freeze still active
 s.Observe(32200,true,kTwinsEast,kTwinsWest,false);assert(s.Holding()); // wrong caster owner
 s.Observe(33200,true,kTwinsEast,kTwinsWest,true);assert(s.Holding());
 s.Observe(33500,true,kTwinsEast,kTwinsWest,false);assert(s.Holding()); // unstable pickup resets
 s.Observe(34200,true,kTwinsEast,kTwinsWest,true);
 s.Observe(34700,true,kTwinsEast,kTwinsWest,true);assert(!s.Holding());
 s.Observe(55200,true,kTwinsEast,kTwinsWest,true);assert(s.Preparing());
 // Partial read preserves last complete pair instead of losing the next swap.
 s.Observe(60200,true,kTwinsWest,kTwinsWest,false);
 s.Observe(60201,true,kTwinsWest,kTwinsEast,false);assert(s.epoch==2 && s.handoff);
 s.Observe(65000,false,kTwinsWest,kTwinsEast,false);assert(!s.active && !s.Holding());
 s.Observe(65100,true,kTwinsWest,kTwinsEast,false);assert(!s.Holding() && s.epoch==0);
 TwinsTeleportState late;late.Observe(90000,true,kTwinsWest,kTwinsEast,true);
 assert(late.Preparing()); // no trustworthy initial timestamp: clear first
 late.Observe(100000,true,kTwinsEast,kTwinsWest,false);assert(late.epoch==1);
 // Ordinary movement, even a side change after collapse, is not an observed pair swap.
 TwinsTeleportState walk;walk.Observe(1,false,kTwinsWest,kTwinsEast,false);
 walk.Observe(2,true,kTwinsWest,kTwinsEast,true);
 walk.Observe(3,true,{-8950,1190,-104},{-8940,1190,-104},true);assert(walk.epoch==0);
}
''')
    def test_group_remains_engaged_during_zero_threat_freeze(self):
        body=extract(self.source(),'bool GroupEngaged(')
        run_cpp(r'''
#include <cassert>
#include <vector>
#include <set>
struct Guid{bool IsPlayer()const{return true;}};
struct HostileReference{Guid getUnitGuid()const{return {};}};
struct Group{};struct Player;
struct Map{Player*p=nullptr;Player*GetPlayer(Guid){return p;}};
struct Threat{std::vector<HostileReference*>refs;auto const&getThreatList(){return refs;}};
struct Unit{bool combat=true,player=false;Unit*victim=nullptr;Threat threat;Map map;
 bool IsInCombat(){return combat;}Unit*GetVictim(){return victim;}bool IsPlayer(){return player;}
 Threat&GetThreatManager(){return threat;}Map*GetMap(){return &map;}};
struct Player:Unit{Group*group;bool alive=true;Group*GetGroup(){return group;}bool IsAlive(){return alive;}
 bool IsInMap(Player*){return true;}std::set<Unit*> GetAttackers(){return {};}};
struct PlayerbotAI{Player bot;Player*GetBot(){return &bot;}};
std::vector<Player*> LiveGroupMembers(Group*){return {};}
'''+body+r'''
int main(){Group own,other;PlayerbotAI a;a.bot.group=&own;Unit boss;HostileReference ref;
 boss.threat.refs={&ref};boss.map.p=&a.bot;
 // DoStopAttack + all DPS stopped: neither boss nor group has an active victim.
 assert(!boss.victim && !a.bot.victim);assert(GroupEngaged(&a,&boss));
 a.bot.group=&other;Player unrelated;unrelated.group=&own;boss.map.p=&unrelated;
 assert(!GroupEngaged(&a,&boss));
 boss.combat=false;assert(!GroupEngaged(&a,&boss));assert(!GroupEngaged(&a,nullptr));
}
''')
    def test_side_heal_filter_and_damage_gate(self):
        body=extract(self.source(),'bool ai::TwinsHealTargetAllowed(')
        run_cpp(r'''
#include "playerbot/TwinsOpeningPolicy.h"
#include <cassert>
using namespace ai;
struct Unit{TwinsPoint p;};struct PlayerbotAI{Unit bot{kTwinsWest};bool healer=true,enabled=true;int side=0;Unit*GetBot(){return &bot;}};
bool HealerRole(PlayerbotAI*a){return a->healer;}bool TwinsOpeningEnabled(PlayerbotAI*a){return a->enabled;}
TwinsPoint Point(Unit*u){return u->p;}int Side(PlayerbotAI*a){return a->side;}
namespace ai{bool TwinsHealTargetAllowed(PlayerbotAI*,Unit*);}
'''+body+r'''
int main(){PlayerbotAI a;Unit west{kTwinsWest},east{kTwinsEast};
 assert(TwinsHealTargetAllowed(&a,&west));assert(!TwinsHealTargetAllowed(&a,&east));
 assert(TwinsHealTargetAllowed(&a,&a.bot));assert(!TwinsHealTargetAllowed(&a,nullptr));
 a.side=1;assert(TwinsHealTargetAllowed(&a,&east));assert(!TwinsHealTargetAllowed(&a,&west));
 a.enabled=false;assert(TwinsHealTargetAllowed(&a,&west));a.enabled=true;a.healer=false;
 assert(TwinsHealTargetAllowed(&a,&west));}
''')
        source=self.source()
        self.assertIn('if(DpsHolding(ai)) return true;',source)
        self.assertIn('action->getThreatType()==ActionThreatType::ACTION_THREAT_AOE',source)
        self.assertIn('positioning && positioning->OwnsMovement()',source)
        self.assertIn('bot->IsNonMeleeSpellCasted(true,false,true)) return false',source)
        self.assertIn('TwinsHealTargetAllowed(ai, player)',(ROOT/'ai/playerbot/strategy/values/PartyMemberToHeal.cpp').read_text())
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path);a,rest=p.parse_known_args()
    if a.root:ROOT=a.root.resolve()
    unittest.main(argv=[__file__]+rest)
