"""Compile production CC gates and target precedence; baseline reproduces refusals."""
from pathlib import Path
import os, shutil, subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path(os.environ.get('VISCIDUS_SOURCE',ROOT))
def block(text, marker):
    start=text.index(marker);i=text.index('{',start)+1;depth=1
    while depth:
        depth+=(text[i]=='{')-(text[i]=='}');i+=1
    return text[start:i]
def run(code):
    with tempfile.TemporaryDirectory() as d:
        cpp=Path(d)/'t.cpp';exe=Path(d)/'t';cpp.write_text(code)
        subprocess.run(['g++','-std=c++17','-I'+str(ROOT),str(cpp),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
@unittest.skipUnless(shutil.which('g++'),'requires compiler')
class ViscidusCycleTest(unittest.TestCase):
    def test_frozen_boss_is_not_preserved_cc(self):
        source=(SOURCE/'ai/playerbot/strategy/values/PossibleAttackTargetsValue.cpp').read_text()
        code=r'''
#include <cassert>
#include <string>
#include "ai/playerbot/ViscidusPolicy.h"
using namespace ai;
constexpr unsigned UNIT_STAT_STUNNED=1;
struct Unit {unsigned entry=15299;bool frozen=true,stunned=true,poly=false,feared=false,rooted=false;
 bool IsPolymorphed(){return poly;}bool HasUnitState(unsigned){return stunned;}};
struct Player{unsigned map=531;bool opted=true;};
struct PlayerbotAI{bool HasAura(const char*,Unit*){return false;}};
struct PlayerbotAIStorage{static PlayerbotAIStorage& Instance(){static PlayerbotAIStorage s;return s;}
 PlayerbotAI* GetAI(Player*){return nullptr;}};
struct Facade{bool IsFrozen(Unit* u){return u->frozen;}bool IsFeared(Unit* u){return u->feared;}
 bool IsInRoots(Unit* u){return u->rooted;}}sServerFacade;
bool IsViscidusShatterTarget(Player* p,Unit* u){return ViscidusShatterWindow(p->map,u->entry,p->opted,true,true,u->frozen);}
struct PossibleAttackTargetsValue {static bool HasBreakableCC(Unit*,Player*);static bool HasUnBreakableCC(Unit*,Player*);};
'''
        for name in ('HasBreakableCC','HasUnBreakableCC'):
            code+=block(source,'bool PossibleAttackTargetsValue::'+name+'(')+'\n'
        code+=r'''
int main(){Unit boss;Player bot;
 assert(!PossibleAttackTargetsValue::HasBreakableCC(&boss,&bot));
 assert(!PossibleAttackTargetsValue::HasUnBreakableCC(&boss,&bot));
 bot.opted=false;assert(PossibleAttackTargetsValue::HasUnBreakableCC(&boss,&bot));
 bot.opted=true;boss.entry=1;assert(PossibleAttackTargetsValue::HasUnBreakableCC(&boss,&bot));
 boss.entry=15299;bot.map=0;assert(PossibleAttackTargetsValue::HasBreakableCC(&boss,&bot));}
'''
        run(code)
    def test_globs_outrank_stale_boss_orders(self):
        source=(SOURCE/'ai/playerbot/strategy/values/DpsTargetValue.cpp').read_text()
        body=source.split('Unit* DpsTargetValue::Calculate()\n{',1)[1]
        # Only the Viscidus pick and the explicit-order fallback it must outrank.
        prefix=body.split('        return explicitTarget;\n',1)[0]+'        return explicitTarget;\n'
        run(r'''
#include <cassert>
struct Unit{unsigned entry;unsigned GetEntry(){return entry;}}boss{15299},glob{15667};
Unit* GetViscidusCombatTarget(void*){return &glob;}
Unit* GetFankrissDpsTarget(void*){return nullptr;}Unit* GetFankrissSpawnTarget(void*){return nullptr;}
Unit* GetExplicitAttackTarget(){return &boss;}
Unit* Pick(){void* ai=nullptr;
'''+prefix+r'''
return nullptr;}
int main(){assert(Pick()==&glob);}
''')
    def test_clouds_use_real_dynamic_radius_and_warning(self):
        path=SOURCE/'ai/playerbot/strategy/actions/ViscidusActions.cpp'
        if not path.exists():
            self.fail('baseline has no Viscidus toxin hazard sensor; persistent effect 27 is excluded by avoid-aoe')
        method=block(path.read_text(),'std::list<HazardPosition> ViscidusCombatAction::Clouds()')
        run(r'''
#include <cassert>
#include <list>
#include <vector>
#include <utility>
using uint32=unsigned;using ObjectGuid=unsigned;constexpr unsigned EFFECT_INDEX_0=0;
struct Player{};struct DynamicObject {bool live=true;unsigned duration=10000;float radius=8;
 bool IsInWorld(){return live;}bool IsInMap(Player*){return true;}unsigned GetDuration(){return duration;}float GetRadius(){return radius;}};
struct Unit{bool live=true;DynamicObject* cloud=nullptr;DynamicObject* GetDynObject(unsigned id){assert(id==25989);return cloud;}};
struct WorldPosition{void* source;template<class T>WorldPosition(T* p):source(p){}};
using HazardPosition=std::pair<WorldPosition,float>;
struct SpellEntry{unsigned EffectRadiusIndex[1]={8};};struct SpellRadiusEntry{float Radius=8;};
struct Store{template<class T>T* LookupEntry(unsigned){static T t;return &t;}}sSpellTemplate;
struct RadiusStore{SpellRadiusEntry* LookupEntry(unsigned){static SpellRadiusEntry r;return &r;}}sSpellRadiusStore;
struct AI{Unit* unit;Unit* GetUnit(ObjectGuid){return unit;}};
bool Live(Player*,Unit* u){return u&&u->live;}
struct ViscidusCombatAction{Player* bot;AI* ai;std::vector<ObjectGuid> clouds{1};void Refresh(){}std::list<HazardPosition> Clouds();};
'''+method+r'''
int main(){Player bot;Unit trigger;AI ai{&trigger};ViscidusCombatAction a{&bot,&ai};
 auto warning=a.Clouds();assert(warning.size()==1&&warning.front().second==10);
 DynamicObject cloud;cloud.radius=11;trigger.cloud=&cloud;auto live=a.Clouds();
 assert(live.front().first.source==&cloud&&live.front().second==13);
 cloud.duration=0;assert(a.Clouds().front().first.source==&trigger);
 trigger.live=false;assert(a.Clouds().empty());}
''')
    def test_real_glob_selector_all_dps_and_passive_globs(self):
        path=SOURCE/'ai/playerbot/strategy/actions/ViscidusActions.cpp'
        if not path.exists(): self.fail('no prompt encounter selector in baseline')
        method=block(path.read_text(),'Unit* ViscidusCombatAction::Target()')
        run(r'''
#include <cassert>
#include <vector>
#include "ai/playerbot/ViscidusPolicy.h"
using namespace ai;using ObjectGuid=unsigned;
constexpr unsigned VISIBILITY_OFF=0,UNIT_FIELD_FLAGS=0,UNIT_FLAG_SPAWNING=1;
struct Unit{bool live=true,attackable=true,spawning=false;unsigned visibility=1,guid=1;float distance=5,merge=30;
 struct Guid{unsigned value;unsigned GetRawValue(){return value;}};Guid GetObjectGuid(){return {guid};}
 float GetDistance(Unit*){return merge;}
 unsigned GetVisibility(){return visibility;}bool HasFlag(unsigned,unsigned){return spawning;}};
struct Player{bool healer=false,tank=false;bool IsValidAttackTarget(Unit* u){return u->attackable;}
 float GetDistance(Unit* u){return u->distance;}};
struct Config{float sightDistance=60;}sPlayerbotAIConfig;
struct AI{Unit* glob=nullptr;Unit* held=nullptr;Unit* other=nullptr;bool IsHeal(Player* p){return p->healer;}
 bool IsTank(Player* p){return p->tank;}Unit* GetUnit(ObjectGuid guid){return guid==2?other:glob;}};
bool Live(Player*,Unit* u){return u&&u->live;}
#define AI_VALUE(type,name) ai->held
struct ViscidusCombatAction{Player* bot;AI* ai;Unit* boss;std::vector<ObjectGuid> globs{1};
 Unit* Boss(){return boss;}Unit* Target();};
'''+method+r'''
int main(){Player bot;Unit boss,glob;boss.visibility=0;AI ai{&glob,&boss};ViscidusCombatAction action{&bot,&ai,&boss};
 assert(action.Target()==&glob); // ranged and melee share this selector, no combat flag/victim required on glob
 ai.held=&glob;assert(action.Target()==&glob);
 Unit urgent;urgent.guid=2;urgent.distance=20;urgent.merge=5;glob.merge=40;ai.other=&urgent;action.globs={1,2};
 assert(action.Target()==&urgent); // urgent merge beats a held, bot-near glob
 urgent.merge=41;assert(action.Target()==&glob); // retain within same urgency tier
 action.globs={1};
 bot.healer=true;assert(!action.Target());bot.healer=false;
 bot.tank=true;assert(!action.Target());bot.tank=false;
 glob.live=false;assert(!action.Target());glob.live=true;
 glob.attackable=false;assert(!action.Target());glob.attackable=true;
 glob.distance=100;assert(!action.Target());glob.distance=5;
 action.boss=nullptr;assert(!action.Target());action.boss=&boss;
 action.globs.clear();boss.visibility=1;assert(action.Target()==&boss);}
''')
    def test_production_freeze_spell_gate(self):
        method=block((ROOT/'ai/playerbot/strategy/actions/ViscidusActions.cpp').read_text(),
                     'bool ai::ViscidusBlocksSpell(').replace('bool ai::','bool ')
        run(r'''
#include <cassert>
#include "ai/playerbot/ViscidusPolicy.h"
using namespace ai;using uint32=unsigned;constexpr unsigned SPELL_SCHOOL_FROST=4;
struct Unit{unsigned entry=15299;unsigned GetEntry(){return entry;}};
struct Player{};struct PlayerbotAI{Player bot;bool enabled=true,waitingMage=true;Player*GetBot(){return &bot;}};
struct SpellEntry{unsigned School=4;bool positive=false;};
struct Store{SpellEntry spell;template<class T>T* LookupEntry(unsigned){return &spell;}}sSpellTemplate;
bool frozen=true;bool IsViscidusShatterTarget(Player*,Unit*){return frozen;}
bool ViscidusEnabled(PlayerbotAI*a){return a&&a->enabled;}
bool WaitingCaster(PlayerbotAI*a){return a->waitingMage;}
bool IsPositiveSpell(SpellEntry const*s){return s->positive;}
'''+method+r'''
int main(){PlayerbotAI ai;Unit boss;
 assert(ViscidusBlocksSpell(&ai,116,&boss));
 sSpellTemplate.spell.School=2;assert(ViscidusBlocksSpell(&ai,133,&boss)); // mage waits
 ai.waitingMage=false;sSpellTemplate.spell.School=0;assert(!ViscidusBlocksSpell(&ai,75,&boss)); // physical ranged/melee
 sSpellTemplate.spell.School=4;assert(ViscidusBlocksSpell(&ai,8056,&boss));
 sSpellTemplate.spell.positive=true;assert(!ViscidusBlocksSpell(&ai,1,&boss));
 sSpellTemplate.spell.positive=false;boss.entry=15667;assert(!ViscidusBlocksSpell(&ai,116,&boss));
 boss.entry=15299;frozen=false;assert(!ViscidusBlocksSpell(&ai,116,&boss));
 frozen=true;ai.enabled=false;assert(!ViscidusBlocksSpell(&ai,116,&boss));}
''')
    def test_handoff_and_stage_wiring(self):
        action=(ROOT/'ai/playerbot/strategy/actions/ViscidusActions.cpp').read_text()
        update=block(action,'void ViscidusCombatAction::Update()')
        self.assertIn('InterruptNonMeleeSpells(false)',update)
        self.assertIn('Attack(GetMaster(), target)',update)
        self.assertNotIn('MoveNear(',update)
        self.assertIn('ai->IsHeal(bot)',update)
        target=block(action,'Unit* ViscidusCombatAction::Target()')
        self.assertIn('bot->IsValidAttackTarget(glob)',target)
        self.assertLess(target.index('if (best) return best;'),target.index('VISIBILITY_OFF'))
        spells=(ROOT/'ai/playerbot/PlayerbotAISpells.cpp').read_text()
        self.assertEqual(spells.count('ViscidusBlocksSpell(this,'),3)
        diag=(ROOT/'host/ViscidusDiagnostics.cpp').read_text()
        for token in ('GetAura(25937, EFFECT_INDEX_1)','ViscidusFrozenStart','ViscidusFrozenEnd',
                      'ViscidusFrozenHits','ViscidusShatter','ViscidusGlobSpawn','ViscidusGlobDeath'):
            self.assertIn(token,diag)
if __name__=='__main__':unittest.main()
