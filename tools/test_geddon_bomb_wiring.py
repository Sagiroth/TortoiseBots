"""Execute the production command parser and bomb action arbitration."""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class BombWiring(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_parked_melee_and_parser(self):
        root = pathlib.Path(os.environ.get('TBOTS_TEST_SOURCE', pathlib.Path(__file__).resolve().parents[1]))
        source = (root/'ai/playerbot/strategy/generic/DungeonMultipliers.cpp').read_text()
        command = (root/'commands/BotCommands.cpp').read_text()
        policy = pathlib.Path(__file__).resolve().parents[1]/'ai/playerbot/GeddonBombPolicy.h'
        code = r'''
#include <cassert>
#include <cctype>
#include <string>
#include <iostream>
'''+f'#include "{policy}"\n#include "{root}/ai/playerbot/PullOpeningPolicy.h"\n'+r'''
using namespace ai;
struct Action { std::string name; Action(std::string n):name(n){} virtual ~Action(){} std::string getName(){return name;} };
struct MovementAction:Action{using Action::Action;};
struct CastReachTargetSpellAction:Action{using Action::Action;};
struct Player { unsigned map=409;unsigned GetMapId(){return map;} };
struct PlayerbotAI { bool armed=true,bomb=true,oldAllowed=false;GeddonBombState state;Player bot;Player* GetBot(){return &bot;} };
bool UpdateGeddonBombMovement(PlayerbotAI* ai){return ai->armed && ai->state.Active();}
bool GetRaidBombAura(PlayerbotAI* ai){return ai->bomb;}
bool RaidBombMovementAllowed(PlayerbotAI* ai){return ai->oldAllowed;}
#define AI_VALUE2(T,N,Q) false
struct RaidBombMovementMultiplier{PlayerbotAI* ai;float GetValue(Action*);};
std::string Trim(std::string s){auto a=s.find_first_not_of(" \t");return a==std::string::npos?"":s.substr(a,s.find_last_not_of(" \t")-a+1);}
bool IsRaidTargetMark(std::string const& s){return s=="skull";}
'''
        code += function(source, 'float RaidBombMovementMultiplier::GetValue(')
        code += '\n'+function(command, 'static std::string PullOptionText(')
        code += '\n'+function(command, 'static bool ParseAction(')
        code += r'''
int main(){
 PlayerbotAI ai;ai.state.Arm({0,30,0},1,2);ai.state.Observe(true,1,{4,0,0},0);
 RaidBombMovementMultiplier guard{&ai};Action melee("melee"),cast("fireball"),move("geddon bomb move");MovementAction stay("stay"),reach("reach melee");
 // Reproduces a parked melee carrier: the old movement permission is false.
 // Before the fix, melee/stay/reach all keep relevance 1 and compete with escape.
 assert(guard.GetValue(&melee)==0);assert(guard.GetValue(&cast)==0);
 assert(guard.GetValue(&stay)==0);assert(guard.GetValue(&reach)==0);assert(guard.GetValue(&move)==1);
 ai.state.Observe(true,1,{0,30,0},7000);assert(guard.GetValue(&melee)==0);
 ai.bomb=false;ai.state.Observe(false,0,{0,30,0},8100);assert(guard.GetValue(&stay)==0);
 ai.state.Observe(false,0,{4,0,0},10000);assert(guard.GetValue(&melee)==1);
 ai.armed=false;ai.bomb=true;assert(guard.GetValue(&stay)==1); // untouched opt-out
 std::string intent,option;
 assert(ParseAction("bombspot",intent,option)&&intent=="bombspot");
 assert(ParseAction(" BOMBsPoT STATUS ",intent,option)&&intent=="bombspot status");
 assert(ParseAction("bombspot clear",intent,option)&&intent=="bombspot clear");
 assert(!ParseAction("bombspot somewhere",intent,option));
 assert(!ParseAction("bombspot clear extra",intent,option));
 assert(ParseAction("attack",intent,option)&&intent=="attack");
 std::cout<<"Production bomb arbitration and command parser: PASS\n";
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp = pathlib.Path(tmp)/'fixture.cpp'
            exe = pathlib.Path(tmp)/'fixture'
            cpp.write_text(code)
            subprocess.run(['g++', '-std=c++17', str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
