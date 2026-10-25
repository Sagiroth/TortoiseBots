"""Production selector tail plus real triage policy; movement/LOS are API doubles."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

class HealingLosAssignmentTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_combined_visibility_urgency_and_assignment(self):
        root = Path(__file__).resolve().parents[1]
        source = Path(os.environ.get('HEALING_SOURCE', root/'ai/playerbot/strategy/values/PartyMemberToHeal.cpp')).read_text()
        method = source[source.index('Unit* PartyMemberToHeal::Calculate()'):source.index('bool PartyMemberToHeal::CanHealPet')]
        marker = '    bool const canReach =' if '    bool const canReach =' in method else '    if (candidates.empty())'
        tail = method[method.index(marker):].rstrip()
        cpp = r'''
#include "ai/playerbot/HealingTriagePolicy.h"
#include <cassert>
#include <map>
#include <string>
using namespace ai;
struct Unit { unsigned guid; };
struct AI { bool movable=true,stay=false;
 bool CanMove(){return movable;} bool HasStrategy(std::string,int){return stay;} int GetState(){return 0;}
};
struct Bot { bool casting=false; bool IsNonMeleeSpellCasted(bool,bool,bool){return casting;} };
HealingBands const bands{20,50,70,90};
Unit* select(std::vector<HealingCandidate> candidates, bool focused, std::size_t healerOrdinal,
             AI* ai, Bot* bot, std::map<std::uint64_t,Unit*> const& targets) {
''' + 'std::size_t assignedHealerOrdinal = healerOrdinal;\n' + tail + r'''
int main() {
 AI ai;Bot bot;Unit urgent{1},routine{2},other{3};
 std::map<std::uint64_t,Unit*> targets{{1,&urgent},{2,&routine},{3,&other}};
 HealingCandidate a{1,10,100,0,false,false,false,true};
 HealingCandidate b{2,80,100,0,true,false,false,true};
 auto pick=[&](std::vector<HealingCandidate> c,bool focus=false,std::size_t ordinal=0){return select(c,focus,ordinal,&ai,&bot,targets);};
 assert(pick({b,a})==&urgent); // urgency wins among visible eligible targets
 a.inLos=false;
 assert(pick({a,b})==&routine); // no opportunistic wall chase over a visible member
 assert(pick({a,b},true)==&urgent); // movable explicit assignment may override LOS
 ai.stay=true;assert(pick({a,b},true)==&routine);
 ai.stay=false;ai.movable=false;assert(pick({a,b},true)==&routine);
 ai.movable=true;bot.casting=true;assert(pick({a,b},true)==&routine);
 bot.casting=false;
 b.health=15;assert(pick({a,b},true)==&routine); // same band: LOS before finer priority
 assert(pick({a,b},true,1)==&routine); // assignment must not undo the LOS preference
 a.inLos=true;HealingCandidate c=a;c.guid=3;c.health=12;
 assert(pick({a,c,b},false,0)==&urgent);
 assert(pick({a,c,b},false,1)==&other);
 assert(pick({a,c,b},false,2)==&routine); // all urgent visible slots remain distributable
 a.inLos=false;b.inLos=false;b.health=80;
 assert(pick({a,b})==&urgent); // no visible alternative: normal reach remains possible
 ai.movable=false;assert(pick({a,b})==nullptr);
 assert(pick({})==nullptr);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'fixture.cpp';exe=Path(folder)/'fixture'
            path.write_text(cpp)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-I',str(root),str(path),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

if __name__ == '__main__':
    unittest.main()
