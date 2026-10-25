"""Production pull gate and staging fixture; no world server needed."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tools.test_patchwerk_soak_opening import fixture, function


def rendezvous_fixture(root):
    source = (root/'ai/playerbot/strategy/actions/PatchwerkSoakerActions.cpp').read_text()
    pull = (root/'ai/playerbot/strategy/generic/PullStrategy.cpp').read_text()
    fixed = 'bool ai::PatchwerkSoakersReadyForPull(' in source
    code = fixture(root).split('int main(')[0]
    code = code.replace('struct PlayerbotAI;struct Group;', 'struct PlayerbotAI;struct Group;struct PullStrategy;')
    code = code.replace('unsigned entry=0,', 'PlayerbotAI* controller=nullptr;float x=0;bool moving=false;\n unsigned entry=0,')
    code = code.replace('bool IsCreature(){', '''bool IsPlayer(){return entry==0;}bool IsMoving(){return moving;}
 float GetDistance(Unit* u){return std::abs(x-u->x);}
 float getPositionX(){return x;}float getPositionY(){return 0;}float getPositionZ(){return 0;}
 bool IsCreature(){''')
    code = code.replace('struct FixtureValue{void Reset(){}};', 'struct FixtureValue{void Reset(){} T Get(){return T{};}};')
    code = code.replace('Player* bot;Unit* boss;bool soak=', 'PullStrategy* pull=nullptr;Player* bot;Unit* boss;bool soak=')
    code = code.replace('void SetActionDuration(unsigned d){duration=d;}', '''void SetActionDuration(unsigned d){duration=d;}
 unsigned GetAIInternalUpdateDelay(){return duration;}bool CanMove(){return !bot->cc;}
 void StopMoving(){bot->moving=false;calls.push_back("stop");}''')
    code = code.replace('bool IsFriendlyTo(Unit*,Unit* u)', 'bool IsSpellReady(Unit*,unsigned){return true;}bool IsFriendlyTo(Unit*,Unit* u)')
    code = code.replace('struct PatchwerkSoakBossValue{', 'struct PatchwerkSoakPullerValue{PlayerbotAI* ai;Player* bot;ObjectGuid Calculate();};\n struct PatchwerkSoakBossValue{')
    code = code.replace('Unit* GetUnit(ObjectGuid id){return id==1?boss:nullptr;}', '''Unit* GetUnit(ObjectGuid id){if(id==1)return boss;
 if(bot->group)for(auto* p:bot->group->members)if(p->guid==id)return p;return nullptr;}''')
    code = code.replace('if(n=="patchwerk soak boss")', '''if(n=="patchwerk soak puller"){PatchwerkSoakPullerValue v{c->ai,c->ai->bot};return v.Calculate();}
  if(n=="patchwerk soak boss")''') if fixed else code
    extra = r'''
#include <ctime>
unsigned nowSeconds=100;
time_t fakeTime(time_t*){return nowSeconds;}
#define time fakeTime
struct PullStrategy {
 PlayerbotAI* ai=nullptr;Unit* target=nullptr;bool active=true,back=false,started=true,completed=false,cast=false;
 bool bossPrepActed=false,bossWallChecked=false;time_t began=100;
 static PullStrategy* Get(PlayerbotAI* ai){return ai?ai->pull:nullptr;}
 Unit* GetTarget(){return target;}bool IsCommandActive(){return active;}bool IsCommandPullback(){return back;}
 bool HasPullStarted(){return started;}bool HasPullActionCompleted(){return completed;}
 bool HasPullActionCast(){return cast;}time_t GetPullStartTime(){return began;}unsigned GetMaxPullTime(){return 15;}
 bool IsProtectionBossCommand(){return false;}bool NeedsBossPullStance(){return false;}
 void TraceCommand(char const*,char const*,int){}
 bool PrepareBossPull(Event&,bool);
};
struct PlayerbotAIStorage {
 static PlayerbotAIStorage& Instance(){static PlayerbotAIStorage s;return s;}
 PlayerbotAI* GetAI(Player* p){return p->controller;}
};
namespace ai{Player* GetPatchwerkSoakPuller(PlayerbotAI*);bool PatchwerkSoakersReadyForPull(PlayerbotAI*);}
struct PatchwerkSoakStageAction {
 PlayerbotAI* ai;Player* bot;bool Execute(Event&);
 bool isPossible(){return ai->CanMove();}unsigned GetDuration(){return 9000;}
 bool MoveTo(unsigned,float x,float,float){ai->calls.push_back("stage move");bot->x=x;bot->moving=true;return true;}
};
'''
    code += extra
    if fixed: code = '#define RENDEZVOUS\n' + code
    if fixed:
        for sig in ['bool PatchwerkMainPull(', 'bool PullRendezvousExpired(',
                    'ObjectGuid PatchwerkSoakPullerValue::Calculate()', 'Player* ai::GetPatchwerkSoakPuller(',
                    'bool ai::PatchwerkSoakersReadyForPull(', 'bool PatchwerkSoakStageAction::Execute(']:
            code += function(source, sig) + '\n'
    code += function(pull, 'bool PullStrategy::PrepareBossPull(') + '\n'
    code += r'''
int main(){
 Player main,a,b,c;Unit boss;boss.entry=16028;boss.guid=1;
 main.guid=2;a.guid=3;b.guid=4;c.guid=5;a.x=b.x=c.x=-40;
 Group group{{&main,&a,&b,&c}};for(auto* p:group.members)p->group=&group;
 PlayerbotAI mainAI;mainAI.bot=&main;mainAI.boss=&boss;mainAI.main=true;mainAI.soak=false;main.controller=&mainAI;
 PlayerbotAI aa;aa.bot=&a;aa.boss=&boss;a.controller=&aa;
 PlayerbotAI bb;bb.bot=&b;bb.boss=&boss;b.controller=&bb;
 PlayerbotAI cc;cc.bot=&c;cc.boss=&boss;c.controller=&cc;
 for(auto* p:group.members)p->controller->GetAiObjectContext();
 PullStrategy pull;pull.ai=&mainAI;pull.target=&boss;mainAI.pull=&pull;Event event;
 // Recorded pattern: ready main, soakers still 40 yards behind. Old pull proceeds.
 assert(!pull.PrepareBossPull(event,false));
#ifdef RENDEZVOUS
 assert(!pull.PrepareBossPull(event,true)); // firing path is guarded too
 for(auto* peer:{&aa,&bb,&cc}){
  peer->stay=true;assert(GetPatchwerkSoakPuller(peer)==&main);
  PatchwerkSoakStageAction action{peer,peer->bot};
  assert(action.Execute(event));assert(peer->calls.back()=="defensive stance");
  peer->duration=1500;assert(!PatchwerkSoakersReadyForPull(&mainAI)); // still on stance GCD
  peer->duration=0;assert(action.Execute(event));assert(peer->bot->x==main.x);
  assert(!peer->bot->victim&&peer->stay); // point movement never arms an attack
 }
 assert(pull.PrepareBossPull(event,false));assert(pull.PrepareBossPull(event,true));
 main.x=10;assert(!pull.PrepareBossPull(event,true)); // main advanced to firing spot; catch up first
 for(auto* peer:{&aa,&bb,&cc}){PatchwerkSoakStageAction action{peer,peer->bot};assert(action.Execute(event));}
 assert(pull.PrepareBossPull(event,true));
 a.cc=true;assert(!pull.PrepareBossPull(event,true));a.cc=false;
 a.alive=false;assert(!pull.PrepareBossPull(event,true));a.alive=true;
 a.instance=2;assert(!pull.PrepareBossPull(event,true));a.instance=1;
 // Windup is never interrupted even if a companion becomes unready.
 pull.cast=true;a.x=-40;assert(pull.PrepareBossPull(event,true));pull.cast=false;a.x=main.x;
 // Exact command lifetime, no stale selection or cross-group following.
 pull.active=false;assert(!GetPatchwerkSoakPuller(&aa));pull.active=true;
 pull.back=true;assert(!GetPatchwerkSoakPuller(&aa));pull.back=false;
 pull.target=nullptr;assert(!GetPatchwerkSoakPuller(&aa));pull.target=&boss;
 boss.entry=15990;assert(!GetPatchwerkSoakPuller(&aa));boss.entry=16028;
 boss.evade=true;assert(!GetPatchwerkSoakPuller(&aa));boss.evade=false;
 main.alive=false;assert(!GetPatchwerkSoakPuller(&aa));main.alive=true;
 aa.soak=false;assert(!GetPatchwerkSoakPuller(&aa));aa.soak=true;
 nowSeconds=115;assert(!GetPatchwerkSoakPuller(&aa));assert(!pull.PrepareBossPull(event,true));nowSeconds=100;
 boss.combat=true;main.combat=true;main.victim=&boss;
 assert(!GetPatchwerkSoakPuller(&aa));assert(GetPatchwerkSoakBoss(&aa)==&boss); // 505 takes over
 assert(pull.PrepareBossPull(event,true));
 // No new DPS timer or orders: all three still have their original stay.
 assert(aa.stay&&bb.stay&&cc.stay);
#endif
}
'''
    return code


class PatchwerkPullRendezvousTest(unittest.TestCase):
    def test_real_pull_gate_and_staging(self):
        root=Path(os.environ.get('TBOTS_SOURCE_ROOT',Path(__file__).resolve().parents[1]))
        compiler=shutil.which('g++')
        if not compiler:self.skipTest('g++ unavailable')
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'rendezvous.cpp';exe=Path(directory)/'rendezvous'
            source.write_text(rendezvous_fixture(root))
            subprocess.run([compiler,'-std=c++17',str(source),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_staging_precedes_combat_opening_and_cancel_stops_movement(self):
        root=Path(__file__).resolve().parents[1]
        source=(root/'ai/playerbot/PlayerbotAI.cpp').read_text()
        stage=source[source.index('bool const soakStaging'):source.index('bool const soakActive')]
        self.assertIn('soakWasStaging && !soakStaging',stage)
        self.assertIn('StopMoving();',stage)
        self.assertIn('ResetAIInternalUpdateDelay();',stage)
        self.assertIn('CanUpdateAIInternal()',stage)
        self.assertNotIn('ChangeStrategy',stage)
        commands=(root/'commands/BotCommands.cpp').read_text()
        self.assertIn('probe->NeedsBossPullStance() || !ai::PatchwerkSoakersReadyForPull(ai)',commands)


if __name__=='__main__':unittest.main()
