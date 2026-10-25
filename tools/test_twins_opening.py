#!/usr/bin/env python3
"""Compile production selectors/geometry with narrow world stubs; no live server."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
def extract(source, signature):
    start=source.index(signature); opening=source.index('{',start); depth=1; end=opening+1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
def run_cpp(code):
    with tempfile.TemporaryDirectory() as directory:
        src=Path(directory)/'fixture.cpp'; exe=Path(directory)/'fixture'
        src.write_text(code)
        subprocess.run(['g++','-std=c++17','-I'+str(ROOT/'ai'),str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
class TwinsOpeningTest(unittest.TestCase):
    def source(self):
        return (ROOT/'ai/playerbot/strategy/actions/TwinsOpeningActions.cpp').read_text()
    def test_registered_opening_roles(self):
        source=(ROOT/'ai/playerbot/strategy/StrategyContext.h').read_text()
        for name in ['twins veklor tank','twins veknilash tank','twins east','twins west','twins spell dps']:
            self.assertIn('creators["'+name+'"]',source)
    def test_geometry_and_burst_path(self):
        source=self.source()
        body=extract(source,'bool ai::TwinsOpeningRouteAllowed(')
        run_cpp(r'''
#include "playerbot/TwinsOpeningPolicy.h"
#include <vector>
#include <algorithm>
#include <cassert>
using namespace ai;
struct WorldPosition {float x,y,z;float getX()const{return x;}float getY()const{return y;}float getZ()const{return z;}};
struct Unit{TwinsPoint point;};
struct PlayerbotAI {bool enabled=true;Unit bot{TwinsOutward(kTwinsEast,1,20)},boss{kTwinsEast}; Unit*GetBot(){return &bot;}};
bool TwinsOpeningEnabled(PlayerbotAI*a){return a->enabled;}
Unit* Boss(PlayerbotAI*a,unsigned){return &a->boss;}
TwinsPoint Point(Unit*u){return u->point;}
float BurstRadius(PlayerbotAI*,Unit*){return 14.5f;}
bool DpsHolding(PlayerbotAI*){return false;}
float LandingRadius(PlayerbotAI*,Unit*){return 30;}
namespace ai {bool TwinsOpeningRouteAllowed(PlayerbotAI*,std::vector<WorldPosition> const&);}
'''+body+r'''
int main(){
 assert(TwinsDistance(kTwinsWest,kTwinsEast)>147);
 assert(TwinsSeparated(TwinsTankCamp(0),TwinsTankCamp(1),3));
 assert(!TwinsSeparated({0,0,0},{78,0,0},3));
 assert(TwinsSide(true,true)==-1 && TwinsSide(false,false)==-1);
 assert(TwinsSide(true,false)==0 && TwinsSide(false,true)==1);
 assert(TwinsDamageEntry(true,false)==15275 && TwinsDamageEntry(false,true)==15276);
 assert(!TwinsDamageEntry(true,true));
 assert(TwinsRoom(531,kTwinsWest) && !TwinsRoom(249,kTwinsWest));
 assert(!TwinsRoom(531,{-9019,1180,0}));
 PlayerbotAI a;
 auto far=TwinsOutward(kTwinsEast,0,20);
 // Both endpoints safe, but straight chase crosses Arcane Burst: must reject.
 assert(!TwinsOpeningRouteAllowed(&a,{{far.x,far.y,far.z}}));
 a.enabled=false;assert(TwinsOpeningRouteAllowed(&a,{{far.x,far.y,far.z}}));a.enabled=true;
 a.bot.point=TwinsOutward(kTwinsEast,1,5);
 auto safe=TwinsOutward(kTwinsEast,1,20);
 assert(TwinsOpeningRouteAllowed(&a,{{safe.x,safe.y,safe.z}}));
 assert(!TwinsOpeningRouteAllowed(&a,{{kTwinsEast.x,kTwinsEast.y,kTwinsEast.z}}));
 for(int side=0;side<2;++side){auto home=side ? kTwinsEast:kTwinsWest;
  auto lock=TwinsOutward(home,side,20);assert(TwinsOnSide(lock,side));
  assert(std::abs(TwinsDistance(lock,home)-20)<.01); // 18 yards plus player/boss radii
 }
}
''')
    def test_actual_selectors_and_cast(self):
        source=self.source()
        functions='\n'.join(extract(source,s) for s in ['unsigned TankEntry(', 'bool ai::SelectTwinsTankTarget(',
            'bool ai::SelectTwinsDpsTarget(', 'bool ai::IsTwinsOpeningTargetExcluded(',
            'bool TwinsSearingPainAction::isUseful(', 'bool TwinsSearingPainAction::Execute('])
        run_cpp(r'''
#include "playerbot/TwinsOpeningPolicy.h"
#include <cassert>
#include <map>
#include <string>
using namespace ai;
constexpr int CLASS_WARLOCK=9, CLASS_WARRIOR=1;
struct Unit {unsigned entry;unsigned GetEntry(){return entry;}};
struct Player:Unit {int cls=9;float distance=18;int GetClass(){return cls;}float GetDistance(Unit*){return distance;}};
struct PlayerbotAI {Player bot;Unit physical{15275},caster{15276};bool enabled=true,heal=false,engaged=true,local=true,order=false,ready=true;
 int side=1;std::map<std::string,bool> flags;Unit* cast=nullptr;
 Player*GetBot(){return &bot;}bool IsHeal(Player*){return heal;}
 bool CanCastSpell(const char*,Unit*){return ready;}bool CastSpell(const char*,Unit*u){cast=u;return true;}};
bool Has(PlayerbotAI*a,const char*n){return a->flags[n];}int Side(PlayerbotAI*a){return a->side;}
bool TwinsOpeningEnabled(PlayerbotAI*a){return a->enabled;}
Unit* Boss(PlayerbotAI*a,unsigned e){return e==15275?&a->physical:&a->caster;}
bool OtherOrder(PlayerbotAI*a,Unit*){return a->order;}
bool GroupEngaged(PlayerbotAI*a,Unit*){return a->engaged;}
bool DpsHolding(PlayerbotAI*){return false;}
struct State{bool opening=false;};State ObserveTwins(PlayerbotAI*){return {};}
bool PullOrdered(PlayerbotAI*,Unit*,Unit*){return false;}
bool TwinsMayAttack(PlayerbotAI*,Unit*){return true;}
bool PairWaiting(PlayerbotAI*){return false;}bool PairCasting(PlayerbotAI*){return false;}
unsigned TankEntry(PlayerbotAI*);
Unit* LocalTankBoss(PlayerbotAI*a){return a->local?Boss(a,TankEntry(a)):nullptr;}
namespace ai {bool SelectTwinsTankTarget(PlayerbotAI*,Unit*&);bool SelectTwinsDpsTarget(PlayerbotAI*,Unit*&);bool IsTwinsOpeningTargetExcluded(PlayerbotAI*,Unit*);}
struct Event{};
struct TwinsSearingPainAction{PlayerbotAI*ai;bool isUseful();bool Execute(Event&);};
'''+functions+r'''
int main(){PlayerbotAI a;Unit*t=nullptr;Event e;
 a.flags["twins veklor tank"]=true;assert(TankEntry(&a)==15276);
 assert(SelectTwinsTankTarget(&a,t)&&t==&a.caster);
 assert(IsTwinsOpeningTargetExcluded(&a,&a.physical));
 TwinsSearingPainAction action{&a};assert(action.Execute(e)&&a.cast==&a.caster);
 a.bot.distance=9;assert(!action.isUseful());a.bot.distance=18;
 a.ready=false;assert(!action.isUseful());a.ready=true;
 a.local=false;assert(SelectTwinsTankTarget(&a,t)&&!t);assert(!action.isUseful());a.local=true;
 a.engaged=false;assert(SelectTwinsTankTarget(&a,t)&&!t);assert(!action.Execute(e));a.engaged=true;
 a.order=true;assert(!SelectTwinsTankTarget(&a,t));a.order=false;
 a.enabled=false;assert(!SelectTwinsTankTarget(&a,t));a.enabled=true;
 a.side=-1;assert(!TankEntry(&a));a.side=1;
 a.bot.cls=1;assert(!TankEntry(&a));a.flags["twins veklor tank"]=false;
 a.flags["twins veknilash tank"]=true;assert(TankEntry(&a)==15275);
 assert(SelectTwinsTankTarget(&a,t)&&t==&a.physical);
 a.flags.clear();a.flags["twins physical dps"]=true;
 assert(SelectTwinsDpsTarget(&a,t)&&t==&a.physical);
 assert(IsTwinsOpeningTargetExcluded(&a,&a.caster));
 a.flags.clear();a.flags["twins spell dps"]=true;
 assert(SelectTwinsDpsTarget(&a,t)&&t==&a.caster);
 a.flags["twins physical dps"]=true;assert(!SelectTwinsDpsTarget(&a,t));
 a.flags["twins physical dps"]=false;a.heal=true;assert(!SelectTwinsDpsTarget(&a,t));
 a.heal=false;a.engaged=false;assert(!SelectTwinsDpsTarget(&a,t));
 a.engaged=true;a.order=true;assert(!SelectTwinsDpsTarget(&a,t));
}
''')
    def test_hold_and_scope_wiring(self):
        source=(ROOT/'ai/playerbot/strategy/generic/CombatStrategy.cpp').read_text()
        body=extract(source,'float WaitForAttackMultiplier::GetValue(')
        run_cpp(r'''
#include <string>
#include <cassert>
struct Action{std::string n;std::string getName(){return n;}};
bool tank=false;bool TwinsOpeningTank(int*){return tank;}
struct WaitForAttackStrategy{static bool ShouldWait(int*){return true;}};
struct WaitForAttackMultiplier{int*ai=nullptr;float GetValue(Action*);};
'''+body+r'''
int main(){WaitForAttackMultiplier m;Action pos{"twins position"},engage{"twins engage"},spell{"twins searing pain"},dps{"attack"};
 assert(m.GetValue(&pos)==1);assert(m.GetValue(&engage)==0);tank=true;
 assert(m.GetValue(&engage)==1&&m.GetValue(&spell)==1);assert(m.GetValue(&dps)==0);}
''')
        action=self.source()
        self.assertIn('pull->IsCommandActive() && !pull->IsCommandPullback()',action)
        self.assertIn('!bot->GetGroup()',action)
        self.assertIn('TwinsRoom(bot->GetMapId(),Point(bot))',action)
        self.assertIn('return phase.active || PullOrdered(ai,physical,caster)',action)
        self.assertIn('!end.isPathTo(route,1.f,1.f)',action)
        self.assertIn('TwinsOpeningTank(ai)) return false;',source)
        self.assertIn('bot->GetDistance(dpsTarget)>sPlayerbotAIConfig.sightDistance*.8f',action)
        self.assertIn('init.MovebyPath(WorldPosition().toPointsArray(path))',action)
        for filename,hook in [('MovementActions.cpp','TwinsOpeningChaseAllowed'),('AttackAction.cpp','TwinsPetAttackAllowed')]:
            self.assertIn(hook,(ROOT/'ai/playerbot/strategy/actions'/filename).read_text())
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--root',type=Path);args,rest=parser.parse_known_args()
    if args.root:ROOT=args.root.resolve()
    unittest.main(argv=[__file__]+rest)
