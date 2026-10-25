#!/usr/bin/env python3
"""Production-function regressions for the batch-13c live Twins openings."""
import argparse
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
def extract(source,signature):
    start=source.index(signature);end=source.index('{',start)+1;depth=1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
def cpp(text):
    with tempfile.TemporaryDirectory() as d:
        p=Path(d);(p/'t.cpp').write_text(text)
        subprocess.run(['g++','-std=c++17','-I'+str(ROOT/'ai'),str(p/'t.cpp'),'-o',str(p/'t')],check=True)
        subprocess.run([str(p/'t')],check=True)
class Recovery(unittest.TestCase):
    def test_collapsed_pair_and_wrong_owner_hold_damage(self):
        cpp(r'''
#include "playerbot/TwinsTeleportPolicy.h"
#include <cassert>
using namespace ai;
int main(){TwinsTeleportState s;
s.Observe(1,false,kTwinsWest,kTwinsEast,false);
s.Observe(2,true,kTwinsWest,kTwinsEast,false);
assert(s.Holding()); // full spawn separation alone is not tank ownership
s.Observe(1000,true,{-8942,1200,-104},{-8934,1200,-104},false);
assert(s.Holding()); // real failure: adjacent twins, neither on the right tank
s.Observe(2000,true,kTwinsWest,kTwinsEast,true);assert(!s.Holding());
s.Observe(3000,true,{-8942,1200,-104},{-8934,1200,-104},false);assert(s.Holding());
s.Observe(4000,true,kTwinsWest,kTwinsEast,true);assert(!s.Holding());
s.Observe(5000,false,kTwinsWest,kTwinsEast,false);assert(!s.Holding());}
''')
    def test_assigned_tank_can_recover_wrong_side_boss(self):
        s=(ROOT/'ai/playerbot/strategy/actions/TwinsOpeningActions.cpp').read_text()
        cpp(r'''
#include "playerbot/TwinsTeleportPolicy.h"
#include <cassert>
using namespace ai;
struct Unit{TwinsPoint p;};
struct PlayerbotAI{Unit physical{kTwinsEast},caster{kTwinsEast};unsigned entry=15275;int side=0;};
TwinsTeleportState state;
unsigned TankEntry(PlayerbotAI*a){return a->entry;}int Side(PlayerbotAI*a){return a->side;}
Unit*Boss(PlayerbotAI*a,unsigned e){return e==15275?&a->physical:&a->caster;}
TwinsPoint Point(Unit*u){return u->p;}bool OtherOrder(PlayerbotAI*,Unit*){return false;}
TwinsTeleportState ObserveTwins(PlayerbotAI*){return state;}
'''+extract(s,'Unit* LocalTankBoss(')+r'''
int main(){PlayerbotAI a;state.active=true;
// The old code requires the boss to be on our side already: recovery impossible.
state.opening=true;assert(LocalTankBoss(&a)==&a.physical);
a.side=1;assert(!LocalTankBoss(&a)); // spare physical tank must not join
state.opening=false;assert(LocalTankBoss(&a)==&a.physical); // after handoff, east receives physical
state.opening=true;state.epoch=1;state.physical=kTwinsEast;state.caster=kTwinsWest;
a.physical.p=kTwinsWest;assert(LocalTankBoss(&a)==&a.physical); // preserve exchanged team on recovery
}
''')
    def test_mature_controls_execute_and_report(self):
        s='\n'.join(p.read_text() for p in sorted((ROOT/'commands').glob('*.cpp')))
        cpp(r'''
#include <string>
#include <vector>
#include <cassert>
struct Player{};struct BotRecord{};
struct ChatHandler{std::vector<std::string> messages;template<class...T>void PSendSysMessage(const char*f,T...){messages.push_back(f);}};
enum class BotState{BOT_STATE_ALL,BOT_STATE_COMBAT};constexpr int CHAT_MSG_WHISPER=1;
struct PlayerbotAI{bool changed=false,printed=false,queued=false;
void ChangeStrategy(std::string,BotState){changed=true;}
void PrintStrategies(Player*,BotState){printed=true;}
void HandleCommand(int,std::string,Player&){queued=true;}};
Player player;PlayerbotAI brain;bool held=false;
Player*Requester(ChatHandler*){return &player;}std::string Trim(std::string s){return s;}
bool ResolveOwnedBot(ChatHandler*,const char*,Player*&p,BotRecord*&,std::string&n){p=&player;n="Example";return true;}
struct Store{void Save(PlayerbotAI*){}}sPlayerbotDbStore;
struct PlayerbotAIStorage{static PlayerbotAIStorage&Instance(){static PlayerbotAIStorage s;return s;}PlayerbotAI*GetAI(Player*){return &brain;}};
bool KnownStrategyChange(std::vector<Player*>,std::string s){return s=="+twins west";}
bool HandleStay(ChatHandler*,const char*){held=true;return true;}
'''+extract(s,'bool HandleMatureCommand(')+r'''
int main(){ChatHandler h;
HandleMatureCommand(&h,"Example all +twins west");assert(brain.changed && brain.printed && !brain.queued);
brain={};HandleMatureCommand(&h,"Example stay");assert(held && !brain.queued);
brain={};HandleMatureCommand(&h,"Example all +twins wset");assert(!brain.changed && !brain.queued);
HandleMatureCommand(&h,"Example co ?");assert(brain.queued);}
''')
    def test_pending_pull_allows_assigned_engagement(self):
        s=(ROOT/'ai/playerbot/strategy/generic/PullStrategy.cpp').read_text()
        cpp(r'''
#include <string>
#include <cassert>
constexpr int ACTION_EMERGENCY=100,ACTION_INTERRUPT=50;
struct Action{std::string name;std::string getName(){return name;}int getRelevance(){return 0;}};
bool assigned=true;bool TwinsOpeningTank(int*){return assigned;}
struct PullStrategy{static const PullStrategy*Get(int*){static PullStrategy s;return &s;}bool GetBossOpeningTarget()const{return false;}bool HasTarget()const{return true;}};
struct PullMultiplier{int*ai=nullptr;float GetValue(Action*);};
'''+extract(s,'float PullMultiplier::GetValue(')+r'''
int main(){PullMultiplier m;Action engage{"twins engage"},spell{"twins searing pain"},stance{"defensive stance"},ordinary{"attack"};
assert(m.GetValue(&engage)==1 && m.GetValue(&spell)==1 && m.GetValue(&stance)==1);
assert(m.GetValue(&ordinary)==0);assigned=false;assert(m.GetValue(&engage)==0);}
''')
    def test_selected_spare_cannot_take_assigned_pull(self):
        s=(ROOT/'commands/BotCommandContext.cpp').read_text()
        cpp(r'''
#include <vector>
#include <string>
#include <cassert>
using uint8=unsigned char;
struct PlayerbotAI;struct Player{unsigned entry=0;PlayerbotAI*brain=nullptr;unsigned GetEntry(){return entry;}};
enum class BotState{BOT_STATE_COMBAT,BOT_STATE_NON_COMBAT};constexpr int BOT_ROLE_TANK=1;
struct PlayerbotAI{unsigned assigned=0;bool HasStrategy(const char*,BotState){return false;}int GetForcedRole(){return 1;}};
struct PlayerbotAIStorage{static PlayerbotAIStorage&Instance(){static PlayerbotAIStorage s;return s;}PlayerbotAI*GetAI(Player*p){return p->brain;}};
struct BotCommandContext{Player*requester,*enemyTarget,*selectedBot;std::vector<Player*>partyBots;};
bool IsPullCandidate(Player*,Player*){return true;}bool configured=true;
namespace ai{bool TwinsRaidControlled(PlayerbotAI*){return configured;}bool TwinsPullCandidate(PlayerbotAI*a,Player*t){return a->assigned==t->entry;}}
'''+extract(s,'Player* ResolvePullExecutor(')+r'''
int main(){PlayerbotAI spareAI,warAI{15275},lockAI{15276};Player owner,boss{15275},spare{0,&spareAI},war{0,&warAI},lock{0,&lockAI};
BotCommandContext c{&owner,&boss,&spare,{&spare,&war,&lock}};
assert(ResolvePullExecutor(c,true)==&war);boss.entry=15276;assert(ResolvePullExecutor(c,true)==&lock);
lockAI.assigned=0;assert(!ResolvePullExecutor(c,true));configured=false;assert(ResolvePullExecutor(c,true)==&spare);}
''')
    def test_attack_permissions_actual_function(self):
        s=(ROOT/'ai/playerbot/strategy/actions/TwinsOpeningActions.cpp').read_text()
        cpp(r'''
#include "playerbot/TwinsTeleportPolicy.h"
#include <cassert>
using namespace ai;
struct Unit{unsigned entry=15275;unsigned GetEntry(){return entry;}};
struct PlayerbotAI{Unit bot,physical{15275},caster{15276};bool tank=false,heal=false;unsigned entry=0,damage=0;int side=0;Unit*local=nullptr;
Unit*GetBot(){return &bot;}bool IsTank(Unit*){return tank;}bool IsHeal(Unit*){return heal;}};
TwinsTeleportState state;bool controlled=true,ordered=false;
bool TwinsRaidControlled(PlayerbotAI*){return controlled;}TwinsTeleportState ObserveTwins(PlayerbotAI*){return state;}
bool PullOrdered(PlayerbotAI*,Unit*,Unit*){return ordered;}Unit*Boss(PlayerbotAI*a,unsigned e){return e==15275?&a->physical:&a->caster;}
unsigned TankEntry(PlayerbotAI*a){return a->entry;}int Side(PlayerbotAI*a){return a->side;}
Unit*LocalTankBoss(PlayerbotAI*a){return a->local;}Unit*SideTank(PlayerbotAI*a,unsigned,int){return a->GetBot();}
bool Has(PlayerbotAI*a,const char*n){return a->damage==(std::string(n)=="twins physical dps"?15275:15276);}
bool PairWaiting(PlayerbotAI*){return false;}
namespace ai{bool TwinsMayAttack(PlayerbotAI*,Unit*);}
'''.replace('#include <cassert>','#include <cassert>\n#include <string>')+extract(s,'bool ai::TwinsMayAttack(')+r'''
int main(){PlayerbotAI a;a.tank=true;a.damage=15275;
assert(!TwinsMayAttack(&a,&a.physical));ordered=true;assert(!TwinsMayAttack(&a,&a.physical));
state.active=true;state.knownCycle=true;state.opening=true;assert(!TwinsMayAttack(&a,&a.caster));assert(!TwinsMayAttack(&a,&a.physical));
a.entry=15275;a.local=&a.physical;assert(TwinsMayAttack(&a,&a.physical));assert(!TwinsMayAttack(&a,&a.caster));
a.local=nullptr;assert(!TwinsMayAttack(&a,&a.physical));
a.entry=15276;a.local=&a.caster;assert(TwinsMayAttack(&a,&a.caster));assert(!TwinsMayAttack(&a,&a.physical));
a.entry=0;state.opening=false;assert(TwinsMayAttack(&a,&a.physical));assert(!TwinsMayAttack(&a,&a.caster));
state.handoff=true;assert(!TwinsMayAttack(&a,&a.physical));controlled=false;assert(TwinsMayAttack(&a,&a.caster));
}
''')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path);a,rest=p.parse_known_args()
    if a.root:ROOT=a.root.resolve()
    unittest.main(argv=[__file__]+rest)
