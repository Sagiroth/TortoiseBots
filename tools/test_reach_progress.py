"""Production reach eligibility and timeout slices with explicit core API doubles.

REACH_SOURCE can select an unmodified ReachTargetActions.h to demonstrate the
same regression assertions failing. No navmesh or full Engine is simulated.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


def body(source, marker):
    start = source.index('{', source.index(marker))
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class ReachProgressTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_production_range_and_timeout(self):
        root = Path(__file__).resolve().parents[1]
        source = Path(os.environ.get('REACH_SOURCE', root / 'ai/playerbot/strategy/actions/ReachTargetActions.h')).read_text()
        useful = body(source, 'virtual bool isUseful() override')
        within = body(source, 'bool IsWithinReach(') if 'bool IsWithinReach(' in source else '{return bot->GetDistance(target) <= range;}'
        stand_start = source.index('uint32 const nowMs = WorldTimer::getMSTime();')
        stand_end = source.index('lastExecuteMs = nowMs;', stand_start) + len('lastExecuteMs = nowMs;')
        stand = source[stand_start:stand_end]
        head_start = source.index('uint32 const nowMs = WorldTimer::getMSTime();', stand_end)
        head_end = source.index('if (!groupInstanceFight && (noHeadway || stoodStill))', head_start)
        head = source[head_start:head_end]
        cpp = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
using uint32=std::uint32_t;
using uint64=std::uint64_t;
constexpr int SPELL_RANGE_IDX_COMBAT=2,SPELLMOD_RANGE=1,SPELL_AURA_MOD_ATTACK_AND_SPELL_RANGE=2;
constexpr int UNIT_STAT_NO_FREE_MOVE=1,UNIT_STAT_FLEEING=2,UNIT_STAT_CONFUSED=4,UNIT_STAND_STATE_STAND=0;
constexpr float ATTACK_DISTANCE=5.0f;
struct ObjectGuid {unsigned id=0;bool operator!=(ObjectGuid o)const{return id!=o.id;}};
uint32 clockMs=1000;
struct WorldTimer {static uint32 getMSTime(){return clockMs;}};
struct Player;
struct Unit {Player* GetCharmerOrOwnerPlayerOrPlayerItself(){return nullptr;}bool IsCreature(){return true;}ObjectGuid guid{1};Unit* victim=nullptr;bool runner=false,unreachable=false;
 ObjectGuid getObjectGuid(){return guid;} Unit* GetVictim(){return victim;}
 bool HasUnitState(int){return runner;}
};
struct Player {float rangeMod=0;float ApplySpellMod(uint32,int,float& base){assert(base==5);return rangeMod;}};
struct Map{bool IsDungeon(){return false;}};
struct Bot:Unit {Map map;bool GetGroup(){return false;}Map* GetMap(){return &map;}bool IsInSameRaidWith(Player*){return false;}float distance=3,x=0,y=0,auraRange=0,lastModifier=0;
 bool valid=true,casting=false,los=true,rooted=false,held=false,combat=true,autoReach=true;
 int stand=0;Player* owner=nullptr;
 bool IsValidAttackTarget(Unit*){return valid;}
 bool IsNonMeleeSpellCasted(bool,bool,bool){return casting;}
 bool IsWithinLOSInMap(Unit*,bool){return los;}
 float GetDistance(Unit*){return distance;}
 bool CanReachWithMeleeAutoAttack(Unit*){return autoReach;}
 // Geometry boundary: native core owns target reach, leeway and XY-only spell
 // distance. Record the modifier supplied by the real module helper.
 bool CanReachWithMeleeSpellAttack(Unit*,float modifier){lastModifier=modifier;return distance < 5+modifier;}
 Player* GetSpellModOwner(){return owner;} float GetTotalAuraRangeModifier(int){return auraRange;}
 bool IsRooted(){return rooted;} bool HasUnitState(int){return held;}
 int GetStandState(){return stand;}
 float GetPositionX(){return x;}float GetPositionY(){return y;}
 bool IsInCombat(){return combat;}
};
struct AI {bool stay=false,castable=true;
 bool HasStrategy(std::string,int){return stay;}int GetState(){return 0;}
 bool CanCastSpell(std::string,Unit*,bool,void*,bool,bool,bool){return castable;}
};
struct SpellEntry {uint32 Id=1,rangeIndex=2;};
struct Facade {SpellEntry entry;bool known=true;SpellEntry const* LookupSpellInfo(uint32){return known?&entry:nullptr;}} sServerFacade;
#define AI_VALUE2(type,key,name) uint32(1)
struct Reach {Bot* bot;AI* ai;Unit* target;float range=0;std::string spellName;bool hostile=true;
 uint32 duration=0;uint32 GetDuration()const{return duration;}
 ObjectGuid noLosTarget,stoodTarget;uint32 noLosSinceMs=0,stoodSinceMs=0,lastExecuteMs=0;
 float noLosBestDist=0,noLosStartX=0,noLosStartY=0,stoodX=0,stoodY=0,stoodDist=0;
 static constexpr uint32 kStandResetMs=2000,kNoHeadwayMs=15000,kNoMoveMs=10000;
 Player* GetMaster(){return nullptr;}
 Unit* GetTarget(){return target;}bool RequiresAttackableTarget()const{return hostile;}
 static bool TargetCannotBeReached(Unit* target){return target->unreachable;}
 bool IsWithinReach(Unit* target) WITHIN
 bool isUseful() USEFUL
 bool tick(){
  float const distanceToTarget=bot->GetDistance(target);bool const inLos=bot->los;
  {STAND}
  {HEAD return !groupInstanceFight && (noHeadway || stoodStill);}
 }
};
'''.replace('WITHIN', within).replace('USEFUL', useful).replace('STAND}', stand + '}').replace('HEAD return', head + 'return')
        cpp += r'''
int main(int argc,char** argv){
 Bot bot;AI ai;Unit target;Reach r{&bot,&ai,&target};
 auto reset=[&]{bot=Bot{};ai=AI{};target=Unit{};r=Reach{&bot,&ai,&target};sServerFacade=Facade{};clockMs=1000;};
 bool timeoutOnly=argc>1 && std::string(argv[1])=="timeout";
 if(!timeoutOnly){
  // Already in melee: the engine must not queue another reach prerequisite.
  assert(!r.isUseful());
  r.spellName="sunder armor";r.range=1.5f;assert(!r.isUseful());
  // Modifiers use the same arguments as core Spell::CheckRange.
  Player owner;owner.rangeMod=2;bot.owner=&owner;bot.auraRange=5000;bot.distance=8;
  assert(!r.isUseful());assert(bot.lastModifier==4.0f);
  bot.distance=10;assert(r.isUseful());
  bot.distance=3;bot.los=false;assert(r.isUseful());
  bot.los=true;bot.distance=10;ai.stay=true;assert(!r.isUseful());
  ai.stay=false;bot.casting=true;assert(!r.isUseful());
  bot.casting=false;bot.valid=false;assert(!r.isUseful());
  bot.valid=true;ai.castable=false;assert(!r.isUseful());
  reset();bot.autoReach=false;assert(r.isUseful());
  reset();r.range=30;r.spellName="frostbolt";sServerFacade.entry.rangeIndex=4;
  bot.distance=29;assert(!r.isUseful());bot.distance=31;assert(r.isUseful());
  reset();r.hostile=false;bot.autoReach=true;assert(r.isUseful()); // zero-range nonattack movement unchanged
  reset();r.spellName="sunder armor";r.range=1.5f;assert(!r.tick());
  for(clockMs=2000;clockMs<=18000;clockMs+=1000)assert(!r.tick()); // stale queued reaches cannot blacklist a reachable target
 }
 // A reach, successful fight/cast gap, then renewed reach: no immediate ban.
 reset();bot.distance=50;bot.autoReach=false;r.duration=3000;assert(!r.tick());
 clockMs=61000;assert(!r.tick());
 // Continuous immobile failed reach still gives up after fifteen seconds.
 reset();bot.distance=50;bot.autoReach=false;assert(!r.tick());
 for(clockMs=2000;clockMs<16000;clockMs+=1000)assert(!r.tick());
 assert(r.tick());
 // A normal movement action may deliberately wait three seconds. That
 // scheduled interval must still count as continuous failed movement.
 reset();bot.distance=50;bot.autoReach=false;r.duration=3000;assert(!r.tick());
 for(clockMs=4000;clockMs<16000;clockMs+=3000)assert(!r.tick());
 assert(r.tick());
 // A held/casting interval resets both clocks, not only the no-move clock.
 reset();bot.distance=50;bot.autoReach=false;assert(!r.tick());
 for(clockMs=2000;clockMs<=16000;clockMs+=1000){bot.casting=true;assert(!r.tick());}
 bot.casting=false;assert(!r.tick());
 // Actual progress and live fleeing retain the old exclusions.
 reset();bot.distance=50;bot.autoReach=false;assert(!r.tick());
 for(clockMs=2000;clockMs<16000;clockMs+=1000)assert(!r.tick());
 bot.distance=47;assert(!r.tick());
 reset();bot.distance=50;bot.autoReach=false;assert(!r.tick());
 for(clockMs=2000;clockMs<16000;clockMs+=1000)assert(!r.tick());
 bot.x=9;assert(!r.tick());
 reset();bot.distance=50;bot.autoReach=false;target.runner=true;assert(!r.tick());
 for(clockMs=2000;clockMs<=18000;clockMs+=1000)assert(!r.tick());
 // An enemy already attacking us remains exempt unless the core says it
 // cannot reach us. Preserve the existing evade/unreachable escape path.
 reset();bot.distance=50;bot.autoReach=false;target.victim=&bot;assert(!r.tick());
 for(clockMs=2000;clockMs<=18000;clockMs+=1000)assert(!r.tick());
 reset();target.unreachable=true;assert(!r.tick());
 for(clockMs=2000;clockMs<16000;clockMs+=1000)assert(!r.tick());
 assert(r.tick());
 std::cout<<"reach eligibility and active timeout PASS\n";
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source_path=Path(directory)/'fixture.cpp'; binary=Path(directory)/'fixture'
            source_path.write_text(cpp)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Wno-missing-field-initializers',str(source_path),'-o',str(binary)],check=True)
            subprocess.run([str(binary),os.environ.get('REACH_TEST_CASE','range')],check=True)


if __name__=='__main__':
    unittest.main()
