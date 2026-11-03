"""Execute the production glob burst action against deterministic spell/range doubles."""
from pathlib import Path
import os, shutil, unittest
from test_viscidus_cycle import block, run
ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path(os.environ.get('VISCIDUS_SOURCE',ROOT))
@unittest.skipUnless(shutil.which('g++'),'requires compiler')
class ViscidusBurst(unittest.TestCase):
 def test_production_instant_burst_and_close_range(self):
  text=(SOURCE/'ai/playerbot/strategy/actions/ViscidusActions.cpp').read_text()
  self.assertIn('bool ViscidusCombatAction::GlobBurst(',text,'baseline delegates globs to normal rotations')
  method=block(text,'bool ViscidusCombatAction::GlobBurst(')
  run(r'''
#include <cassert>
#include <map>
#include <string>
#include <algorithm>
using uint32=unsigned;constexpr unsigned CLASS_MAGE=8,CLASS_WARRIOR=1,MAX_EFFECT_INDEX=3,
 SPELL_AURA_MOD_ROOT=1,SPELL_AURA_MOD_DECREASE_SPEED=2,CURRENT_MELEE_SPELL=0;
struct SpellEntry{unsigned id=0,EffectRadiusIndex[3]={1,0,0};bool channel=false;unsigned cast=0;};
struct Store{std::map<unsigned,SpellEntry> spells;template<class T>T*LookupEntry(unsigned id){return id?&spells[id]:nullptr;}}sSpellTemplate;
struct SpellRadiusEntry{float Radius=10;};struct Radii{SpellRadiusEntry r;SpellRadiusEntry*LookupEntry(unsigned i){return i?&r:nullptr;}}sSpellRadiusStore;
struct Unit{unsigned entry=15667;float merge=12;bool root=false,slow=false,immune=false;
 unsigned GetEntry(){return entry;}float GetDistance(Unit*){return merge;}
 bool HasAuraType(unsigned t){return t==1?root:slow;}bool IsImmuneToSpell(SpellEntry const*,bool){return immune;}
 struct Guid{unsigned GetRawValue(){return 2;}};Guid GetObjectGuid(){return {};}};
struct Player:Unit{unsigned role=8;float distance=5;bool melee=true,stance=true,queued=false;
 unsigned GetClass(){return role;}float GetDistance(Unit*){return distance;}bool IsWithinLOSInMap(Unit*,bool){return true;}
 bool CanReachWithMeleeAutoAttack(Unit*){return melee;}bool HasAura(unsigned){return stance;}
 void*GetCurrentSpell(unsigned){return queued?this:nullptr;}unsigned GetInstanceId(){return 1;}};
struct AI{bool tank=false,heal=false;unsigned cast=0;Unit*recipient=nullptr;std::map<unsigned,bool>ready;
 bool IsTank(Player*){return tank;}bool IsHeal(Player*){return heal;}
 bool CanCastSpell(unsigned id,Unit*){return ready[id];}
 bool CastSpell(unsigned id,Unit*u){cast=id;recipient=u;return true;}};
unsigned Id(char const*n){std::string s=n;return s=="arcane explosion"?1:s=="frost nova"?2:s=="berserker stance"?3:s=="whirlwind"?4:s=="hamstring"?5:s=="cleave"?6:0;}
#define AI_VALUE2(type,key,name) Id(name)
bool IsChanneledSpell(SpellEntry const*s){return s->channel;}unsigned GetSpellCastTime(SpellEntry const*s,Player*){return s->cast;}
struct Config{void logEvent(AI*,const char*,const char*,std::string){}}sPlayerbotAIConfig;
struct ViscidusCombatAction{Player*bot;AI*ai;Unit boss;float moved=-1;
 Unit*Boss(){return &boss;}bool MoveNear(Unit*,float radius=0){moved=radius;return true;}bool GlobBurst(Unit*);};
'''+method+r'''
int main(){Player p;AI a;Unit glob;ViscidusCombatAction action{&p,&a};
 for(unsigned i=1;i<=6;++i){a.ready[i]=true;sSpellTemplate.spells[i].id=i;}
 assert(action.GlobBurst(&glob)&&a.cast==2&&a.recipient==&p); // instant Nova on urgent unrooted glob
 glob.root=true;a.cast=0;assert(action.GlobBurst(&glob)&&a.cast==1); // AE once rooted
 glob.root=false;glob.immune=true;a.cast=0;assert(action.GlobBurst(&glob)&&a.cast==1);
 p.distance=20;a.cast=0;assert(action.GlobBurst(&glob)&&action.moved==7&&a.cast==0); // move into AE, not 30yd cast range
 p.distance=5;sSpellTemplate.spells[1].channel=true;assert(!action.GlobBurst(&glob));sSpellTemplate.spells[1].channel=false;
 p.role=CLASS_WARRIOR;p.stance=false;a.cast=0;assert(action.GlobBurst(&glob)&&a.cast==3);
 p.stance=true;assert(action.GlobBurst(&glob)&&a.cast==4);
 a.ready[4]=false;assert(action.GlobBurst(&glob)&&a.cast==5);
 glob.slow=true;assert(action.GlobBurst(&glob)&&a.cast==6);
 p.queued=true;a.cast=0;assert(!action.GlobBurst(&glob)&&!a.cast);
 a.tank=true;assert(!action.GlobBurst(&glob));a.tank=false;a.heal=true;assert(!action.GlobBurst(&glob));
 a.heal=false;glob.entry=15299;assert(!action.GlobBurst(&glob));}
''')
 def test_ring_and_channel_wiring(self):
  s=(ROOT/'ai/playerbot/strategy/actions/ViscidusActions.cpp').read_text()
  ring=block(s,'bool ViscidusCombatAction::RingPoint(')
  self.assertIn('CanReachWithMeleeAutoAttackAtPosition',ring)
  self.assertIn('boss->HasAura(26036) || Frozen(boss)',ring)
  route=block(s,'bool ViscidusCombatAction::CheckedRoute(')
  for token in ('isPathTo','IsHazardNearPosition(sample)','IsWithinLOS'):
   self.assertIn(token,route)
  update=block(s,'void ViscidusCombatAction::Update(')
  self.assertIn('IsChanneledSpell',update)
  self.assertIn('CURRENT_CHANNELED_SPELL',update)
if __name__=='__main__': unittest.main()
