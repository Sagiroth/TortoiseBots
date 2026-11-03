"""Balance OOM regression: real trigger/multiplier code, narrow world stubs."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
DRUID = ROOT / 'ai/playerbot/strategy/druid'


def extract(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 0
    for end in range(brace, len(source)):
        depth += {'{': 1, '}': -1}.get(source[end], 0)
        if depth == 0:
            return source[start:end + 1]
    raise ValueError(signature)


def run_cpp(code):
    gxx = shutil.which('g++')
    if not gxx:
        raise unittest.SkipTest('g++ not available')
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / 'case.cpp'
        src.write_text(code)
        subprocess.run([gxx, '-std=c++17', str(src), '-o', str(Path(tmp) / 'case')], check=True)
        subprocess.run([str(Path(tmp) / 'case')], check=True)

class BalanceManaMeleeTest(unittest.TestCase):
    def test_live_wiring(self):
        strategy = (DRUID / 'BalanceDruidStrategy.cpp').read_text()
        context = (DRUID / 'DruidAiObjectContext.cpp').read_text()
        for trigger, action in [('balance mana depleted', 'switch to melee'),
                                ('balance mana recovered', 'switch to ranged'),
                                ('balance mana melee', 'melee')]:
            self.assertIn('new TriggerNode("' + trigger + '"', strategy)
            self.assertIn('new NextAction("' + action + '"', strategy)
            self.assertIn('creators["' + trigger + '"]', context)
        self.assertIn('new BalanceManaMultiplier(ai)', strategy)
        self.assertIn('creators["oom melee"]', context)
        base = extract(strategy, 'void BalanceDruidStrategy::InitCombatTriggers')
        self.assertNotIn('balance mana', base)
        factory = (ROOT / 'ai/playerbot/AiFactory.cpp').read_text()
        self.assertIn('"balance", "dps assist", "flee", "ranged", "oom melee"', factory)
        self.assertIn('"balance", "ranged", "oom melee"', factory)
        # These are the actual action names, distinct from their registry keys.
        actions = (ROOT / 'ai/playerbot/strategy/actions/CombatActions.h').read_text()
        self.assertIn('"-ranged,+close"', actions)
        self.assertIn('"-close,+ranged"', actions)
        melee = (ROOT / 'ai/playerbot/strategy/generic/MeleeCombatStrategy.cpp').read_text()
        self.assertIn('"reach melee"', melee)
        attack = (ROOT / 'ai/playerbot/strategy/actions/CombatActions.cpp').read_text()
        self.assertIn('bot->Attack(target, true)', attack)

    def test_mana_cycle_cast_and_feral_guards(self):
        header = '\n'.join(line for line in (DRUID / 'BalanceDruidMana.h').read_text().splitlines()
                           if not line.startswith(('#include', '#pragma')))
        run_cpp(r'''
#include <cassert>
#include <string>
#include <set>
enum { FORM_NONE, FORM_MOONKIN, FORM_BEAR, FORM_CAT, FORM_DIREBEAR, POWER_MANA };
enum class BotState { BOT_STATE_COMBAT, BOT_STATE_NON_COMBAT };
struct Player {
 unsigned mana=0, max=100, form=FORM_NONE; bool casting=false, stopped=false;
 void AttackStop(bool){stopped=true;}
 unsigned GetPower(int){return mana;} unsigned GetMaxPower(int){return max;}
 unsigned GetShapeshiftForm(){return form;} bool IsNonMeleeSpellCasted(bool){return casting;}
};
struct PlayerbotAI {
 Player player; std::set<std::string> strategies{"ranged","balance","oom melee"};
 Player* GetBot(){return &player;}
 bool HasStrategy(const std::string& n,BotState){return strategies.count(n);}
 void ChangeStrategy(const std::string& text,BotState){
  assert(text=="-close,+ranged"); strategies.erase("close"); strategies.insert("ranged");
 }
};
namespace ai {
struct Trigger {PlayerbotAI* ai; Player* bot;
 Trigger(PlayerbotAI*a,const char*):ai(a),bot(a->GetBot()){} virtual bool IsActive(){return false;}};
struct Action {std::string name;std::string getName(){return name;}};
struct Multiplier {PlayerbotAI*ai;Player*bot;
 Multiplier(PlayerbotAI*a,const char*):ai(a),bot(a->GetBot()){} virtual float GetValue(Action*){return 1;}};
}
''' + header + r'''
namespace ai {struct OomMeleeStrategy { PlayerbotAI* ai;
 void OnStrategyRemoved(BotState);};}
using namespace ai;
''' + extract((DRUID / 'BalanceDruidStrategy.cpp').read_text(), 'void OomMeleeStrategy::OnStrategyRemoved') + r'''
int main(){
 using namespace ai;
 // The level 10+ balance kit uses this same path; no form spell is required.
 {
  PlayerbotAI a; auto& p=a.player;
  BalanceManaTrigger enter(&a,BalanceManaTrigger::ENTER_MELEE),
    leave(&a,BalanceManaTrigger::RETURN_RANGED), swing(&a,BalanceManaTrigger::KEEP_MELEE);
  BalanceManaMultiplier weight(&a);
  p.mana=6; assert(!enter.IsActive());
  p.mana=5; assert(enter.IsActive());
  p.casting=true; assert(!enter.IsActive());
  Action toMelee{"-ranged,+close"}, toRange{"-close,+ranged"}, melee{"melee"};
  assert(weight.GetValue(&toMelee)==0 && weight.GetValue(&toRange)==0);
  p.casting=false; a.strategies={"balance","oom melee","close"};
  for (unsigned mana : {0u,5u,6u,15u,29u}) {
   p.mana=mana; assert(!enter.IsActive() && !leave.IsActive() && swing.IsActive());
   for (const char* n : {"wrath","starfire","moonfire","insect swarm","hurricane","faerie fire"}) {
    Action spell{n}; assert(weight.GetValue(&spell)==0);
   }
   for (const char* n : {"melee","reach melee","healing touch","regrowth","innervate","flee","remove curse"}) {
    Action action{n}; assert(weight.GetValue(&action)==1);
   }
  }
  p.mana=30; assert(leave.IsActive() && !swing.IsActive());
  p.casting=true; assert(!leave.IsActive() && !swing.IsActive());
  assert(weight.GetValue(&melee)==0);p.casting=false;
  a.strategies={"balance","oom melee","ranged"};Action wrath{"wrath"};assert(weight.GetValue(&wrath)==1);
  assert(!enter.IsActive() && !leave.IsActive());
  p.mana=0; for(unsigned form : {FORM_BEAR,FORM_DIREBEAR,FORM_CAT}) {
   p.form=form;assert(!enter.IsActive());a.strategies={"balance","oom melee","close"};
   assert(!swing.IsActive() && weight.GetValue(&wrath)==1);a.strategies={"balance","oom melee","ranged"};
  }
  p.form=FORM_MOONKIN;assert(enter.IsActive());p.form=FORM_NONE;
  for(const char* n : {"tank feral","dps feral"}) {
   a.strategies.insert(n);assert(!enter.IsActive());a.strategies.erase(n);
  }
  p.max=0;assert(!enter.IsActive());p.max=100;
  a.strategies={"balance","oom melee","close"};
  OomMeleeStrategy strategy{&a};
  strategy.OnStrategyRemoved(BotState::BOT_STATE_NON_COMBAT);
  assert(a.strategies.count("close") && !p.stopped);
  strategy.OnStrategyRemoved(BotState::BOT_STATE_COMBAT);
  a.strategies.erase("oom melee"); // Engine erases after the callback.
  assert(!a.strategies.count("close") && a.strategies.count("ranged") && p.stopped);
  assert(!enter.IsActive() && !leave.IsActive() && !swing.IsActive());
  assert(weight.GetValue(&wrath)==1);
  a.strategies.insert("oom melee");assert(enter.IsActive());
  a.strategies.erase("balance");assert(!enter.IsActive());
 }
}
''')

if __name__ == '__main__':
    unittest.main()
