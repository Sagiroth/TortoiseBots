"""Run production diagnostic code with bounded world doubles; no game server."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class HealerDiagnosticsTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_observations_and_rate_limits(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / 'ai/playerbot/HealerDiagnostics.cpp').read_text()
        source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
        actions = (root / 'ai/playerbot/strategy/actions/GenericSpellActions.cpp').read_text()
        gate = actions[actions.index('bool CastSpellAction::isPossible()'):actions.index('bool CastSpellAction::isUseful()')]
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include "HealerDiagnostics.h"
using uint32 = std::uint32_t;
constexpr unsigned POWER_MANA=0;
enum SpellCastResult { SPELL_CAST_OK=0, SPELL_FAILED_OUT_OF_RANGE=1, SPELL_FAILED_LINE_OF_SIGHT, SPELL_FAILED_NO_POWER,
 SPELL_FAILED_SPELL_IN_PROGRESS, SPELL_FAILED_MOVING, SPELL_FAILED_BAD_TARGETS,
 SPELL_FAILED_NOT_IN_CONTROL, SPELL_FAILED_NOT_READY, SPELL_FAILED_NOT_KNOWN };
constexpr unsigned ATTACK_DISTANCE=5, SPELL_RANGE_IDX_COMBAT=1, SPELL_RANGE_IDX_SELF_ONLY=2, SPELL_RANGE_IDX_ANYWHERE=3;
enum class SizeFactor { CombatReach, CombatReachWithMelee };
struct WorldTimer { static uint32 now; static uint32 getMSTime() { return now; } };
uint32 WorldTimer::now=0;
struct Guid { unsigned n; unsigned GetCounter() const { return n; } };
struct Unit {
 bool alive=true, world=true, friendly=true, los=true;
 unsigned map=1, guid=1; float hp=100, distance=0;
 bool IsAlive() const { return alive; }
 bool IsInWorld() const { return world; }
 unsigned GetMap() const { return map; }
 float GetHealthPercent() const { return hp; }
 Guid GetObjectGuid() const { return {guid}; }
};
struct Player;
struct Group { std::vector<Player*> members; unsigned scans=0; };
struct MotionMaster {
 unsigned GetCurrentMovementGeneratorType() { return 2; }
 bool GetDestination(float& x, float& y, float& z) { x=10; y=20; z=30; return true; }
};
struct Player : Unit {
 MotionMaster motion;
 MotionMaster* GetMotionMaster() { return &motion; }
 Group* group=nullptr; bool healer=true, moving=false, casting=false;
 Group* GetGroup() { return group; }
 unsigned GetMapId() { return map; }
 unsigned GetInstanceId() { return 7; }
 unsigned GetPower(unsigned) { return 10; }
 unsigned GetMaxPower(unsigned) { return 100; }
 bool IsMoving() { return moving; }
 bool IsInCombat() { return true; }
 bool IsNonMeleeSpellCasted(bool) { return casting; }
 bool IsFriendlyTo(Unit* u) { return u->friendly; }
 float GetDistance(Unit* u) { return u->distance; }
 float GetDistance(Unit* u, SizeFactor) { return u->distance; }
 bool IsMounted() { return false; }
 bool CanReachWithMeleeAutoAttack(Unit* u) { return u->distance <= 5; }
 bool IsWithinLOSInMap(Unit* u) { return u->los; }
};
std::vector<Player*> const& LiveGroupMembers(Group* g) { ++g->scans; return g->members; }
namespace ai {
struct Action { std::string getName() { return "follow,\"master\"\n"; } };
struct Engine { Action action; Action const* GetLastExecutedAction() { return &action; } };
}
using ai::Action; using ai::Engine;
struct SpellEntry { bool heal; unsigned rangeIndex=0; };
struct Facade {
 SpellEntry heal{true}, damage{false};
 SpellEntry const* LookupSpellInfo(unsigned id) { return id==1 ? &heal : id==2 ? &damage : nullptr; }
} sServerFacade;
class PlayerbotAI {
public:
 Player* bot; Engine engine;
 ai::HealerDiagnostics diagnostics;
 bool possible=true; SpellCastResult result=SPELL_CAST_OK; unsigned probes=0;
 ai::HealerDiagnostics& GetHealerDiagnostics() { return diagnostics; }
 bool IsRanged(Player*) { return true; }
 void Unmount() {}
 bool GetSpellRange(std::string const&, float* max, float* min) { *max=40; *min=0; return true; }
 bool CanCastSpell(std::string const&, Unit*, unsigned, void*, bool ignoreRange,
                   bool ignoreCombat=false, bool ignoreMount=false, SpellCastResult* check=nullptr) {
  ++probes; assert(ignoreRange && !ignoreCombat && !ignoreMount);
  if(check) *check=result;
  return possible;
 }
 Player* GetBot() { return bot; }
 static bool IsHeal(Player* p) { return p->healer; }
 static bool IsHealSpell(SpellEntry const* s) { return s->heal; }
 float GetRange(char const*) { return 30; }
 Engine* GetCurrentEngine() { return &engine; }
};
struct Record { std::string event, reason, detail; };
struct Config {
 bool healerDiagnostics=true, allowed=true; unsigned almostFullHealth=90; float contactDistance=0;
 std::vector<Record> events;
 bool hasLog(char const*) { return allowed; }
 void logEvent(PlayerbotAI*, std::string e, std::string r, std::string d) {
  events.push_back({e,r,d});
 }
} sPlayerbotAIConfig;
''' + source + r'''
struct CastSpellAction {
 PlayerbotAI* ai; Player* bot; Unit* target;
 std::string spellName="heal"; unsigned spellId=1; float range=40;
 Unit* GetTarget() { return target; }
 std::string GetSpellName() { return spellName; }
 bool isPossible();
};
''' + gate + r'''
int main() {
 Player bot, injured, far, dead, elsewhere;
 injured.guid=2; injured.hp=35; injured.distance=20; injured.los=false;
 far.hp=20; far.distance=50; dead.hp=0; dead.alive=false;
 elsewhere.hp=10; elsewhere.map=2;
 Group g{{&bot,&injured,&far,&dead,&elsewhere}}; bot.group=&g;
 PlayerbotAI ai{&bot,{},{}}; ai::HealerDiagnostics diag;
 auto& events=sPlayerbotAIConfig.events;
 sPlayerbotAIConfig.healerDiagnostics=false;
 diag.Sample(&ai); diag.CheckFailed(&ai,&injured,1,SPELL_FAILED_NO_POWER);
 assert(events.empty() && g.scans==0);
 sPlayerbotAIConfig.healerDiagnostics=true; sPlayerbotAIConfig.allowed=false;
 diag.Sample(&ai); assert(events.empty() && g.scans==0);
 sPlayerbotAIConfig.allowed=true; bot.group=nullptr;
 diag.Sample(&ai); assert(events.empty()); bot.group=&g;
 diag.CheckFailed(&ai,&injured,2,SPELL_FAILED_NO_POWER); assert(events.empty());
 diag.CheckFailed(&ai,&injured,1,SPELL_FAILED_NO_POWER);
 assert(events.size()==1 && events.back().reason=="no-power");
 for(unsigned i=0;i<100;++i) diag.CheckFailed(&ai,&injured,1,SPELL_FAILED_OUT_OF_RANGE);
 assert(events.size()==1); // Changing reason does not evade the limit.
 diag.CastAttempt(&ai,&injured,1,true);
 diag.CastAttempt(&ai,&injured,1,false);
 diag.Sample(&ai);
 assert(events.size()==2 && events.back().event=="HealState" && g.scans==1);
 std::string text=events.back().detail;
 assert(text.find("injured=2 nearby=1 failed_checks=101 attempts=2 accepted=1")!=std::string::npos);
 assert(text.find("hp=35.0 distance=20.0 los=0")!=std::string::npos);
 assert(text.find('"')==std::string::npos && text.find(',')==std::string::npos && text.find('\n')==std::string::npos);
 for(unsigned t=1;t<10000;++t) { WorldTimer::now=t; diag.Sample(&ai); }
 assert(events.size()==2 && g.scans==1);
 WorldTimer::now=10000; diag.Sample(&ai);
 assert(events.size()==3 && g.scans==2);
 assert(events.back().detail.find("failed_checks=0 attempts=0 accepted=0")!=std::string::npos);
 diag.CheckFailed(&ai,nullptr,1,SPELL_FAILED_BAD_TARGETS);
 assert(events.size()==4 && events.back().reason=="target-invalid");
 WorldTimer::now=20000; diag.CastAttempt(&ai,&injured,1,false);
 assert(events.size()==5 && events.back().reason=="execute-refused");
 bot.healer=false; WorldTimer::now=30000; diag.Sample(&ai);
 diag.CheckFailed(&ai,&injured,1,SPELL_FAILED_NO_POWER);
 assert(events.size()==5 && g.scans==2);
 bot.healer=true; ai::HealerDiagnostics healthy;
 injured.hp=far.hp=100; healthy.Sample(&ai); assert(events.size()==5);
 bot.alive=false; diag.Sample(&ai); assert(events.size()==5);
 bot.alive=true; injured.hp=30; injured.distance=50; events.clear();
 CastSpellAction action{&ai,&bot,&injured};
 WorldTimer::now=40000;
 assert(!action.isPossible() && ai.probes==0 && events.back().reason=="out-of-range");
 WorldTimer::now=50000; injured.distance=20; ai.result=SPELL_FAILED_LINE_OF_SIGHT;
 ai.possible=true; assert(action.isPossible() && ai.probes==1 && events.size()==1);
 // CanCastSpell intentionally allows LOS/movement for prerequisites; don't falsely log them as rejected.
 ai.result=SPELL_FAILED_MOVING; assert(action.isPossible() && events.size()==1);
 ai.result=SPELL_FAILED_NO_POWER; ai.possible=false;
 assert(!action.isPossible() && events.size()==2 && events.back().reason=="no-power");
 WorldTimer::now=60000; action.target=nullptr;
 assert(!action.isPossible() && events.back().reason=="target-invalid");
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp = pathlib.Path(tmp) / 'test.cpp'
            exe = pathlib.Path(tmp) / 'test'
            cpp.write_text(fixture)
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-I',
                            str(root / 'ai/playerbot'), str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
