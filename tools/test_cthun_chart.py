"""Fixed-chart live regression. --root permits executing the same fixture on 11j."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=Path(__file__).with_suffix('.cpp')
class ChartTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'),'g++ required')
    def test_chart(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
            cpp=Path(tmp)/'test.cpp';binary=Path(tmp)/'test'
            cpp.write_text(FIXTURE.read_text().replace('../ai/playerbot/CthunSpreadPolicy.h','playerbot/CthunSpreadPolicy.h'))
            flags=['-DCTHUN_CHART_EXTENDED'] if 'CthunChartSlot' in (ROOT/'ai/playerbot/CthunSpreadPolicy.h').read_text() else []
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-O2',*flags,'-I'+str(ROOT/'ai'),str(cpp),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
    def test_target_hooks_and_duty_exceptions(self):
        source=(ROOT/'ai/playerbot/strategy/values/DpsTargetValue.cpp').read_text()
        for name in ('DpsTargetValue','DpsAoeTargetValue'):
            body=source.split('Unit* '+name+'::Calculate()')[1].split('\n}')[0]
            self.assertIn('SelectCthunSpreadTarget(ai, sectorTarget)',body)
            self.assertLess(body.index('if (rti)'),body.index('SelectCthunSpreadTarget'))
        action=(ROOT/'ai/playerbot/strategy/actions/CthunSpreadActions.cpp').read_text()
        self.assertIn('if(ai->IsHeal(bot) || ai->IsTank(bot)) return false',action)
        self.assertIn('!boss->IsInCombat()',action)
        self.assertIn('!bot->IsWithinLOSInMap(unit)',action)
        self.assertIn('distance>=range.first',action)
        self.assertIn('current->GetEntry()==15728',action)
        self.assertNotIn('CthunRoute(',action)
        self.assertIn('CthunDistance(here,destination)<=kCthunArrival',action)
        self.assertIn('movespline->Finalized()',action)
        self.assertIn('bot->movespline->GetId()==lastSpline',action)
        prepare=action.split('bool CthunSpreadAction::Prepare()')[1].split('bool CthunSpreadAction::Execute')[0]
        self.assertNotIn('lastDestination=destination',prepare) # preparation cannot claim a running spline
        execute=action.split('bool CthunSpreadAction::Execute')[1].split('bool ai::SelectCthunSpreadTarget')[0]
        self.assertLess(execute.index('init.Launch()'),execute.index('lastSpline=bot->movespline->GetId()'))
        strategy=(ROOT/'ai/playerbot/strategy/generic/CthunSpreadStrategy.cpp').read_text()
        self.assertIn('new NextAction("dps assist",ACTION_HIGH+1)',strategy)
        reaction=strategy.split('void CthunSpreadStrategy::InitReactionTriggers')[1].split('\n}')[0]
        self.assertNotIn('dps assist',reaction) # no attack-driven cancellation of casts
    @unittest.skipUnless(shutil.which('g++'),'g++ required')
    def test_actual_runtime_selector(self):
        action=(ROOT/'ai/playerbot/strategy/actions/CthunSpreadActions.cpp').read_text()
        signature='bool ai::SelectCthunSpreadTarget('
        if signature not in action:
            self.fail('11j has no reachable local-add selection before tank assistance')
        actual=action[action.index(signature):]
        mock=r'''
#include "playerbot/CthunSpreadPolicy.h"
#include <cassert>
#include <list>
#include <string>
#include <map>
using namespace ai;
using ObjectGuid=unsigned;
enum {CLASS_HUNTER=3,CLASS_MAGE=8,CLASS_WARLOCK=9,CLASS_PRIEST=5,CLASS_SHAMAN=7,CLASS_DRUID=11};
struct Unit {
 unsigned id=0,entry=0,map=531;float x=0,y=0,z=100;
 bool combat=true,valid=true,immune=false,los=true,shell=false;
 unsigned GetGUIDLow(){return id;} unsigned getObjectGuid(){return id;}
 unsigned GetEntry(){return entry;} unsigned GetMapId(){return map;}
 float GetPositionX(){return x;} float GetPositionY(){return y;} float GetPositionZ(){return z;}
 bool IsInMap(Unit* p){return p->map==map;} bool IsInCombat(){return combat;}
 bool HasAura(unsigned){return shell;}
};
struct Group {unsigned sector=0;unsigned GetMemberGroup(unsigned){return sector;}};
struct Player:Unit {
 Group group;unsigned cls=CLASS_MAGE;
 Group* GetGroup(){return &group;} unsigned GetClass(){return cls;}
 bool CanReachWithMeleeAutoAttack(Unit* u){return GetCombatDistance(u)<5;}
 float GetCombatDistance(Unit* u){return std::hypot(x-u->x,y-u->y);}
 bool IsWithinLOSInMap(Unit* u){return u->los;}
};
struct AiObjectContext {Unit* current=nullptr;std::list<ObjectGuid> candidates;};
template<class T>T Value(AiObjectContext*,std::string);
template<> Unit* Value<Unit*>(AiObjectContext*c,std::string){return c->current;}
template<> std::list<ObjectGuid> Value<std::list<ObjectGuid>>(AiObjectContext*c,std::string){return c->candidates;}
#define AI_VALUE(type,name) Value<type>(context,name)
struct PlayerbotAI {
 Player bot;Unit boss;AiObjectContext context;std::map<unsigned,Unit*> units;
 bool enabled=true,heal=false,tank=false,ranged=true;
 Player* GetBot(){return &bot;} bool IsHeal(Player*){return heal;} bool IsTank(Player*){return tank;}
 bool IsRanged(Player*){return ranged;} AiObjectContext* GetAiObjectContext(){return &context;}
 Unit* GetUnit(unsigned id){return units[id];}
 bool GetSpellRange(std::string,float*max,float*min){*max=30;*min=bot.cls==CLASS_HUNTER?8:0;return true;}
};
namespace ai {bool SelectCthunSpreadTarget(PlayerbotAI*,Unit*&);}
bool CthunSpreadEnabled(PlayerbotAI*a){return a->enabled;}
Unit* Boss(PlayerbotAI*a){return &a->boss;}
CthunPoint Point(Unit*u){return {u->x,u->y};}
struct PossibleAttackTargetsValue {
 static bool IsValid(Unit*u,Player*,int,bool,bool){return u->valid;}
 static bool IsImmuneToDamage(Unit*u,Player*){return u->immune;}
};
''' + actual + r'''
int main(){
 PlayerbotAI a;auto p=CthunChartSlot(0,1);a.bot.x=p.x;a.bot.y=p.y;
 a.boss.id=1;a.boss.entry=15589;a.boss.x=kCthunCenter.x;a.boss.y=kCthunCenter.y;
 Unit eye;eye.id=2;eye.entry=15726;eye.x=kCthunEyes[0].x;eye.y=kCthunEyes[0].y;
 Unit claw=eye;claw.id=3;claw.entry=15725;
 a.context.candidates={1,2,3};a.context.current=&a.boss;
 a.units={{1,&a.boss},{2,&eye},{3,&claw}};
 Unit* target=nullptr;
 assert(ai::SelectCthunSpreadTarget(&a,target) && target==&eye); // unreachable tank boss -> local eye
 eye.los=false;assert(ai::SelectCthunSpreadTarget(&a,target) && target==&claw);
 claw.valid=false;assert(ai::SelectCthunSpreadTarget(&a,target) && !target); // no unreachable fallback
 eye.los=true;eye.z=-96;assert(ai::SelectCthunSpreadTarget(&a,target) && !target);
 eye.z=100;a.bot.cls=CLASS_HUNTER;eye.x=a.bot.x+2;eye.y=a.bot.y;
 assert(ai::SelectCthunSpreadTarget(&a,target) && !target); // dead zone
 a.bot.cls=CLASS_MAGE;assert(ai::SelectCthunSpreadTarget(&a,target) && target==&eye);
 a.heal=true;assert(!ai::SelectCthunSpreadTarget(&a,target));a.heal=false;
 a.tank=true;a.context.current=&claw;claw.entry=15728;
 assert(!ai::SelectCthunSpreadTarget(&a,target)); // no takeover of claw tank
 a.tank=false;a.boss.combat=false;assert(!ai::SelectCthunSpreadTarget(&a,target));
 a.boss.combat=true;a.enabled=false;assert(!ai::SelectCthunSpreadTarget(&a,target));
 a.enabled=true;a.ranged=false;eye.x=a.bot.x+20;
 assert(ai::SelectCthunSpreadTarget(&a,target) && !target); // extra melee waits
 eye.x=a.bot.x+3;assert(ai::SelectCthunSpreadTarget(&a,target) && target==&eye);
 eye.combat=false;assert(ai::SelectCthunSpreadTarget(&a,target) && !target);
}
'''
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
            cpp=Path(tmp)/'selector.cpp';binary=Path(tmp)/'selector';cpp.write_text(mock)
            subprocess.run(['g++','-std=c++17','-I'+str(ROOT/'ai'),str(cpp),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path);a,rest=p.parse_known_args()
    if a.root:ROOT=a.root.resolve()
    unittest.main(argv=[__file__]+rest)
