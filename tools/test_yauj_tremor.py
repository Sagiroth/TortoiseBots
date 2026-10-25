"""Exercise the production Tremor trigger with Yauj opt-in and control gates."""
import ast
import pathlib
import shutil
import subprocess
import tempfile
import unittest
from tools.test_nefarian_bone_target import function

class YaujTremorTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_opt_in_yauj(self):
        root=pathlib.Path(__file__).resolve().parents[1]
        # Reuse the same small world boundary as the active-wiring fixture.
        tree=ast.parse((root/'tools/test_raid_tremor_wiring.py').read_text())
        method=next(n for n in ast.walk(tree) if isinstance(n,ast.FunctionDef) and n.name=='test_trigger_respects_spell_state_and_manual_totems')
        fixture=next(n.value.value for n in ast.walk(method) if isinstance(n,ast.Assign) and isinstance(n.value,ast.Constant) and any(isinstance(t,ast.Name) and t.id=='fixture' for t in n.targets))
        production=function((root/'ai/playerbot/strategy/shaman/ShamanTriggers.h').read_text(),'class RaidFearTremorTotemTrigger')+';\n'
        checks=r'''
int main(){PlayerbotAI botAI;RaidFearTremorTotemTrigger trigger(&botAI);
 botAI.player.map=531;unit.entry=15543;assert(!trigger.IsActive());
 botAI.strategies={"yauj fear"};assert(trigger.IsActive());
 botAI.player.map=409;assert(!trigger.IsActive());botAI.player.map=531;
 unit.entry=15511;assert(!trigger.IsActive());unit.entry=15543;
 hasTotem=true;assert(!trigger.IsActive());hasTotem=false;
 spellId=0;assert(!trigger.IsActive());spellId=1;
 ready=false;assert(!trigger.IsActive());ready=true;
 for(auto manual:{"totem earth stoneclaw","totem earth stoneskin","totem earth earthbind","totem earth strength"}){
  botAI.strategies={"yauj fear",manual};assert(!trigger.IsActive());
 }
 botAI.strategies={"yauj fear","totem earth tremor"};assert(trigger.IsActive());
 target=nullptr;assert(!trigger.IsActive());target=&unit;
 botAI.strategies.clear();assert(!trigger.IsActive());
 // Existing raids still work without the new opt-in.
 botAI.player.map=409;unit.entry=11982;assert(trigger.IsActive());
}
'''
        with tempfile.TemporaryDirectory() as directory:
            p=pathlib.Path(directory);source=p/'fixture.cpp';binary=p/'fixture'
            source.write_text(fixture+production+checks)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-I',str(root),str(source),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
