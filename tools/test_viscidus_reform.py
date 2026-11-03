"""Production-method regressions for threat-reset recovery and safe spacing."""
from pathlib import Path
import os, unittest
from test_viscidus_cycle import block, run
ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path(os.environ.get('VISCIDUS_SOURCE',ROOT))
class ViscidusReform(unittest.TestCase):
 def source(self): return (SOURCE/'ai/playerbot/strategy/actions/ViscidusActions.cpp').read_text()
 def test_tank_election_spare_and_claim(self):
  s=self.source()
  self.assertIn('bool ViscidusCombatAction::TankRecovery(',s,'baseline has no reform recovery')
  run(r'''
#include <cassert>
#include <vector>
#include <map>
#include <mutex>
#include <string>
using uint32=unsigned;using uint64=unsigned long long;
constexpr unsigned CLASS_WARRIOR=1,CLASS_DRUID=11,VISIBILITY_OFF=0,UNIT_FIELD_FLAGS=1,UNIT_FLAG_SPAWNING=2,UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL=4;
struct Guid{uint64 n;uint64 GetRawValue(){return n;}bool operator<(Guid b){return n<b.n;}};
struct Player; struct AI;
struct Unit {unsigned visibility=1;bool spawning=false,held=false;Guid GetObjectGuid(){return {99};}
 unsigned GetVisibility(){return visibility;}bool HasFlag(unsigned,unsigned){return spawning;}};
struct Player {AI* ai=nullptr;unsigned guid=1,role=1;bool alive=true,control=true,melee=true,stance=true;
 Guid GetObjectGuid(){return {guid};}unsigned GetInstanceId(){return 7;}bool IsInMap(Player*){return true;}
 bool HasUnitState(unsigned){return !control;}bool CanReachWithMeleeAutoAttack(Unit*){return melee;}
 unsigned GetClass(){return role;}bool HasAura(unsigned){return stance;}bool HasSpell(unsigned){return true;}int GetGroup(){return 1;}};
struct AI {Player* player;bool tank=true;unsigned ready=0,casts=0;bool CanCastSpell(unsigned,Player*){return true;}
 bool IsTank(Player*){return tank;}bool CastSpell(unsigned id,Unit*){++casts;return id!=0;}
 bool CastSpell(unsigned,Player*){++casts;return true;}unsigned spell(){return 71;}};
using PlayerbotAI=AI;
#define GET_PLAYERBOT_AI(p) ((p)->ai)
#define AI_VALUE2(type,name,key) ai->spell()
std::vector<Player*> members;
std::vector<Player*> LiveGroupMembers(int){return members;}
bool ViscidusEnabled(AI* a){return a&&a->player->alive;}
unsigned ViscidusTaunt(AI* a,Unit*){return a->ready;}
bool TankVictim(AI*,Unit* b){return b->held;}
struct WorldTimer{static unsigned now;static unsigned getMSTime(){return now;}};unsigned WorldTimer::now=10000;
std::mutex retakeMutex;std::map<std::pair<uint32,uint64>,uint32> retakeClaims;
struct Config{void logEvent(AI*,const char*,const char*,std::string){}}sPlayerbotAIConfig;
struct ViscidusCombatAction{Player* bot;AI* ai;bool moved=false;
 bool TankRecovery(Unit*);bool MoveNear(Unit*){return moved=true;}};
'''+block(s,'bool ViscidusCombatAction::TankRecovery(')+r'''
int main(){Player main,spare;spare.guid=2;AI a{&main},b{&spare};main.ai=&a;spare.ai=&b;
 members={&main,&spare};Unit boss;ViscidusCombatAction x{&main,&a},y{&spare,&b};a.ready=b.ready=355;
 assert(x.TankRecovery(&boss)&&a.casts==1);assert(!y.TankRecovery(&boss)&&!b.casts);
 a.ready=0;assert(!y.TankRecovery(&boss)); // claim prevents same-tick duplicate
 WorldTimer::now+=2001;main.alive=false;assert(y.TankRecovery(&boss)&&b.casts==1); // dead main -> spare
 WorldTimer::now+=2001;boss.held=true;assert(!y.TankRecovery(&boss)); // don't taunt another tank
 boss.held=false;boss.visibility=0;assert(!y.TankRecovery(&boss));boss.visibility=1;
 b.ready=0;spare.stance=false;assert(y.TankRecovery(&boss)&&b.casts==2); // prepare stance
 spare.stance=true;spare.melee=false;assert(y.TankRecovery(&boss)&&y.moved);
 spare.control=false;assert(!y.TankRecovery(&boss));}
''')
 def test_caster_distance_respects_real_reach(self):
  s=self.source();self.assertIn('bool ViscidusCombatAction::KeepDistance(',s,'baseline permits casting in boss melee')
  run(r'''
#include <cassert>
constexpr unsigned VISIBILITY_OFF=0,UNIT_FIELD_FLAGS=1,UNIT_FLAG_SPAWNING=2;
struct Unit{float distance=4,reach=12;unsigned visibility=1;bool spawning=false;
 unsigned GetVisibility(){return visibility;}bool HasFlag(unsigned,unsigned){return spawning;}
 float GetCombatReach(Unit*,bool,float){return reach;}};
using Player=Unit;struct WorldPosition{Unit* u;explicit WorldPosition(Unit* p):u(p){}float distance(WorldPosition b){return u->distance;}};
struct AI{bool tank=false,heal=false,ranged=true;bool IsTank(Player*){return tank;}bool IsHeal(Player*){return heal;}bool IsRanged(Player*){return ranged;}};
struct ViscidusCombatAction{Player* bot;AI* ai;Unit* boss;Unit* Boss(){return boss;}bool KeepDistance();};
'''+block(s,'bool ViscidusCombatAction::KeepDistance(')+r'''
int main(){Player bot;Unit boss;AI ai;ViscidusCombatAction a{&bot,&ai,&boss};assert(a.KeepDistance());
 bot.distance=18;assert(!a.KeepDistance());boss.reach=20;assert(a.KeepDistance());
 boss.visibility=0;assert(!a.KeepDistance());boss.visibility=1;ai.tank=true;assert(!a.KeepDistance());
 ai.tank=false;ai.ranged=false;assert(!a.KeepDistance());ai.heal=true;assert(a.KeepDistance());a.boss=nullptr;assert(!a.KeepDistance());}
''')
 def test_blocked_ring_uses_checked_fallback(self):
  s=self.source();self.assertIn('bool ViscidusCombatAction::RingFallback(',s,'baseline abandons blocked assigned slot')
  run(r'''
#include <cassert>
#include <string>
using uint32=unsigned;
struct Player{unsigned GetInstanceId(){return 1;}};
struct WorldPosition{int slot=0;WorldPosition(){}explicit WorldPosition(Player*){}float distance(WorldPosition p){return p.slot==9?0:5;}};
struct WorldTimer{static unsigned getMSTime(){return 10000;}};
struct Config{void logEvent(void*,const char*,const char*,std::string){}}sPlayerbotAIConfig;
struct ViscidusCombatAction{Player* bot;void* ai=nullptr;unsigned ringReported=0;bool safe=false,fallback=true,moved=false;int chosen=0;
 bool RingPoint(WorldPosition& p){p.slot=1;return true;}bool CheckedRoute(WorldPosition){return safe;}
 bool RingFallback(WorldPosition& p){p.slot=2;return fallback;}bool MoveTo(WorldPosition p){chosen=p.slot;return moved=true;}
 bool MoveRing();};
'''+block(s,'bool ViscidusCombatAction::MoveRing(')+r'''
int main(){Player bot;ViscidusCombatAction a{&bot};assert(a.MoveRing()&&a.chosen==2);
 a.fallback=false;a.moved=false;assert(!a.MoveRing()&&!a.moved);a.safe=true;assert(a.MoveRing()&&a.chosen==1);}
''')
  fallback=block(s,'bool ViscidusCombatAction::RingFallback(')
  self.assertIn('std::sort(candidates.begin()',fallback)
  self.assertIn('CanReachWithMeleeAutoAttackAtPosition',fallback)
  self.assertIn('if (CheckedRoute(candidate))',fallback)
 def test_reset_gap_latch_and_priority_wiring(self):
  s=self.source();boss=block(s,'Unit* ViscidusCombatAction::Boss(')
  self.assertIn('engaged && Live(bot, boss) && boss->IsInCombat()',boss)
  self.assertIn('!Live(bot, boss) || !boss->IsInCombat()',boss)
  update=block(s,'void ViscidusCombatAction::Update(')
  self.assertIn('retake_ms=',update);self.assertIn('boss->GetVictim() == bot',update)
  self.assertIn('if (KeepDistance() && ai->CanMove())',update)
  strategy=(SOURCE/'ai/playerbot/strategy/generic/TempleOfAhnQirajStrategies.cpp').read_text()
  self.assertIn('"viscidus keep distance", ACTION_EMERGENCY + 7',strategy)
  self.assertIn('NeedsViscidusDistance(ai)',strategy)
if __name__=='__main__':unittest.main()
