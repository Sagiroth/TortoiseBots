"""Production Patchwerk rotation and shared opening-clock regression fixtures."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tools.test_patchwerk_soak_opening import function
from tools.test_patchwerk_pull_rendezvous import rendezvous_fixture
from tools.test_tank_stance_sunder import body


def clock_fixture(root):
    source=(root/'ai/playerbot/strategy/actions/PatchwerkSoakerActions.cpp').read_text()
    policy=root/'ai/playerbot/PatchwerkOpeningPolicy.h'
    fixed=policy.exists()
    header=policy.read_text().replace('#pragma once','') if fixed else '''
#include <cstdint>
namespace ai {struct PatchwerkOpeningWindow {void Observe(uint64_t,bool,bool,uint32_t){} bool Holds(uint32_t,uint32_t)const{return false;}};}
'''
    code=rendezvous_fixture(root).split('int main(){')[0]
    code=header+'\nusing ai::PatchwerkOpeningWindow; PatchwerkOpeningWindow sharedWindow;\nstruct SpellEntry{bool positive;};bool IsPositiveSpell(SpellEntry const* s){return s->positive;}\n'+code
    code=code.replace('bool IsFriendlyTo(Unit*,Unit* u)', 'SpellEntry* LookupSpellInfo(unsigned id){static SpellEntry p{true},n{false};return id==1?&p:id==2?&n:nullptr;}bool IsFriendlyTo(Unit*,Unit* u)')
    code=code.replace('using ObjectGuid=unsigned;', '''struct ObjectGuid {unsigned id;ObjectGuid(unsigned i=0):id(i){} operator unsigned() const{return id;}
 unsigned long long GetRawValue()const{return id;}void Clear(){id=0;}};''')
    code=code.replace('unsigned GetObjectGuid()', 'ObjectGuid GetObjectGuid()')
    code=code.replace('unsigned GetMapId(){', 'unsigned GetInstanceId(){return instance;}unsigned GetMapId(){')
    code=code.replace('T Get(){return T{};}', 'T Get(){if constexpr(std::is_same_v<T,PatchwerkOpeningWindow>)return sharedWindow;else return T{};}')
    code=code.replace('bool IsTank(Player*){', 'bool healer=false;bool IsHeal(Player*){return healer;}bool IsTank(Player*){')
    code=code.replace('struct {float sightDistance=60;}', 'struct {unsigned patchwerkOpeningDelay=4;float sightDistance=60;}')
    code+='''
unsigned clockMs=1000;
struct WorldTimer{static unsigned getMSTime(){return clockMs;}};
struct PatchwerkOpeningWindowValue{PlayerbotAI* ai;Player* bot;PatchwerkOpeningWindow window;ObjectGuid bossGuid;Group* group=nullptr;unsigned instance=0;PatchwerkOpeningWindow Calculate();};
struct PatchwerkOpeningOwnerValue{PlayerbotAI* ai;Player* bot;ObjectGuid Calculate();};
namespace ai{bool PatchwerkDpsOpeningHold(PlayerbotAI*);bool PatchwerkOpeningBlocksSpell(PlayerbotAI*,uint32,Unit*);}
'''
    if fixed:
        for sig in ['PatchwerkOpeningWindow PatchwerkOpeningWindowValue::Calculate()', 'ObjectGuid PatchwerkOpeningOwnerValue::Calculate()', 'bool ai::PatchwerkDpsOpeningHold(', 'bool ai::PatchwerkOpeningBlocksSpell(']:code+=function(source,sig)+'\n'
    else:
        code+='PatchwerkOpeningWindow PatchwerkOpeningWindowValue::Calculate(){return {};}\nObjectGuid PatchwerkOpeningOwnerValue::Calculate(){return {};}\nbool ai::PatchwerkDpsOpeningHold(PlayerbotAI*){return false;}\nbool ai::PatchwerkOpeningBlocksSpell(PlayerbotAI*,uint32,Unit*){return false;}\n'
    code+=r'''
int main(){
 Player main,dps;Unit boss;boss.guid=1;boss.entry=16028;main.guid=2;dps.guid=3;
 Group group{{&main,&dps}};main.group=dps.group=&group;
 PlayerbotAI ma;ma.bot=&main;ma.boss=&boss;ma.main=true;ma.soak=false;main.controller=&ma;
 PlayerbotAI da;da.bot=&dps;da.boss=&boss;da.main=da.soak=da.tank=false;dps.controller=&da;
 ma.GetAiObjectContext();da.GetAiObjectContext();da.context.order=2;
 PullStrategy pull;pull.ai=&ma;pull.target=&boss;ma.pull=&pull;
 PatchwerkOpeningWindowValue clock{&ma,&main};
 sharedWindow=clock.Calculate();
 assert(PatchwerkDpsOpeningHold(&da));
 assert(PatchwerkOpeningBlocksSpell(&da,2,&boss));
 assert(PatchwerkOpeningBlocksSpell(&da,2,&dps)); // self-centered harmful AoE
 assert(!PatchwerkOpeningBlocksSpell(&da,1,&boss)); // positive heals/buffs remain allowed
 Unit add;add.entry=16024;assert(!PatchwerkOpeningBlocksSpell(&da,2,&add));
 // no DPS face-pull during main's approach
 clockMs=9000;sharedWindow=clock.Calculate();assert(PatchwerkDpsOpeningHold(&da));
 boss.combat=true;main.combat=true;main.victim=&boss;
 sharedWindow=clock.Calculate();assert(PatchwerkDpsOpeningHold(&da));
 pull.active=false;pull.target=nullptr; // 510 consumes pull command, but not this clock
 clockMs=12999;sharedWindow=clock.Calculate();assert(PatchwerkDpsOpeningHold(&da));
 clockMs=13000;sharedWindow=clock.Calculate();assert(!PatchwerkDpsOpeningHold(&da));assert(!PatchwerkOpeningBlocksSpell(&da,2,&boss));
 // Late arrivals/attack spam/weak threat cannot restart a finished window.
 clockMs=18000;sharedWindow=clock.Calculate();assert(!PatchwerkDpsOpeningHold(&da));
 main.distance=200;sharedWindow=clock.Calculate();assert(!PatchwerkDpsOpeningHold(&da));
 main.distance=30;sharedWindow=clock.Calculate();assert(!PatchwerkDpsOpeningHold(&da));
 boss.combat=false;main.victim=nullptr;sharedWindow=clock.Calculate();assert(!PatchwerkDpsOpeningHold(&da));
 boss.combat=true;main.victim=&boss;clockMs=20000;sharedWindow=clock.Calculate();assert(PatchwerkDpsOpeningHold(&da));
 da.tank=true;assert(!PatchwerkDpsOpeningHold(&da));da.tank=false;
 da.healer=true;assert(!PatchwerkDpsOpeningHold(&da));da.healer=false;
 sPlayerbotAIConfig.patchwerkOpeningDelay=0;assert(!PatchwerkDpsOpeningHold(&da));sPlayerbotAIConfig.patchwerkOpeningDelay=4;
 da.bot->instance=2;assert(!PatchwerkDpsOpeningHold(&da));da.bot->instance=1;
 ma.main=false;assert(!PatchwerkDpsOpeningHold(&da));ma.main=true;
 ma.soak=true;assert(!PatchwerkDpsOpeningHold(&da));ma.soak=false;
 Group other{{&dps}};dps.group=&other;assert(!PatchwerkDpsOpeningHold(&da));dps.group=&group;
 // Owner selection needs explicit assignment; generic tanks do not enable it.
 PatchwerkOpeningOwnerValue owner{&da,&dps};assert(owner.Calculate()==2);ma.main=false;assert(owner.Calculate()==0);ma.main=true;
 // Timer arithmetic remains bounded across the millisecond counter wrap.
 PatchwerkOpeningWindow wrap;wrap.Observe(1,true,false,0xffffff00u);
 assert(wrap.Holds(100,4000));assert(!wrap.Holds(5000,4000));
}
'''
    return code


def rotation_fixture(root):
    source=(root/'ai/playerbot/strategy/actions/PatchwerkSoakerActions.cpp').read_text()
    trigger=(root/'ai/playerbot/strategy/warrior/WarriorTriggers.cpp').read_text()
    actions=(root/'ai/playerbot/strategy/warrior/WarriorActions.h').read_text()
    # Reuse the established tank fixture's doubles, but execute our exact
    # encounter helper and production trigger/action bodies against new cases.
    original=(root/'tools/test_tank_stance_sunder.py').read_text()
    code=original.split("cpp = r'''")[1].split("'''",1)[0].split('int main(')[0]
    code=code.replace('bool PatchwerkThreatTank(AI*, Unit*){return false;} // separately exercised by Patchwerk opening fixture','')
    code=code.replace('struct Unit {', 'struct Unit {unsigned entry=16028,map=533;bool combat=true;unsigned GetEntry(){return entry;}bool IsInCombat(){return combat;}bool IsInMap(Unit* u){return map==u->map;}')
    code=code.replace('int GetMapId(){return 1;}', 'int GetMapId(){return map;}')
    code=code.replace('struct AI {', 'enum class BotState{BOT_STATE_COMBAT};\nstruct AI {Unit* bot=nullptr;bool main=true,soak=false;Unit* GetBot(){return bot;}bool HasStrategy(std::string s,BotState){return s=="patchwerk main"?main:soak;}')
    code=code.replace('bool CanCastSpell(const char*,Unit*){return false;}', 'bool CanCastSpell(const char* n,Unit*){return std::string(n)=="revenge"?revenge:ready[3]&&usable[3]&&rage>=20;}bool revenge=false;')
    if 'CanCastSpell(const char*' not in code:
        code=code.replace('bool CanCastSpell(uint32', 'bool revenge=false;bool CanCastSpell(const char* n,Unit*){return std::string(n)=="revenge"?revenge:ready[3]&&usable[3]&&rage>=20;}bool CanCastSpell(uint32')
    insert=code.index('struct Facade')
    helper=function(source,'bool Has(').replace('PlayerbotAI','AI')
    if 'bool ai::PatchwerkThreatTank(' in source:helper+=function(source,'bool ai::PatchwerkThreatTank(').replace('ai::','').replace('PlayerbotAI','AI')
    else:helper+='bool PatchwerkThreatTank(AI*,Unit*){return false;}\n'
    code=code[:insert]+helper+code[insert:]
    replacements={'BATTLE':body(trigger,'bool BattleStanceTrigger::IsActive()'), 'DEFENSIVE':body(trigger,'bool DefensiveStanceTrigger::IsActive()'), 'BERSERKER':body(trigger,'bool BerserkerStanceTrigger::IsActive()'), 'SHIELD_BLOCK':body(trigger,'bool ShieldBlockTrigger::IsActive()'), 'SUNDER_TRIGGER':body(trigger,'bool SunderArmorDebuffTrigger::IsActive()'), 'SUNDER_ACTION':body(actions[actions.index('class CastSunderArmorAction'):],'virtual bool isUseful()')}
    # Unused stance-action declarations need only a valid expression.
    code=code.replace('BATTLE_ACTION','true').replace('BERSERKER_ACTION','true')
    for key,val in replacements.items():code=code.replace(key,val)
    slam=body(trigger,'bool PatchwerkShieldSlamTrigger::IsActive()') if 'bool PatchwerkShieldSlamTrigger::IsActive()' in trigger else '{return ai->rage>=40 && ai->CanCastSpell("shield slam",target);}'
    code+='struct SlamTrigger : BuffTrigger {using BuffTrigger::BuffTrigger;bool IsActive()'+slam+'};\n'
    code+=r'''
int main(){
 AI a;Unit bot,target;Aura five;five.stacks=5;five.duration=30000;
 a.bot=&bot;a.current=&target;a.tank=true;a.aura=&five;sServerFacade.ai=&a;
 SunderTrigger trigger(&a,&bot,&target);SunderAction filler(&a,&bot,&target);SlamTrigger slam(&a,&bot,&target);
 // Live gap: full shared stack, Slam on cooldown, no Revenge proc.
 a.rage=25;assert(trigger.IsActive());assert(filler.isUseful());
 // Actual-cost Slam (20 rage) takes priority even below the generic 40 reserve.
 a.ready[3]=true;assert(slam.IsActive());assert(!filler.isUseful());
 a.ready[3]=false;a.revenge=true;assert(!filler.isUseful());
 a.revenge=false;a.main=false;a.soak=true;assert(trigger.IsActive()&&filler.isUseful());
 a.ready[3]=true;assert(slam.IsActive());
 // No rotation changes outside the explicit encounter assignments.
 target.entry=11502;assert(!trigger.IsActive()&&!slam.IsActive());target.entry=16028;
 a.soak=false;assert(!trigger.IsActive()&&!slam.IsActive());a.soak=true;
 bot.map=1;assert(!trigger.IsActive()&&!slam.IsActive());bot.map=533;
 target.combat=false;assert(!trigger.IsActive()&&!slam.IsActive());target.combat=true;
 a.tank=false;assert(!trigger.IsActive()&&!slam.IsActive());
}
'''
    return code


class PatchwerkOpeningThreatTest(unittest.TestCase):
    def run_fixture(self,code):
        compiler=shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as d:
            src=Path(d)/'fixture.cpp';exe=Path(d)/'fixture';src.write_text(code)
            subprocess.run([compiler,'-std=c++17',str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
    def test_shared_window(self):
        self.run_fixture(clock_fixture(Path(os.environ.get('TBOTS_SOURCE_ROOT',Path(__file__).resolve().parents[1]))))
    def test_rotation_gap(self):
        self.run_fixture(rotation_fixture(Path(os.environ.get('TBOTS_SOURCE_ROOT',Path(__file__).resolve().parents[1]))))
    def test_damage_paths_and_config_wired(self):
        root=Path(__file__).resolve().parents[1]
        source=(root/'ai/playerbot/PlayerbotAI.cpp').read_text() + (root/'ai/playerbot/PlayerbotAISpells.cpp').read_text()
        self.assertIn('PatchwerkOpeningBlocksSpell(this, spellid, target)',source)
        self.assertIn('PatchwerkOpeningBlocksSpell(this, spellId, target)',source)
        self.assertIn('pet->AttackStop()',source)
        for file in ['ai/playerbot/strategy/actions/AttackAction.cpp','ai/playerbot/strategy/actions/ReachTargetActions.h']:
            self.assertIn('PatchwerkDpsOpeningHold(ai)',(root/file).read_text())
        for file in ['ai/playerbot/aiplayerbot.conf.dist.in','conf/tortoise_bots.conf.dist']:
            self.assertIn('AiPlayerbot.PatchwerkOpeningDelay = 4',(root/file).read_text())
        self.assertIn('"AiPlayerbot.PatchwerkOpeningDelay", 4',(root/'ai/playerbot/PlayerbotAIConfig.cpp').read_text())

if __name__=='__main__':unittest.main()
