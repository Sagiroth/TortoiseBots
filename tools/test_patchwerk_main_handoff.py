"""Run the production handoff with the soaker updating before a delayed main."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tools.test_patchwerk_pull_rendezvous import rendezvous_fixture
from tools.test_patchwerk_soak_opening import function


def handoff_fixture(root):
    source = (root/'ai/playerbot/strategy/actions/PatchwerkSoakerActions.cpp').read_text()
    code = rendezvous_fixture(root).split('int main(){')[0]
    code = code.replace('struct Unit {', '''struct Threat {float amount=0;float getThreat(void*){return amount;}};
struct Unit {
 Threat threat;bool repeat=true;Threat& GetThreatManager(){return threat;}
 void InterruptSpell(int){repeat=false;}
''')
    code = code.replace('struct Event{};', 'struct Event{Event(char const* = "") {}};')
    code = code.replace('struct PlayerbotAI {', 'void finishPull(PlayerbotAI*);\nstruct PlayerbotAI {')
    code = code.replace('void SetActionDuration(unsigned d)', 'void ClearActionClock(){duration=0;}\n void SetActionDuration(unsigned d)')
    code = code.replace('calls.push_back(s);if(s==', 'calls.push_back(s);if(s=="pull end"){finishPull(this);return true;}if(s==')
    code = code.replace('struct {float sightDistance=60;}', '''struct {float sightDistance=60;
 bool hasLog(char const*){return false;}
 void logEvent(PlayerbotAI*,char const*,ObjectGuid,char const*){}
}''')
    code += '''
constexpr int CURRENT_AUTOREPEAT_SPELL=1;
void finishPull(PlayerbotAI* ai){ai->pull->active=false;ai->pull->target=nullptr;ai->duration=1500;}
struct PatchwerkMainEngageAction : PatchwerkSoakEngageAction {
 PatchwerkMainEngageAction(PlayerbotAI* p):PatchwerkSoakEngageAction{p,p->bot,p->GetAiObjectContext()}{}
 bool Execute(Event&);
};
namespace ai {void WakePatchwerkPullMain(PlayerbotAI*);}
'''
    if 'bool PatchwerkMainEngageAction::Execute(' in source:
        code += function(source, 'bool PatchwerkMainEngageAction::Execute(')
        code += function(source, 'void ai::WakePatchwerkPullMain(')
    else:
        # Baseline has no early handoff: ordinary pull-end/rotation is still
        # asleep when 505 wakes the now-nearby soaker. The assertion must fail.
        code += 'bool PatchwerkMainEngageAction::Execute(Event&){return false;}\nvoid ai::WakePatchwerkPullMain(PlayerbotAI*){}\n'
    code += r'''
int main(){
 Player main,soak;Unit boss;boss.entry=16028;boss.guid=1;boss.combat=true;
 main.guid=2;soak.guid=3;Group group{{&main,&soak}};main.group=soak.group=&group;
 main.stance=soak.stance=true;main.melee=soak.melee=true;
 PlayerbotAI a;a.bot=&main;a.boss=&boss;a.main=true;a.soak=false;main.controller=&a;
 PlayerbotAI b;b.bot=&soak;b.boss=&boss;soak.controller=&b;
 a.GetAiObjectContext();b.GetAiObjectContext();
 PullStrategy pull;pull.ai=&a;pull.target=&boss;pull.cast=true;a.pull=&pull;
 boss.threat.amount=50;boss.victim=&main;main.attackers.push_back(&boss);
 a.duration=2000;Event event;
 // Live 11f: soaker gets its first update while the main is still delayed.
 WakePatchwerkPullMain(&b);
 PatchwerkSoakEngageAction soaker{&b,&soak,&b.context};assert(soaker.Execute(event));
 assert(soak.swings);
 assert(main.swings && main.victim==&boss && a.duration==0 && !main.repeat);
 assert(!pull.active); // normal cleanup, no repeated peer wake/GCD reset
 a.duration=1500;WakePatchwerkPullMain(&b);assert(a.duration==1500);
 // Main-updates-first path, outside melee: attack+chase, no idle action delay.
 auto reset=[&](){pull.active=true;pull.target=&boss;main.swings=false;main.victim=nullptr;
                  main.repeat=true;a.duration=2000;a.calls.clear();};
 reset();main.melee=false;PatchwerkMainEngageAction opening(&a);
 assert(opening.Execute(event));assert(main.swings && a.duration==0);
 assert(std::find(a.calls.begin(),a.calls.end(),"move")!=a.calls.end());
 // Combat from somebody else cannot cancel the main's unlanded arrow.
 reset();boss.threat.amount=0;assert(!opening.Execute(event));assert(main.repeat && !main.swings && a.duration==2000);
 boss.threat.amount=50;
 reset();pull.cast=false;assert(!opening.Execute(event));pull.cast=true;
 reset();boss.combat=false;assert(!opening.Execute(event));boss.combat=true;
 reset();pull.back=true;assert(!opening.Execute(event));pull.back=false;
 reset();pull.active=false;assert(!opening.Execute(event));
 reset();boss.entry=11502;assert(!opening.Execute(event));boss.entry=16028;
 reset();a.main=false;assert(!opening.Execute(event));a.main=true;
 reset();a.soak=true;assert(!opening.Execute(event));a.soak=false;
 reset();main.alive=false;assert(!opening.Execute(event));main.alive=true;
 reset();main.cc=true;assert(!opening.Execute(event));main.cc=false;
 reset();boss.evade=true;assert(!opening.Execute(event));boss.evade=false;
 reset();boss.instance=2;assert(!opening.Execute(event));boss.instance=1;
 reset();main.teleport=true;assert(!opening.Execute(event));main.teleport=false;
 reset();main.map=1;assert(!opening.Execute(event));main.map=533;
 reset();main.group=nullptr;assert(!opening.Execute(event));main.group=&group;
 reset();main.friendly=false;WakePatchwerkPullMain(&b);assert(!main.swings);main.friendly=true;
}
'''
    return code


class MainHandoffTest(unittest.TestCase):
    def test_soaker_updates_first_and_safety_gates(self):
        root=Path(os.environ.get('TBOTS_SOURCE_ROOT',Path(__file__).resolve().parents[1]))
        compiler=shutil.which('g++')
        self.assertIsNotNone(compiler, 'g++ required for main handoff fixture')
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'handoff.cpp';exe=Path(directory)/'handoff'
            source.write_text(handoff_fixture(root))
            subprocess.run([compiler,'-std=c++17',str(source),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_dispatch_precedes_soaker_attack(self):
        root=Path(os.environ.get('TBOTS_SOURCE_ROOT',Path(__file__).resolve().parents[1]))
        source=(root/'ai/playerbot/PlayerbotAI.cpp').read_text()
        wake=source.index('WakePatchwerkPullMain(this);')
        self.assertLess(wake,source.index('PatchwerkSoakEngageAction opening(this);'))
        self.assertLess(source.index('PatchwerkMainEngageAction opening(this);'),source.index('bool const soakStaging'))


if __name__=='__main__':unittest.main()
