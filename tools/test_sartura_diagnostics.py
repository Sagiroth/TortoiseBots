"""Execute the production diagnostic sink against a bounded world/log boundary."""
import ast
import pathlib
import shutil
import subprocess
import tempfile
import unittest
from tools.test_nefarian_bone_target import function

ROOT = pathlib.Path(__file__).resolve().parents[1]

class SarturaDiagnosticsTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_rows_scope_and_shared_throttle(self):
        tree = ast.parse((ROOT / 'tools/test_sartura_whirlwind.py').read_text())
        fixture = next(n.value.value for n in ast.walk(tree) if isinstance(n, ast.Assign)
                       and any(isinstance(t, ast.Name) and t.id == 'fixture' for t in n.targets))
        fixture = fixture.replace('void LogSarturaEscape(PlayerbotAI*, char const*, std::string const&) {}', '')
        fixture = fixture.replace('struct AiObjectContext {', 'struct Action;\nstruct AiObjectContext {\n    Action* action=nullptr;\n    Action* GetAction(char const*) { return action; }')
        fixture = fixture.replace('struct MoveAwayFromHazard {', 'struct MoveAwayFromHazard : Action {')
        fixture = fixture.replace('explicit MoveAwayFromHazard(PlayerbotAI* a):ai(a)', 'explicit MoveAwayFromHazard(PlayerbotAI* a):Action("escape"),ai(a)')
        fixture = fixture.replace('bool Execute(Event&) override;', 'bool Execute(Event&) override { return true; }\n        SarturaDiagnosticThrottle diagnosticThrottle;')
        fixture = fixture.replace('namespace ai {\n    std::list', 'namespace ai {\n    void LogSarturaEscape(PlayerbotAI*, char const*, std::string const&, bool=false, WorldPosition const*=nullptr);\n    std::list').replace('const*=','const* =')
        fixture = fixture.replace('float distance(WorldPosition const& b)', 'float getX() const { return x; } float getY() const { return y; } float getZ() const { return z; }\n    float distance(WorldPosition const& b)')
        fixture += r'''
#include <vector>
struct Config {
 bool raidBossDiagnostics=true;
 std::vector<std::pair<std::string,std::string>> rows;
 void logEvent(PlayerbotAI*, std::string event, std::string, std::string row) { rows.emplace_back(event,row); }
} sPlayerbotAIConfig;
struct WorldTimer { static uint32 now; static uint32 getMSTime(){return now;} };
uint32 WorldTimer::now=0;
'''
        code = '#include "ai/playerbot/SarturaDiagnosticPolicy.h"\nusing namespace ai;\n' + fixture
        source = (ROOT / 'ai/playerbot/strategy/actions/SarturaWhirlwindActions.cpp').read_text()
        code += function(source, 'void ai::LogSarturaEscape(')
        code += r'''
int main() {
 PlayerbotAI ai; Unit boss; boss.entry=15516; boss.auras={26083};
 ai.units[1]=&boss; ai.context.possible={1}; ai.strategies={"sartura whirlwind"};
 SarturaWhirlwindEscapeAction escape(&ai); ai.context.action=&escape;
 sSpellTemplate.rows[26083].EffectTriggerSpell[0]=26084;
 sSpellTemplate.rows[26084].EffectRadiusIndex[0]=13; sSpellRadiusStore.rows[13].Radius=10;
 WorldPosition dest(&boss); dest.x=15;
 sPlayerbotAIConfig.raidBossDiagnostics=false;
 LogSarturaEscape(&ai,"SarturaEscape","moved",true,&dest);
 assert(sPlayerbotAIConfig.rows.empty() && ai.context.listReads==0);
 sPlayerbotAIConfig.raidBossDiagnostics=true;
 ai.bot.map=1; LogSarturaEscape(&ai,"SarturaEscapeBlocked","stay");
 ai.bot.map=531; ai.bot.alive=false; LogSarturaEscape(&ai,"SarturaEscapeBlocked","stay");
 ai.bot.alive=true; ai.bot.teleport=true; LogSarturaEscape(&ai,"SarturaEscapeBlocked","stay");
 ai.bot.teleport=false; ai.bot.combat=false; LogSarturaEscape(&ai,"SarturaEscapeBlocked","stay");
 ai.bot.combat=true; ai.strategies.clear(); LogSarturaEscape(&ai,"SarturaEscapeBlocked","stay");
 assert(sPlayerbotAIConfig.rows.empty() && ai.context.listReads==0);
 ai.strategies={"sartura whirlwind"};
 LogSarturaEscape(&ai,"SarturaEscape","moved",true,&dest);
 assert(sPlayerbotAIConfig.rows.size()==1);
 assert(sPlayerbotAIConfig.rows.back().first=="SarturaEscape");
 assert(sPlayerbotAIConfig.rows.back().second=="entry=15516|aura=26083|distance=0|reason=moved|success=1|destination=15,0,0");
 for (auto reason:{"stay","follow","wait-for-attack","pull-hold","multiplier:test","no-safe-point","movement-failed"}) {
  WorldTimer::now+=1999; auto count=sPlayerbotAIConfig.rows.size();
  LogSarturaEscape(&ai,"SarturaEscapeBlocked",reason); assert(sPlayerbotAIConfig.rows.size()==count);
  ++WorldTimer::now; LogSarturaEscape(&ai,"SarturaEscapeBlocked",reason);
  assert(sPlayerbotAIConfig.rows.size()==count+1);
  assert(sPlayerbotAIConfig.rows.back().first=="SarturaEscapeBlocked");
  assert(sPlayerbotAIConfig.rows.back().second.find(std::string("reason=")+reason+"|success=0|destination=none")!=std::string::npos);
 }
 boss.auras.clear(); WorldTimer::now+=2000; auto count=sPlayerbotAIConfig.rows.size();
 LogSarturaEscape(&ai,"SarturaEscapeBlocked","stay"); assert(sPlayerbotAIConfig.rows.size()==count);
 boss.entry=15984;boss.auras={26038};sSpellTemplate.rows[26038].EffectTriggerSpell[0]=26084;
 WorldTimer::now+=2000;LogSarturaEscape(&ai,"SarturaEscapeBlocked","stay");
 assert(sPlayerbotAIConfig.rows.back().second.find("entry=15984|aura=26038")==0);
 SarturaDiagnosticThrottle rollover; rollover.Mark(0xffffff00u);
 assert(!rollover.Due(0x10u)); assert(rollover.Due(0x700u));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp=pathlib.Path(tmp)/'fixture.cpp'; exe=pathlib.Path(tmp)/'fixture'
            cpp.write_text(code)
            subprocess.run(['g++','-std=c++17','-I',str(ROOT),str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_hooks(self):
        root=ROOT/'ai/playerbot/strategy'
        for file in ('Engine.cpp','ReactionEngine.cpp'):
            text=(root/file).read_text()
            self.assertIn('"multiplier:" +',text)
            self.assertIn('"not-possible"',text)
            self.assertIn('"sartura whirlwind escape"',text)
        text=(root/'actions/DungeonActions.cpp').read_text()
        for marker in ('"SarturaEscape", "moved", true, &point','"movement-failed"','"no-safe-point"'):
            self.assertIn(marker,text)
