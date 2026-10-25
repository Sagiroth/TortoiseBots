"""Regression: an owned movement must not consume every combat decision."""
import os
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(os.environ.get('TBOTS_OPENING_SOURCE',Path(__file__).resolve().parents[1]))
def extract(s,key):
 a=s.index(key);b=s.index('{',a)+1;d=1
 while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
def run(s):
 with tempfile.TemporaryDirectory() as d:
  p=Path(d);(p/'t.cpp').write_text(s)
  subprocess.run(['g++','-std=c++17','-I'+str(ROOT/'ai'),str(p/'t.cpp'),'-o',str(p/'t')],check=True)
  subprocess.run([str(p/'t')],check=True)
class OnyxiaOpeningTest(unittest.TestCase):
 def test_active_walk_leaves_engine_free_for_threat(self):
  s=(ROOT/'ai/playerbot/strategy/actions/OnyxiaTankPositionActions.cpp').read_text()
  run('''
#include "playerbot/OnyxiaTankPositionPolicy.h"
#include <cassert>
using namespace ai;
constexpr int CHASE_MOTION_TYPE=1;
struct Motion{int GetCurrentMovementGeneratorType(){return 0;}};
struct Bot{Motion m;float GetPositionX(){return -25;}float GetPositionY(){return -215;}float GetPositionZ(){return -87;}bool IsStopped(){return false;}Motion*GetMotionMaster(){return &m;}};
struct MovementAction{static bool isUseful(){return true;}};
bool GetOnyxiaTankPositionTarget(int*){return true;}
struct OnyxiaTankPositionAction:MovementAction{int*ai=nullptr;Bot b;Bot*bot=&b;bool active=true;bool isUseful();};
'''+extract(s,'bool OnyxiaTankPositionAction::isUseful()')+'''
int main(){OnyxiaTankPositionAction a;assert(!a.isUseful());a.active=false;assert(a.isUseful());}
''')
 def test_movement_is_speed_bounded_and_preserves_path(self):
  run('''
#include "playerbot/OnyxiaTankPositionPolicy.h"
#include <cassert>
using namespace ai;
int main(){OnyxiaTankPoint a{-25,-215,-87},b{20,-215,-85};
auto step=OnyxiaOpeningStep(a,b,4.5f,100);assert(OnyxiaTankDistance(a,step)<=.451);
assert(step.x>a.x);auto delayed=OnyxiaOpeningStep(a,b,4.5f,9000);assert(OnyxiaTankDistance(a,delayed)<=.901);
auto stopped=OnyxiaOpeningStep(a,b,0,100);assert(OnyxiaTankDistance(a,stopped)==0);
assert(OnyxiaTankRouteSafe(a,{a,b},b));assert(!OnyxiaTankRouteSafe(a,{a,{0,-254,-87},b},b));}
''')
 def test_actual_tick_attacks_backsteps_and_cancels(self):
  source=(ROOT/'ai/playerbot/strategy/actions/OnyxiaTankPositionActions.cpp').read_text()
  run(r'''
#include "playerbot/OnyxiaTankPositionPolicy.h"
#include <cassert>
#include <string>
#include <vector>
using namespace ai;using uint32=unsigned;
constexpr int CLASS_WARRIOR=1,POWER_RAGE=1,MOVE_RUN=1,MOVE_RUN_BACK=2;
constexpr unsigned MOVEFLAG_MASK_MOVING=3,MOVEFLAG_SPLINE_ENABLED=4,MOVEFLAG_FORWARD=1,MOVEFLAG_BACKWARD=2,MSG_MOVE_HEARTBEAT=10;
constexpr float M_PI_F=3.14159265f;
uint32 now=200;struct WorldTimer{static uint32 getMSTime(){return now;}};
struct MovementInfo{unsigned flags=0,ctime=0,stime=999;float x=0,y=0,z=0,o=0;
void RemoveMovementFlag(unsigned f){flags&=~f;}void AddMovementFlag(unsigned f){flags|=f;}
void ChangePosition(float a,float b,float c,float d){x=a;y=b;z=c;o=d;}};
struct WorldPacket{MovementInfo data;unsigned time=0;WorldPacket(int){}WorldPacket&operator<<(MovementInfo m){data=m;data.ctime=m.stime;return *this;}void FillPacketTime(unsigned t){time=t;}};
struct Session{unsigned queued=0;WorldPacket last{0};void QueuePacket(WorldPacket*p){++queued;last=*p;delete p;}}session;
struct Spline{bool final=true;bool Finalized(){return final;}}spline;
struct Unit{float x=-25,y=-215,z=-87;unsigned GetObjectGuid(){return 1;}float GetPositionX(){return x;}float GetPositionY(){return y;}float GetPositionZ(){return z;}}boss;
struct Player:Unit{MovementInfo m_movementInfo;Spline*movespline=&spline;int attacks=0;bool falling=false;
bool GetTransport(){return false;}bool IsTaxiFlying(){return false;}bool IsFalling(){return falling;}
void SetSelectionGuid(unsigned){}void Attack(Unit*u,bool b){assert(u==&boss&&b);++attacks;}
int GetClass(){return CLASS_WARRIOR;}bool CanReachWithMeleeAutoAttack(Unit*){return true;}
unsigned GetPower(int){return 600;}float GetSpeed(int mode){return mode==MOVE_RUN?7.f:4.5f;}Session*GetSession(){return &session;}};
struct WorldPosition{float x=0,y=0,z=0;WorldPosition(){}WorldPosition(unsigned,float a,float b,float c):x(a),y(b),z(c){}WorldPosition(Player*p):x(p->x),y(p->y),z(p->z){}
float getX()const{return x;}float getY()const{return y;}float getZ()const{return z;}
float distance(WorldPosition const&p)const{return OnyxiaTankDistance({x,y,z},{p.x,p.y,p.z});}};
struct PlayerbotAI{Player bot;bool canMove=true;int stopped=0;std::vector<std::string> casts;
bool CanMove(){return canMove;}void StopMoving(){++stopped;}bool HasAura(const char*,Player*){return true;}
bool CanCastSpell(const char*,Unit*){return true;}bool CastSpell(const char*n,Unit*){casts.push_back(n);return true;}}brain;
Unit*current=nullptr;
#define SET_AI_VALUE(T,N,V) current=V
bool enabled=true;
Unit*GetOnyxiaTankPositionTarget(PlayerbotAI*){return enabled?&boss:nullptr;}
struct Config{void logEvent(PlayerbotAI*,const char*,const char*,const char*){}}sPlayerbotAIConfig;
struct OnyxiaTankPositionAction{PlayerbotAI*ai=&brain;Player*bot=&brain.bot;bool active=true,awaiting=false,crossing=false,crossed=false,hazard=false;
uint32 lastUpdate=100,sentAt=0;size_t waypoint=1;WorldPosition expected;std::vector<WorldPosition> openingPath{{249,0,-215,-87},{249,22,-215,-87}};
bool IsMovingAllowed(unsigned,float,float,float){return true;}bool IsHazardNearPosition(WorldPosition){return hazard;}void Update();};
'''+extract(source,'void OnyxiaTankPositionAction::Update()')+r'''
int main(){brain.bot.x=0;OnyxiaTankPositionAction a;
a.Update();assert(brain.bot.attacks==1 && current==&boss);
assert(brain.casts[0]=="shield slam" && brain.casts[1]=="heroic strike");
assert(session.queued==1 && session.last.time==200 && session.last.data.flags==MOVEFLAG_BACKWARD && session.last.data.ctime==0);
assert(session.last.data.x>0 && session.last.data.x<=.451 && session.last.data.o>3.0f);
now=400;a.Update();assert(session.queued==1); // never extrapolate an unaccepted step
now=1400;a.Update();assert(!a.active && brain.stopped==1);
a.active=true;enabled=false;now=1600;a.Update();assert(!a.active && brain.stopped==2);
enabled=true;a.active=true;a.crossing=true;a.awaiting=false;now=1800;a.Update();assert(session.last.data.flags==MOVEFLAG_FORWARD);
a.active=true;brain.bot.falling=true;now=2000;a.Update();assert(!a.active);
}
''')
 def test_live_update_and_packet_contract(self):
  s=(ROOT/'ai/playerbot/strategy/actions/OnyxiaTankPositionActions.cpp').read_text()
  f=extract(s,'void OnyxiaTankPositionAction::Update()')
  self.assertIn('bot->Attack(boss,true)',f)
  self.assertIn('"sunder armor"',f)
  self.assertIn('MOVEFLAG_BACKWARD',f)
  self.assertIn('packet->FillPacketTime(now)',f)
  self.assertIn('uint32(now-sentAt)>1000',f)
  self.assertIn('bot->IsFalling()',f)
  ai=(ROOT/'ai/playerbot/PlayerbotAI.cpp').read_text()
  self.assertIn('UpdateOnyxiaTankOpening(this)',ai)
if __name__=='__main__':unittest.main()
