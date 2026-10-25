"""Compile production multiplier and heal gates with core boundary doubles."""
import os
from pathlib import Path
import unittest
from test_raid_tank_healing import body, run
ROOT=Path(os.environ.get('RAID_THREAT_SOURCE',Path(__file__).resolve().parents[1]))
class RaidHealThreatTest(unittest.TestCase):
    def test_threat_veto(self):
        source=(ROOT/'ai/playerbot/strategy/generic/ThreatStrategy.cpp').read_text()
        run(r'''
#include <cassert>
#include <string>
using uint8=unsigned char;
namespace BotState {constexpr int BOT_STATE_COMBAT=1;}
enum class ActionThreatType {ACTION_THREAT_NONE,ACTION_THREAT_SINGLE,ACTION_THREAT_AOE};
struct Action {virtual ~Action(){};virtual std::string getName(){return "action";}virtual ActionThreatType GetThreatType(){return ActionThreatType::ACTION_THREAT_AOE;}};
struct CastSpellAction:Action {std::string name;std::string getName() override{return name;}};
struct CastHealingSpellAction:CastSpellAction {};
struct ShieldAction:CastSpellAction {ActionThreatType GetThreatType() override{return ActionThreatType::ACTION_THREAT_NONE;}};
struct Bot {void SendPlaySpellVisual(int){}};
struct AI {bool unsafe=true,neglect=false,group=true,high=true;int aoe=100;Bot bot;
 bool HasStrategy(std::string,int){return false;}Bot* GetMaster(){return nullptr;}Bot* GetBot(){return &bot;}
 int value(std::string n){return n=="neglect threat"?neglect:n=="group"?group:n=="aoe"?aoe:n=="high threat"?high:0;}};
bool RaidHealerShouldHoldDamage(AI* ai){return ai->unsafe;}
#define AI_VALUE(type,name) static_cast<type>(ai->value(name))
#define AI_VALUE2(type,name,qualifier) static_cast<type>(ai->value(qualifier))
struct Multiplier {AI* ai;float GetValue(Action* action) BODY};
int main(){
 AI ai;Multiplier m{&ai};CastHealingSpellAction heal;ShieldAction shield;CastSpellAction damage;shield.name="power word: shield";damage.name="holy fire";
 assert(m.GetValue(&heal)==1);assert(m.GetValue(&shield)==1); // no tank threat: AOE=100
 assert(m.GetValue(&damage)==0); // no general threat bypass
 ai.high=false;ai.aoe=50;assert(m.GetValue(&heal)==1);
 ai.unsafe=false;assert(m.GetValue(&heal)==1);assert(m.GetValue(&shield)==1);
 ai.aoe=49;assert(m.GetValue(&heal)==1);
 ai.neglect=true;ai.aoe=100;assert(m.GetValue(&damage)==1);
 assert(m.GetValue(nullptr)==1);
}
'''.replace('BODY',body(source,'float ThreatMultiplier::GetValue')))

    def test_assigned_tank_cast_gates(self):
        policy=(ROOT/'ai/playerbot/HealingTriagePolicy.h').read_text()
        policy = policy.replace('#include "HealerManaPolicy.h"', '#include "' + str(ROOT/'ai/playerbot/HealerManaPolicy.h') + '"')
        mana=(ROOT/'ai/playerbot/HealerManaPolicy.h').read_text()
        source=(ROOT/'ai/playerbot/strategy/actions/GenericSpellActions.cpp').read_text()
        run(policy+r'''
#include <cassert>
using namespace ai;
using uint8=unsigned char;using uint32=unsigned;
struct Unit {float hp=89;bool player=true,assigned=true;virtual ~Unit(){};
 unsigned GetMaxHealth(){return 100;}unsigned GetHealth(){return hp;}
 bool IsPlayer(){return player;}float GetHealthPercent(){return hp;}};
struct Player:Unit {};
struct AI {unsigned incoming=0;bool IsTank(Player*,bool){return true;}};
unsigned GetIncomingHeal(AI* ai,Unit*){return ai->incoming;}
struct {int lowHealth=50,mediumHealth=70,mediumMana=50,almostFullHealth=90;} sPlayerbotAIConfig;
bool IsPatchwerkHealingAssignment(Player* p){return p->assigned;}
#define AI_VALUE2(type,name,qualifier) static_cast<type>(90)
struct CastHealingSpellAction {bool valid=true;bool isUseful(){return valid;}};
struct HealPartyMemberAction:CastHealingSpellAction {AI* ai;Unit* target;uint8 estAmount=15;HealManaEfficiency manaEfficiency=HealManaEfficiency::LOW;
 unsigned overrideId=9,downrank=0;void ClearSpellIdOverride(){overrideId=0;}unsigned DownrankSpellId(Unit*){return downrank;}
 Unit* GetTarget(){return target;}bool isUseful() BODY};
int main(){
 HealingBands bands{20,50,70,90};HealingCandidate tank{1,89,100,0,true,false,false,true,true,true};
 assert(FastTankHealing(tank,bands));tank.health=90;assert(!FastTankHealing(tank,bands));
 tank.health=89;tank.patchwerkAssigned=false;assert(!FastTankHealing(tank,bands));
 tank.patchwerkAssigned=true;tank.inCombat=false;assert(!FastTankHealing(tank,bands));
 AI ai;Player patient;HealPartyMemberAction action;action.ai=&ai;action.target=&patient;
 assert(action.isUseful());assert(action.overrideId==0); // Flash Heal isn't conservation-vetoed on assigned tank at 89%
 action.valid=false;assert(!action.isUseful());action.valid=true; // actual cast validity remains
 patient.assigned=false;assert(!action.isUseful());patient.assigned=true;
 patient.hp=90;assert(!action.isUseful());patient.hp=40;assert(action.isUseful());
 patient.assigned=false;patient.hp=89;action.downrank=1;assert(action.isUseful());
 patient.hp=100;assert(!action.isUseful());
 action.target=nullptr;assert(!action.isUseful());
}
'''.replace('BODY',body(source,'bool HealPartyMemberAction::isUseful()')))
        trigger=(ROOT/'ai/playerbot/strategy/triggers/HealthTriggers.cpp').read_text()
        self.assertIn('target->IsInCombat(), IsPatchwerkHealingAssignment',body(trigger,'bool PartyMemberTankDangerTrigger::IsActive'))
if __name__=='__main__':unittest.main()
