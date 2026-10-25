"""Production selectors/CC gate with core API boundaries; no simulated boss fight."""
import os
from pathlib import Path
import unittest
from test_raid_tank_healing import body, run
ROOT=Path(os.environ.get('SOAKER_SHIELD_SOURCE',Path(__file__).resolve().parents[1]))
class SoakerPreshieldTest(unittest.TestCase):
    extra_cases = ""

    def test_production_selection_and_cc(self):
        source=(ROOT/'ai/playerbot/RaidTankHealing.cpp').read_text()
        policy=(ROOT/'ai/playerbot/HealingTriagePolicy.h').read_text()
        # Baseline's only shield patient route is the normal healing selector;
        # use its real eligibility policy for the full-health regression case.
        select=body(source,'Unit* PatchwerkSoakerShieldValue::Calculate') if 'Unit* PatchwerkSoakerShieldValue::Calculate' in source else '''{
          ai::HealingCandidate c{2,100,100,0,true,false,false,true,true,true};
          return ai::NeedsPartyHealing(c,ai::HealingBands{20,50,70,90})?context->patient:nullptr;
        }'''
        # Baseline CC_TRIGGER is the unqualified HasCcTargetTrigger; a legal
        # unmarked raid CC candidate has no boss-fight suppression at all.
        cc=body(source,'bool ai::SuppressAutomaticRaidShackle') if 'bool ai::SuppressAutomaticRaidShackle' in source else '{return false;}'
        run(policy+r'''
#include <cassert>
#include <string>
using uint32=unsigned;
namespace BotState {constexpr int BOT_STATE_COMBAT=1;}
struct Map {bool raid=true;bool IsRaid(){return raid;}};
struct Unit {bool alive=true,combat=true,same=true;float hp=100;unsigned guid=1;
 bool IsAlive(){return alive;}bool IsInCombat(){return combat;}bool IsInMap(Unit*){return same;}
 float GetHealthPercent(){return hp;}unsigned GetHealth(){return unsigned(hp);}unsigned GetObjectGuid(){return guid;}};
struct PlayerbotAI;struct Group;
struct Player:Unit {bool tank=false,assigned=false;bool world=true,friendly=true,los=true,weak=false,shield=false,claimed=false;float distance=10;unsigned mapid=533;
 Map map;Group* group=nullptr;PlayerbotAI* peer=nullptr;
 bool IsInWorld(){return world;}Map* GetMap(){return &map;}unsigned GetMapId(){return mapid;}
 Group* GetGroup(){return group;}bool IsWithinDistInMap(Player* p,float r){return p->distance<=r;}};
struct Group {bool raid=true;std::vector<Player*> members;bool IsRaidGroup(){return raid;}};
struct AiObjectContext {Unit* boss=nullptr;Unit* mark=nullptr;Unit* patient=nullptr;
 Unit* value(std::string n){return n=="boss target"?boss:n=="rti cc target"?mark:patient;}};
struct PlayerbotAI {Player* bot;bool healer=true,focus=false,engagedSoaker=false,trained=true;AiObjectContext context;
 Player* GetBot(){return bot;}AiObjectContext* GetAiObjectContext(){return &context;}bool IsHeal(Player*){return healer;}
 bool IsTank(Player* p){return p->tank;}
 bool HasStrategy(std::string,int){return focus;}bool GetSpellRange(std::string,float* r){*r=40;return trained;}
 bool HasAura(int,Player* p){return p->weak;}bool HasAura(std::string,Player* p){return p->shield;}};
struct PlayerbotAIStorage {static PlayerbotAIStorage& Instance(){static PlayerbotAIStorage s;return s;}PlayerbotAI* GetAI(Player* p){return p->peer;}};
struct {int criticalHealth=20,almostFullHealth=90;} sPlayerbotAIConfig;
struct {bool IsFriendlyTo(Player*,Player* p){return p->friendly;}bool IsWithinLOSInMap(Player*,Player* p){return p->los;}} sServerFacade;
struct BuffClaimRegistry {static bool IsTargetClaimedByOther(Player*,Player* p,std::string){return p->claimed;}};
std::vector<Player*> LiveGroupMembers(Group* g){return g->members;}
bool IsPatchwerkHealingAssignment(Player* p){return p->assigned && p->combat;}
Unit* GetPatchwerkSoakBoss(PlayerbotAI* peer){return peer->engagedSoaker?peer->context.boss:nullptr;}
#define AI_VALUE(type,name) static_cast<type>(context->value(name))
bool SuppressAutomaticRaidShackle(PlayerbotAI* ai,Unit* target,bool explicitRequest) CC
struct Value {PlayerbotAI* ai;Player* bot;AiObjectContext* context;Unit* Calculate() SELECT};
int main(){
 Player healer,a,b;Group g;healer.group=&g;g.members={&a,&b};a.guid=2;b.guid=3;
 PlayerbotAI ai{&healer},pa{&a},pb{&b};a.peer=&pa;b.peer=&pb;Unit boss;
 pa.engagedSoaker=pb.engagedSoaker=true;pa.context.boss=pb.context.boss=&boss;ai.context.boss=&boss;
 a.combat=b.combat=false; // group engaged: they have not reached melee or taken damage
 Value v{&ai,&healer,&ai.context};
 assert(v.Calculate()==&a);a.claimed=true;assert(v.Calculate()==&b);
 a.claimed=false;a.weak=true;assert(v.Calculate()==&b);a.weak=false;a.shield=true;assert(v.Calculate()==&b);
 a.shield=false;a.distance=41;assert(v.Calculate()==&b);a.distance=10;a.los=false;assert(v.Calculate()==&b);
 a.los=true;a.alive=false;assert(v.Calculate()==&b);a.alive=true;a.hp=90;assert(v.Calculate()==&b);a.hp=100;
 ai.focus=true;assert(!v.Calculate());ai.focus=false;
 Unit critical;critical.hp=19;ai.context.patient=&critical;assert(!v.Calculate());ai.context.patient=nullptr;
 pa.engagedSoaker=pb.engagedSoaker=false;assert(!v.Calculate());pa.engagedSoaker=pb.engagedSoaker=true;
 EXTRA_CASES
 healer.mapid=1;assert(!v.Calculate());healer.mapid=533;
 assert(SuppressAutomaticRaidShackle(&ai,&a,false)); // baseline lets automatic Shackle through
 assert(!SuppressAutomaticRaidShackle(&ai,&a,true));
 ai.context.mark=&a;assert(!SuppressAutomaticRaidShackle(&ai,&a,false));
 assert(SuppressAutomaticRaidShackle(&ai,&b,false));ai.context.mark=nullptr;
 boss.combat=false;assert(!SuppressAutomaticRaidShackle(&ai,&a,false));boss.combat=true;
 boss.alive=false;assert(!SuppressAutomaticRaidShackle(&ai,&a,false));boss.alive=true;
 healer.map.raid=false;assert(!SuppressAutomaticRaidShackle(&ai,&a,false));
}
'''.replace('EXTRA_CASES',self.extra_cases).replace('SELECT',select).replace('CC',cc))

    def test_action_wiring(self):
        actions=(ROOT/'ai/playerbot/strategy/priest/PriestActions.h').read_text()
        triggers=(ROOT/'ai/playerbot/strategy/priest/PriestTriggers.h').read_text()
        self.assertIn('SuppressAutomaticRaidShackle(ai, GetTarget(), event.getOwner() != nullptr)',actions)
        self.assertIn('SuppressAutomaticRaidShackle(ai, target, false)',triggers)
        shield=actions.split('class ShieldPatchwerkSoakerAction',1)[1].split('SPELL_ACTION_U',1)[0]
        self.assertIn('public CastBuffSpellAction',shield)
        self.assertNotIn('HealPartyMemberAction::isUseful()',shield)
        self.assertIn('GetReachActionName() override { return ""; }',shield)
        self.assertIn('ACTION_THREAT_NONE',shield)
        strategy=(ROOT/'ai/playerbot/strategy/priest/PriestStrategy.cpp').read_text()
        for state in ['Combat','NonCombat']:
            section=strategy.split('void PriestStrategy::Init'+state+'Triggers',1)[1].split('\nvoid ',1)[0]
            self.assertIn('"shield patchwerk soaker", ACTION_CRITICAL_HEAL - 1',section)
if __name__=='__main__':unittest.main()
