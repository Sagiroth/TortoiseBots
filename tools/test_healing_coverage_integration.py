"""Production candidate eligibility and selector tail, with world API doubles."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tools.test_nefarian_bone_target import function

class CoverageIntegrationTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_coverage_then_urgency_assignment_and_los(self):
        root = Path(__file__).resolve().parents[1]
        source = (root/'ai/playerbot/strategy/values/PartyMemberToHeal.cpp').read_text()
        method = function(source, 'Unit* PartyMemberToHeal::Calculate()')
        add = function(method, 'auto addTarget =') + ';'
        tail = method[method.index('    bool const canReach ='):]
        cpp = r'''
#include "ai/playerbot/HealingTriagePolicy.h"
#include "ai/playerbot/HealReachPolicy.h"
#include <cassert>
#include <map>
#include <string>
using namespace ai;
using uint64=std::uint64_t;
namespace BotState {constexpr int BOT_STATE_COMBAT=1;}
namespace SizeFactor {constexpr int CombatReach=1;}
struct Guid {uint64 id;uint64 GetRawValue(){return id;}bool operator<(Guid g)const{return id<g.id;}};
struct Unit {uint64 guid;unsigned hp;bool los=true,covered=false,assigned=false;float distance=10;
 Guid GetObjectGuid(){return {guid};}unsigned GetHealth(){return hp;}unsigned GetMaxHealth(){return 100;}
 float GetHealthPercent(){return hp;}bool IsPlayer(){return true;}bool IsInCombat(){return true;}
};
using Player=Unit;
struct AI {bool combat=true,stay=false;bool IsStateActive(int){return combat;}
 bool CanMove(){return true;}bool HasStrategy(std::string,int){return stay;}int GetState(){return 0;}};
struct Bot {float GetDistance(Unit* u,int){return u->distance;}bool IsWithinLOSInMap(Unit* u,bool){return u->los;}
 bool IsNonMeleeSpellCasted(bool,bool,bool){return false;}};
struct {unsigned lowHealth=50,criticalHealth=20;} sPlayerbotAIConfig;
struct {bool IsWithinLOSInMap(Bot*,Unit* u){return u->los;}} sServerFacade;
float HealReachLimit(AI*){return 40;}
bool AnotherHealerCoversAlly(AI*,Unit* u,float){return u->covered;}
bool IsPatchwerkHealingAssignment(Player* u){return u->assigned;}
unsigned getIncomingdamage(Unit*){return 0;}
Unit* select(std::vector<Unit*> input, AI* ai, Bot* bot, bool focused=false) {
 HealingBands bands{20,50,70,90};bool preHealing=false;std::size_t healerOrdinal=0,assignedHealerOrdinal=0;
 std::map<Guid,unsigned> incomingHeals;std::vector<HealingCandidate> candidates;std::map<uint64,Unit*> targets;
 auto Check=[](Unit* u){return u!=nullptr;};
''' + add + '\n for(Unit* u:input)addTarget(u,u->assigned,false);\n' + tail + r'''
int main(){
 AI ai;Bot bot;Unit assigned{1,30},visible{2,80};assigned.assigned=true;assigned.los=false;assigned.covered=true;
 // A covered noncritical assignment cannot draw a healer away from reachable work.
 assert(select({&assigned,&visible},&ai,&bot,true)==&visible);
 assigned.hp=20;assert(select({&assigned,&visible},&ai,&bot,true)==&assigned); // critical bypass
 ai.stay=true;assert(select({&assigned,&visible},&ai,&bot,true)==&visible);ai.stay=false;
 assigned.hp=30;assigned.covered=false;assert(select({&assigned,&visible},&ai,&bot,true)==&assigned);
 assert(select({&assigned,&visible},&ai,&bot)==&visible); // ordinary LOS preference
 assigned.covered=true;assigned.los=true;assigned.distance=45;
 assert(select({&assigned,&visible},&ai,&bot,true)==&visible); // range also filters
 assigned.distance=40;assert(select({&assigned,&visible},&ai,&bot)==&assigned); // in-range urgency
 assigned.distance=45;ai.combat=false;assert(select({&assigned,&visible},&ai,&bot,true)==&assigned);
 assert(select({},&ai,&bot)==nullptr);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            src=Path(folder)/'fixture.cpp';exe=Path(folder)/'fixture';src.write_text(cpp)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-I',str(root),str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

if __name__=='__main__':unittest.main()
