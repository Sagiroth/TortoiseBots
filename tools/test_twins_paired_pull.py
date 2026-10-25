#!/usr/bin/env python3
"""Execute the production pair coordinator with two independently ready tanks."""
import argparse
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
def extract(s,signature):
    start=s.index(signature);end=s.index('{',start)+1;depth=1
    while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]
def cpp(source):
    with tempfile.TemporaryDirectory() as d:
        p=Path(d);(p/'test.cpp').write_text(source)
        subprocess.run(['g++','-std=c++17',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test')],check=True)
class PairedPullTest(unittest.TestCase):
    def source(self):return (ROOT/'ai/playerbot/strategy/actions/TwinsOpeningActions.cpp').read_text()
    def test_ready_warrior_must_not_fire_while_partner_is_approaching(self):
        # Execute the actual attack gate on both revisions. Before 523 this
        # allows the elected warrior to shoot while the warlock is out of range.
        s=self.source()
        cpp(r'''
#include <cassert>
#include <string>
struct Unit{unsigned entry;unsigned GetEntry(){return entry;}};
struct PlayerbotAI{Unit bot{0},physical{15275},caster{15276};Unit*GetBot(){return &bot;}};
struct State{bool active=false,opening=true;bool Holding(){return opening;}}state;
bool waiting=true,dispatch=false,ordered=true,controlled=true;
bool PairWaiting(PlayerbotAI*){return waiting && !dispatch;}
bool TwinsRaidControlled(PlayerbotAI*){return controlled;}
State ObserveTwins(PlayerbotAI*){return state;}
bool PullOrdered(PlayerbotAI*,Unit*,Unit*){return ordered;}
Unit*Boss(PlayerbotAI*a,unsigned e){return e==15275?&a->physical:&a->caster;}
unsigned TankEntry(PlayerbotAI*){return 15275;}int Side(PlayerbotAI*){return 0;}
Unit*LocalTankBoss(PlayerbotAI*a){return &a->physical;}
Unit*SideTank(PlayerbotAI*a,unsigned,int){return &a->bot;}
bool Has(PlayerbotAI*,const char*){return false;}unsigned TwinsDamageEntry(bool,bool){return 0;}
namespace ai{bool TwinsMayAttack(PlayerbotAI*,Unit*);}
'''.replace('Unit*GetBot(){return &bot;}','Unit*GetBot(){return &bot;}bool IsTank(Unit*){return true;}bool IsHeal(Unit*){return false;}')+extract(s,'bool ai::TwinsMayAttack(')+r'''
int main(){PlayerbotAI a;
assert(!ai::TwinsMayAttack(&a,&a.physical)); // waiting for the east tank
state.active=true;assert(!ai::TwinsMayAttack(&a,&a.physical)); // linked proximity aggro is not permission
state.active=false;dispatch=true;assert(ai::TwinsMayAttack(&a,&a.physical));
assert(!ai::TwinsMayAttack(&a,&a.caster)); // never the wrong brother
waiting=false;dispatch=false;assert(ai::TwinsMayAttack(&a,&a.physical));
ordered=false;assert(!ai::TwinsMayAttack(&a,&a.physical));
controlled=false;assert(ai::TwinsMayAttack(&a,&a.caster)); // unrelated parties unchanged
}
''')
    def test_actual_coordinator_readiness_dispatch_and_cancellation(self):
        s=self.source()
        cpp(r'''
#include <cassert>
#include <string>
#include <vector>
#include <mutex>
#include <utility>
using uint32=unsigned;using ObjectGuid=unsigned;
struct Event{};constexpr int UNIT_STAND_STATE_STAND=0;
uint32 now=1000;struct WorldTimer{static uint32 getMSTime(){return now;}};
std::vector<std::string> calls;
struct Unit{unsigned guid;unsigned GetObjectGuid(){return guid;}}physical{15275},caster{15276};
struct Player;struct Map{Player*GetPlayer(unsigned);};Map map;
struct Player:Unit{bool alive=true,los=true,casting=false;float distance=24;int group=1;
bool IsAlive(){return alive;}int GetGroup(){return group;}Map*GetMap(){return &map;}
float GetDistance(Unit*){return distance;}bool IsWithinLOSInMap(Unit*,bool){return los;}
bool IsNonMeleeSpellCasted(bool,bool,bool){return casting;}
bool HasSpell(int){return true;}void AttackStop(){}void SetStandState(int){}void SetSelectionGuid(unsigned){}
}west{{1}},east{{2}};
Player*Map::GetPlayer(unsigned g){return g==1?&west:g==2?&east:nullptr;}
struct Context{Unit*target=nullptr;template<class T>Context*GetValue(const char*){return this;}void Set(Unit*u){target=u;}};
struct PlayerbotAI{Player*bot;Context context;bool ready=true,castOK=true,stance=true;
Player*GetBot(){return bot;}Context*GetAiObjectContext(){return &context;}void StopMoving(){}
bool HasAura(const char*,Player*){return stance;}bool DoSpecificAction(const char*,Event&,bool){stance=true;calls.push_back("stance");return true;}
bool CanCastSpell(const char*,Unit*t){assert(t==&caster);return ready;}
bool CastSpell(const char*,Unit*t){assert(t==&caster);calls.push_back("pain");return castOK;}
}warrior{&west},warlock{&east};
struct PlayerbotAIStorage{static PlayerbotAIStorage&Instance(){static PlayerbotAIStorage x;return x;}PlayerbotAI*GetAI(Player*p){return p==&west?&warrior:&warlock;}};
struct PullStrategy{bool active=true,back=false;Unit*target=&physical;bool noted=false;
static PullStrategy*Get(PlayerbotAI*){static PullStrategy p;return &p;}bool IsCommandActive(){return active;}bool IsCommandPullback(){return back;}Unit*GetTarget(){return target;}
void OnPullEnded(){active=false;}void NotePullActionCast(){noted=true;}};
struct PairedPull{ObjectGuid owner=1,target=15275;uint32 started=1,launched=0;bool pending=true,dispatching=false;}pair;
std::recursive_mutex pairMutex;PairedPull*Pair(PlayerbotAI*){return &pair;}
bool teams=true;bool TwinsTeamsReady(PlayerbotAI*){return teams;}
Player*SideTank(PlayerbotAI*,unsigned e,int side){assert(side==(e==15275?0:1));return e==15275?&west:&east;}
Unit*Boss(PlayerbotAI*,unsigned e){return e==15275?&physical:&caster;}
struct Facade{void SetFacingTo(Player*,Unit*){}}sServerFacade;
struct Logger{std::string result;void logEvent(PlayerbotAI*,const char*,std::string r,std::string){result=r;}}sPlayerbotAIConfig;
bool shotReady=true,shotOK=true;
struct CastShootAction{PlayerbotAI*a;CastShootAction(PlayerbotAI*p):a(p){}bool isPossible(){assert(pair.dispatching);assert(a==&warrior && a->context.target==&physical);return shotReady;}
bool Execute(Event&){assert(pair.dispatching);calls.push_back("shoot");return shotOK;}};
'''+extract(s,'void UpdatePairedPull(')+r'''
void reset(){pair={};now=1000;teams=true;west=Player{{1}};east=Player{{2}};
warrior.ready=warlock.ready=warlock.castOK=warrior.stance=shotReady=shotOK=true;
*PullStrategy::Get(&warrior)=PullStrategy{};calls.clear();}
int main(){
reset();east.distance=60;UpdatePairedPull(&warrior);assert(calls.empty() && pair.pending);
east.distance=24;warlock.ready=false;UpdatePairedPull(&warrior);assert(calls.empty() && pair.pending && !pair.dispatching);
warlock.ready=true;shotReady=false;UpdatePairedPull(&warrior);assert(calls.empty());
shotReady=true;east.los=false;UpdatePairedPull(&warrior);assert(calls.empty());
east.los=true;UpdatePairedPull(&warrior);
assert((calls==std::vector<std::string>{"pain","shoot"}));assert(!pair.pending && !pair.dispatching);
assert(PullStrategy::Get(&warrior)->noted && sPlayerbotAIConfig.result=="launched");
UpdatePairedPull(&warlock);assert(calls.size()==2); // no duplicate from partner's update
reset();pair.owner=2;pair.target=15276;PullStrategy::Get(&warlock)->target=&caster;
UpdatePairedPull(&warlock);assert((calls==std::vector<std::string>{"pain","shoot"})); // either selected twin
reset();warrior.stance=false;east.distance=60;UpdatePairedPull(&warrior);assert(calls==std::vector<std::string>{"stance"});
reset();PullStrategy::Get(&warrior)->active=false;UpdatePairedPull(&warrior);assert(calls.empty()&&!pair.pending);
reset();PullStrategy::Get(&warrior)->target=&caster;UpdatePairedPull(&warrior);assert(calls.empty()&&!pair.pending);assert(PullStrategy::Get(&warrior)->active);
reset();east.alive=false;teams=false;UpdatePairedPull(&warrior);assert(calls.empty()&&!pair.pending);
reset();now=31002;UpdatePairedPull(&warrior);assert(calls.empty()&&!pair.pending);
reset();warlock.castOK=false;UpdatePairedPull(&warrior);assert(calls==std::vector<std::string>{"pain"});assert(sPlayerbotAIConfig.result=="cast-failed");
reset();shotOK=false;UpdatePairedPull(&warrior);assert(calls.size()==2 && sPlayerbotAIConfig.result=="cast-failed");
}
''')
    def test_wiring_and_linked_aggro_does_not_end_barrier(self):
        command=(ROOT/'commands/BotCommands.cpp').read_text()
        self.assertIn('ai::TwinsBeginPairedPull(ai,context.enemyTarget);',command)
        s=self.source();f=extract(s,'void ai::TwinsEnforceOpening(')
        self.assertLess(f.index('UpdatePairedPull(ai)'),f.index('OnPullActionCompleted'))
        self.assertIn('!PairWaiting(ai)',f)
        trigger=(ROOT/'ai/playerbot/strategy/triggers/PullTriggers.cpp').read_text()
        f=extract(trigger,'bool PullEndTrigger::IsActive(')
        self.assertLess(f.index('TwinsPairedPullPending(ai)'),f.index('target->GetVictim() == bot'))
        self.assertIn('PairCasting(ai)',extract(s,'bool TwinsPositionAction::Prepare('))
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--root',type=Path);args,rest=parser.parse_known_args()
    if args.root:ROOT=args.root
    unittest.main(argv=[__file__]+rest)
