"""Production handoff/recovery loop; the engine success break and spells are boundaries."""
import os
from pathlib import Path
import unittest
from test_onyxia_air_recovery import STUB, function, run

ROOT = Path(os.environ.get('TBOTS_539_SOURCE', Path(__file__).resolve().parents[1]))
class OnyxiaAirLoopTest(unittest.TestCase):
    def test_assist_yields_to_cast_and_auto_shot_after_recovery(self):
        actions=(ROOT/'ai/playerbot/strategy/actions/OnyxiasLairDungeonActions.cpp').read_text()
        choose=(ROOT/'ai/playerbot/strategy/actions/ChooseTargetActions.cpp').read_text()
        triggers=(ROOT/'ai/playerbot/strategy/triggers/OnyxiasLairDungeonTriggers.cpp').read_text()
        stub=STUB.replace('bool HasAura(unsigned){return hover;}', 'bool HasAura(unsigned id){return id==17131&&hover;}')
        stub=stub.replace('Unit*GetOnyxiaRangedTarget(PlayerbotAI*a){return GetAirborneOnyxia(a);}', 'Unit*GetOnyxiaRangedTarget(PlayerbotAI*);bool OnyxiaAirNeedsAssist(PlayerbotAI*);')
        stub=stub.replace('struct WorldTimer{static unsigned getMSTime(){return 10000;}};', 'unsigned now=10000;struct WorldTimer{static unsigned getMSTime(){return now;}};')
        stub=stub.replace('Creature boss;bool ranged', 'Creature boss;Unit whelp;bool ranged')
        stub=stub.replace('PlayerbotAI(){bot.combat=false;', 'PlayerbotAI(){whelp.guid=88;whelp.entry=11262;bot.combat=false;')
        stub=stub.replace('return g==99?&boss:nullptr;', 'return g==99?static_cast<Unit*>(&boss):g==88?&whelp:nullptr;')
        code=stub+'''
using uint32=unsigned;
bool IsRaidCrowdControlMarkedTarget(PlayerbotAI*,Unit*){return false;}
Unit* rescue=nullptr;Unit*GetOnyxiaWhelpTarget(PlayerbotAI*){return rescue;}
struct DpsAssistAction{PlayerbotAI*ai;Player*bot;AiObjectContext*context;Unit*GetTarget(){return context->marked.v?context->marked.v:ai::GetOnyxiaRangedTarget(ai);}bool isUseful();};
struct OnyxiaAirborneTrigger{PlayerbotAI*ai;bool IsActive();};
'''
        for marker in ['bool EngagedAirborneOnyxia(', 'bool OnyxiaAirDamageRole(', 'Unit* ai::GetAirborneOnyxia(',
                       'bool ai::IsOnyxiaAirCombatTarget(', 'bool ai::OnyxiaRangedFocusEnabled(',
                       'void ai::UpdateOnyxiaAirCombat(', 'Unit* ai::GetOnyxiaRangedTarget(']:code+=function(actions,marker)
        if 'bool ai::OnyxiaAirNeedsAssist(' in actions:code+=function(actions,'bool ai::OnyxiaAirNeedsAssist(')
        code+=function(choose,'bool DpsAssistAction::isUseful(')
        code+=function(triggers,'bool OnyxiaAirborneTrigger::IsActive(')
        # Run the actual new stale-queue gate, stopping at the unrelated loot body.
        prefix=choose[choose.index('bool SelectNewTargetAction::Execute('):]
        prefix=prefix[:prefix.index('    if (target && sServerFacade.UnitIsDead(target))')]
        prefix=prefix[prefix.index('{')+1:]
        code+='bool queuedReselectionWouldClear(PlayerbotAI*ai){Player*bot=ai->GetBot();auto*context=ai->GetAiObjectContext();'+prefix+'return true;}\n'
        run(code+r'''
int main(){PlayerbotAI a;DpsAssistAction assist{&a,&a.bot,&a.context};OnyxiaAirborneTrigger trigger{&a};
int attacks=0,casts=0,shots=0;
for(int tick=0;tick<8;++tick){now+=2000;UpdateOnyxiaAirCombat(&a);
 // Engine breaks after any successful high-priority assist; normal casts
 // run only when that action is absent/useless. Attack's same-victim success
 // is the live behavior, so the fixture deliberately does not hide it.
 if(trigger.IsActive()&&assist.isUseful()){++attacks;a.bot.victim=GetOnyxiaRangedTarget(&a);continue;}
 ++casts;if(!shots)++shots;
}
assert(attacks==1&&casts==7&&shots==1);
assert(a.context.invalid.resets>0&&a.context.dps.resets>0);
assert(!queuedReselectionWouldClear(&a));
// A queued assist from an older trigger must also yield once engaged.
assert(!assist.isUseful());
a.strategy="onyxia";assert(trigger.IsActive()&&!assist.isUseful()); // base generic spread retained
a.strategy="onyxia air spread";
// A new raid mark still gets its handoff ahead of automatic boss focus.
a.context.marked.v=&a.whelp;assert(assist.isUseful());a.context.marked.v=nullptr;
// Both selectors/rotation see one boss/whelp choice for this decision.
now+=2000;rescue=&a.whelp;assert(GetOnyxiaRangedTarget(&a)==&a.whelp);
rescue=nullptr;assert(GetOnyxiaRangedTarget(&a)==&a.whelp);
now+=1001;assert(GetOnyxiaRangedTarget(&a)==&a.boss);
// Recovery should not erase the cast/action clock just because target changed.
int clocks=a.clocks;now+=1001;rescue=&a.whelp;UpdateOnyxiaAirCombat(&a);assert(a.clocks==clocks);
// Dead rescue targets never get cached for the rest of the interval.
a.whelp.alive=false;rescue=nullptr;assert(GetOnyxiaRangedTarget(&a)==&a.boss);
// Restart a real auto-repeat still bound to a whelp; leave a correct one alone.
Spell repeat;repeat.m_targets.guid=88;a.bot.repeat=&repeat;a.context.active.v=75;
now+=1001;UpdateOnyxiaAirCombat(&a);assert(a.bot.interrupts==1&&!a.bot.repeat&&a.context.active.resets==1);
repeat.m_targets.guid=99;a.bot.repeat=&repeat;a.context.active.v=75;
UpdateOnyxiaAirCombat(&a);assert(a.bot.interrupts==1&&a.bot.repeat==&repeat&&a.context.active.v==75);
a.enabled=false;assert(assist.isUseful()); // generic assist untouched
}
''')
    def test_native_engine_has_success_break_and_hunter_start(self):
        engine=(ROOT/'ai/playerbot/strategy/Engine.cpp').read_text()
        block=function(engine,'if (actionExecuted)')
        self.assertIn('break;',block)
        hunter=(ROOT/'ai/playerbot/strategy/hunter/HunterActions.cpp').read_text()
        self.assertIn('CastSpellAction::Execute(event)',function(hunter,'bool CastAutoShotAction::Execute('))
        self.assertIn('"AutoShot"',function(hunter,'bool CastAutoShotAction::Execute('))
    def test_spell_reach_uses_hunter_minimum_and_combat_reach(self):
        source=(ROOT/'ai/playerbot/strategy/actions/ReachTargetActions.h').read_text()
        body=function(source,'bool IsWithinReach(')
        prefix=body[body.index('{')+1:body.index('// GetSpellRange')]
        run(r'''
#include <cassert>
#include <string>
struct Unit{};
struct Bot{float distance=0;float GetCombatDistance(Unit*){return distance;}float GetDistance(Unit*){return distance;}};
struct AI{bool air=true;bool GetSpellRange(std::string,float*max,float*min){*max=35;*min=8;return true;}};
bool IsOnyxiaAirCombatTarget(AI*a,Unit*){return a->air;}
struct Reach{AI*ai;Bot*bot;float range=35;std::string spellName="auto shot";bool within(Unit*target){
'''+prefix+r'''
return bot->GetDistance(target)<=range;}};
int main(){Bot b;AI a;Unit target;Reach r{&a,&b};
b.distance=5;assert(!r.within(&target));b.distance=15;assert(r.within(&target));
b.distance=36;assert(!r.within(&target));a.air=false;b.distance=5;assert(r.within(&target));}
''')
    def test_diagnostic_rate_limits_and_raw_geometry(self):
        source=(ROOT/'ai/playerbot/strategy/actions/OnyxiaAirReachAction.cpp').read_text()
        if 'void ai::LogOnyxiaAir(' not in source:
            self.fail('No reach/cast failure diagnostics on baseline')
        stub=STUB.replace('struct WorldTimer{static unsigned getMSTime(){return 10000;}};', 'unsigned now=10000;struct WorldTimer{static unsigned getMSTime(){return now;}};')
        stub=stub.replace('unsigned choiceAt=0;', 'unsigned choiceAt=0;unsigned lastLog[4]{};')
        stub=stub.replace('unsigned GetEntry(){return entry;}', 'float GetPositionX(){return -25;}float GetPositionY(){return -215;}float GetPositionZ(){return -86;}unsigned GetEntry(){return entry;}')
        stub=stub.replace('unsigned GetMapId(){return map;}', 'unsigned GetInstanceId(){return instance;}float GetCombatDistance(Unit*){return 20;}bool IsWithinLOSInMap(Unit*,bool){return true;}unsigned GetMapId(){return map;}')
        stub=stub.replace('int GetSelectionGuid(){return selection;}', 'ObjectGuid GetSelectionGuid(){return selection;}')
        stub=stub.replace('struct Config {float sightDistance=60;void logEvent(PlayerbotAI*,std::string,std::string,std::string){}}', 'int rows=0;std::string detail;struct Config {float sightDistance=60;void logEvent(PlayerbotAI*,std::string,std::string,std::string d){++rows;detail=d;}}')
        stub=stub.replace('void LogOnyxiaAir(PlayerbotAI*,OnyxiaAirLog,char const*,Unit* =nullptr,std::string const& = ""){}', 'namespace ai {void LogOnyxiaAir(PlayerbotAI*,OnyxiaAirLog,char const*,Unit* =nullptr,std::string const& = "");}')
        run('#include <sstream>\n'+stub+'Unit* ai::GetAirborneOnyxia(PlayerbotAI*a){return &a->boss;}\n'+function(source,'void ai::LogOnyxiaAir(')+r'''
int main(){PlayerbotAI a;LogOnyxiaAir(&a,OnyxiaAirLog::Reach,"no candidate",&a.boss);
assert(rows==1&&detail.find("xyz=-25/-215/-86")!=std::string::npos&&detail.find("instance=1")!=std::string::npos);
LogOnyxiaAir(&a,OnyxiaAirLog::Reach,"path rejected",&a.boss);assert(rows==1);
LogOnyxiaAir(&a,OnyxiaAirLog::Cast,"LOS",&a.boss);assert(rows==2);
now+=1999;LogOnyxiaAir(&a,OnyxiaAirLog::Reach,"path rejected",&a.boss);assert(rows==2);
++now;LogOnyxiaAir(&a,OnyxiaAirLog::Reach,"path rejected",&a.boss);assert(rows==3);
LogOnyxiaAir(&a,OnyxiaAirLog::State,"active",&a.boss);assert(rows==4);
now+=2000;LogOnyxiaAir(&a,OnyxiaAirLog::State,"active",&a.boss);assert(rows==4);
a.bot.map=1;LogOnyxiaAir(&a,OnyxiaAirLog::Cast,"gate",&a.boss);assert(rows==4);}
''')

if __name__=='__main__':unittest.main()
