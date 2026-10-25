"""Production spread/assist regressions for phase-two ranged damage."""
import os
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(os.environ.get('TBOTS_P2_SOURCE',Path(__file__).resolve().parents[1]))
def extract(s,key):
 a=s.index(key);b=s.index('{',a)+1;d=1
 while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
def run(s):
 with tempfile.TemporaryDirectory() as d:
  p=Path(d);(p/'t.cpp').write_text(s)
  subprocess.run(['g++','-std=c++17','-I'+str(ROOT/'ai'),str(p/'t.cpp'),'-o',str(p/'t')],check=True)
  subprocess.run([str(p/'t')],check=True)
class OnyxiaP2FocusTest(unittest.TestCase):
 def test_ranged_can_reach_boss_from_spread_point(self):
  s=(ROOT/'ai/playerbot/strategy/generic/OnyxiasLairDungeonStrategies.cpp').read_text();s=s[s.index('class OnyxiaAirSpreadMultiplier'):]
  f=extract(s,'float GetValue(').replace(' override','').replace('float GetValue(','float OnyxiaAirSpreadMultiplier::GetValue(')
  run('''
#include <cassert>
#include <string>
struct Unit{}boss,other;struct PlayerbotAI{};
bool enabled=true,focus=true;
bool OnyxiaAirSpreadEnabled(PlayerbotAI*){return enabled;}
bool OnyxiaRangedFocusEnabled(PlayerbotAI*){return focus;}
Unit*GetOnyxiaRangedTarget(PlayerbotAI*){return &boss;}
struct Action{std::string name;Unit*target=&boss;Action(std::string n):name(n){}virtual ~Action(){}std::string getName(){return name;}Unit*GetTarget(){return target;}};
struct MovementAction:Action{using Action::Action;};struct ReachTargetAction:MovementAction{using MovementAction::MovementAction;};
struct CastReachTargetSpellAction:Action{using Action::Action;};struct AttackAction:Action{using Action::Action;};
struct OnyxiaAirSpreadMultiplier{PlayerbotAI*ai=nullptr;float GetValue(Action*);};
'''+f+'''
int main(){OnyxiaAirSpreadMultiplier m;ReachTargetAction reach("reach spell");MovementAction follow("follow");
assert(m.GetValue(&reach)==1);assert(m.GetValue(&follow)==0);
reach.target=&other;assert(m.GetValue(&reach)==0);focus=false;reach.target=&boss;assert(m.GetValue(&reach)==0);
enabled=false;assert(m.GetValue(&follow)==1);}
''')
 def test_ranged_leaves_a_whelp_after_healer_is_safe(self):
  s=(ROOT/'ai/playerbot/strategy/actions/OnyxiasLairDungeonActions.cpp').read_text()
  run('''
#include <cassert>
#include <string>
constexpr unsigned kOnyxiaMapId=249,kOnyxianWhelpEntry=11262;
struct Guid{bool IsEmpty(){return true;}};
struct Unit{unsigned GetEntry(){return 11262;}}current;
struct Player:Unit{unsigned GetMapId(){return 249;}}player;
struct PlayerbotAI{bool IsRanged(Player*){return true;}}brain;
struct Context{};
template<class T>T value(){if constexpr(__is_same(T,Guid))return {};else return &current;}
#define AI_VALUE(T,N) value<T>()
using ObjectGuid=Guid;
struct Action{virtual ~Action(){} };struct DpsAssistAction:Action{};struct TankAssistAction:Action{};struct DpsAoeAction:Action{};
struct Scan{bool bossAirborne=true;Unit*whelp=nullptr;};Scan scan;
Scan ScanOnyxiaWhelps(PlayerbotAI*,Player*){return scan;}bool IsOnyxiaWhelpDuty(PlayerbotAI*,Player*){return true;}
struct Config{bool onyxiaWhelpsAndEggs=true;}sPlayerbotAIConfig;
struct Facade{bool IsAlive(Unit*){return true;}}sServerFacade;
struct OnyxiaWhelpHoldMultiplier{Player*bot=&player;PlayerbotAI*ai=&brain;Context*context=nullptr;float GetValue(Action*);};
'''+extract(s,'float OnyxiaWhelpHoldMultiplier::GetValue(')+'''
int main(){OnyxiaWhelpHoldMultiplier m;DpsAssistAction assist;
assert(m.GetValue(&assist)==1);scan.whelp=&current;assert(m.GetValue(&assist)==0);
scan.bossAirborne=false;assert(m.GetValue(&assist)==1);}
''')
 def test_rescue_only_own_group_healers(self):
  run('''
#include "playerbot/OnyxiaAirPolicy.h"
#include <cassert>
using namespace ai;
int main(){assert(!OnyxiaAirWhelpAllowed(true,false,true));assert(!OnyxiaAirWhelpAllowed(true,true,false));
assert(OnyxiaAirWhelpAllowed(true,true,true));assert(OnyxiaAirWhelpAllowed(false,false,true));}
''')
if __name__=='__main__':unittest.main()
