"""Real triage functions and priest trigger wiring; no simulated combat claims."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(os.environ.get('TANK_HEAL_SOURCE', Path(__file__).resolve().parents[1]))

class TankHealingTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_emergency_assignment(self):
        policy = (ROOT/'ai/playerbot/HealingTriagePolicy.h').read_text()
        # Old code ignores combat state. Supply only the new metadata field so
        # the predecessor compiles and fails on its real selection behavior.
        if 'bool inCombat' not in policy:
            policy = policy.replace('bool inLos = true;', 'bool inLos = true; bool inCombat = false;')
        cpp = policy + r'''
#include <cassert>
using namespace ai;
int main() {
 HealingBands bands{20,50,70,90};
 HealingCandidate main{1,40,100,0,true,false,false,true,true};
 HealingCandidate dps{2,30,100,0,false,false,false,true,true};
 auto pick=[&](std::vector<HealingCandidate> raid, unsigned ordinal) {
  std::sort(raid.begin(),raid.end(),[&](auto a,auto b){return HealingPriority(a,b,bands);});
  return raid[HealingAssignment(raid,bands,ordinal)].guid;
 };
 // Sorting a tank first must survive the subsequent assignment step.
 for(unsigned i=0;i<12;++i) assert(pick({main,dps},i)==1);
 dps.health=5;
 for(unsigned i=0;i<12;++i) assert(pick({main,dps},i)==1);
 auto soak=main;soak.guid=3;soak.health=35;
 assert(pick({dps,main,soak},0)==3);
 assert(pick({dps,main,soak},1)==1); // soakers also get emergency help
 main.inCombat=false;assert(pick({main,dps},0)==2);
 main.inCombat=true;main.health=50;assert(pick({main,dps},0)==2);
 main.health=100;assert(!NeedsPartyHealing(main,bands));
 main.health=40;main.incomingHeal=true;assert(NeedsPartyHealing(main,bands));
 main.health=0;assert(!NeedsPartyHealing(main,bands));
}
'''
        with tempfile.TemporaryDirectory() as folder:
            src=Path(folder)/'test.cpp';exe=Path(folder)/'test'
            src.write_text(cpp)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra',str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_fast_heal_in_both_states(self):
        strategy=(ROOT/'ai/playerbot/strategy/priest/PriestStrategy.cpp').read_text()
        for state in ['Combat','NonCombat']:
            section=strategy.split('void PriestStrategy::Init'+state+'Triggers',1)[1].split('\nvoid ',1)[0]
            self.assertIn('"party member tank danger"',section)
            node=section.split('"party member tank danger"',1)[1].split('NULL',1)[0]
            self.assertIn('"flash heal on party", ACTION_CRITICAL_HEAL',node)
            self.assertIn('"power word: shield on party", ACTION_CRITICAL_HEAL + 1',node)
        source=(ROOT/'ai/playerbot/strategy/values/PartyMemberToHeal.cpp').read_text()
        self.assertIn('target->IsInCombat()}',source)
        trigger=(ROOT/'ai/playerbot/strategy/triggers/HealthTriggers.cpp').read_text()
        self.assertIn('return FastTankHealing(candidate, bands);',trigger)
        self.assertIn('target->IsInCombat()',trigger)

if __name__=='__main__': unittest.main()
