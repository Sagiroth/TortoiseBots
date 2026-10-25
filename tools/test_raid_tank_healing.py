"""Production triage and raid guard bodies with group/spell API doubles."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path(os.environ.get('RAID_HEAL_SOURCE',ROOT))
def body(s,marker):
    start=s.index('{',s.index(marker));end=start+1;depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]
def run(cpp):
    with tempfile.TemporaryDirectory() as d:
        src=Path(d)/'test.cpp';exe=Path(d)/'test';src.write_text(cpp)
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-I',str(ROOT),str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
@unittest.skipUnless(shutil.which('g++'),'g++ unavailable')
class RaidTankHealingTest(unittest.TestCase):
    def test_assignment(self):
        s=(SOURCE/'ai/playerbot/HealingTriagePolicy.h').read_text()
        if 'bool patchwerkAssigned' not in s:
            s=s.replace('bool inCombat = false;','bool inCombat = false; bool patchwerkAssigned = false;')
        run(s+r'''
#include <cassert>
using namespace ai;
int main(){
 HealingBands b{20,50,70,90};
 HealingCandidate main{1,40,100,0,true,false,false,true,true,true};
 auto soak=main;soak.guid=2;soak.health=80;soak.incomingHeal=true;
 assert(NeedsPartyHealing(soak,b)); // a pending heal does not guarantee safety
 HealingCandidate dps{3,10,100,0,false,false,false,true,true,false};
 std::vector<HealingCandidate> v{dps,soak,main};
 auto sort=[&]{std::sort(v.begin(),v.end(),[&](auto a,auto c){return HealingPriority(a,c,b);});};
 sort();assert(v.front().guid==1);
 assert(v[HealingAssignment(v,b,0)].guid==1);
 assert(v[HealingAssignment(v,b,1)].guid==2); // do not starve a noncritical soaker
 soak.health=10;v={main,soak,dps};sort();
 for(unsigned i=0;i<8;++i)assert(v[HealingAssignment(v,b,i)].guid==2);
 soak.health=90;assert(!NeedsPartyHealing(soak,b));
 soak.health=80;soak.patchwerkAssigned=false;assert(!NeedsPartyHealing(soak,b));
 soak.patchwerkAssigned=true;soak.inCombat=false;assert(!NeedsPartyHealing(soak,b));
 soak.inCombat=true;soak.health=0;assert(!NeedsPartyHealing(soak,b));
}
''')
    def test_action_and_update_wiring(self):
        actions=(SOURCE/'ai/playerbot/strategy/actions/GenericSpellActions.cpp').read_text()
        for marker in ['bool CastSpellAction::isUseful()', 'bool CastSpellAction::Execute(', 'bool CastShootAction::Execute(']:
            self.assertIn('RaidHealerShouldHoldDamage(ai)',body(actions,marker))
        selector=(SOURCE/'ai/playerbot/strategy/values/PartyMemberToHeal.cpp').read_text()
        stable=selector.split('// A cast starting must not renumber',1)[1].split('++assignedHealerOrdinal;',1)[0]
        self.assertNotIn('IsNonMeleeSpellCasted',stable)
        self.assertIn('candidates.front().patchwerkAssigned ? assignedHealerOrdinal : healerOrdinal',selector)
        update=(SOURCE/'ai/playerbot/PlayerbotAI.cpp').read_text()
        self.assertLess(update.index('StopUnsafeRaidHealerFiller(this)'),update.index('if(!UpdateAIReaction('))
        self.assertIn('SetAIInternalUpdateDelay(0)',update[update.index('StopUnsafeRaidHealerFiller(this)'):][:130])

    def test_live_guards(self):
        p=SOURCE/'ai/playerbot/RaidTankHealing.cpp'
        self.assertTrue(p.exists(),'predecessor has no independent raid tank guard')
        s=p.read_text()
        cpp=r'''
#include <cassert>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
constexpr int CURRENT_GENERIC_SPELL=0,CURRENT_AUTOREPEAT_SPELL=1,SPELL_STATE_PREPARING=1;
namespace BotState {constexpr int BOT_STATE_COMBAT=1;}
struct Info {const char* SpellName[1];};
struct Spell {Info* m_spellInfo;int state=1;int getState(){return state;}};
struct Map {bool raid=true;bool IsRaid(){return raid;}};
struct PlayerbotAI;
struct Player;
struct Group {bool raid=true;std::vector<Player*> members;bool IsRaidGroup(){return raid;}};
struct Unit {bool alive=true;unsigned entry=16028;bool IsAlive(){return alive;}unsigned GetEntry(){return entry;}};
struct Player:Unit {bool world=true,combat=true,same=true,friendly=true,tank=true;unsigned mapid=533;float hp=80;
 Map map;Group* group=nullptr;Unit* victim=nullptr;PlayerbotAI* ai=nullptr;Spell* spells[2]={};int interrupts=0;
 bool IsInWorld(){return world;}bool IsInCombat(){return combat;}bool IsInMap(Player*){return same;}
 unsigned GetMapId(){return mapid;}Unit* GetVictim(){return victim;}Group* GetGroup(){return group;}
 Map* GetMap(){return &map;}float GetHealthPercent(){return hp;}
 Spell* GetCurrentSpell(int slot){return spells[slot];}void InterruptSpell(int slot){spells[slot]=nullptr;++interrupts;}};
struct PlayerbotAI {Player* bot;bool healer=true,main=false,soak=false,hold=true;
 Player* GetBot(){return bot;}bool IsHeal(Player*){return healer;}bool IsTank(Player* p){return p->tank;}
 bool HasStrategy(std::string s,int){return s=="patchwerk main"?main:soak;}};
struct PlayerbotAIStorage {static PlayerbotAIStorage& Instance(){static PlayerbotAIStorage s;return s;}
 PlayerbotAI* GetAI(Player* p){return p->ai;}};
std::vector<Player*> LiveGroupMembers(Group* g){return g->members;}
struct {bool IsFriendlyTo(Player*,Player* p){return p->friendly;}} sServerFacade;
struct {float almostFullHealth=90;} sPlayerbotAIConfig;
bool RaidHealerShouldHoldDamage(PlayerbotAI* ai){return ai->hold;}
bool IsPatchwerkHealingAssignment(Player* player) ASSIGN
bool IsRaidHealerFiller(std::string name) FILLER
bool StopUnsafeRaidHealerFiller(PlayerbotAI* ai) STOP
struct Value {Player* bot;PlayerbotAI* ai;bool Calculate() CALCULATE};
int main(){
 Player healer,tank;PlayerbotAI ai{&healer},peer{&tank};Group group;group.members={&tank};healer.group=&group;
 Value v{&healer,&ai};Unit boss;tank.victim=&boss;tank.ai=&peer;peer.soak=true;tank.tank=false;
 assert(IsPatchwerkHealingAssignment(&tank));assert(v.Calculate());
 peer.soak=false;peer.main=true;assert(v.Calculate());peer.main=false;assert(!v.Calculate());
 tank.tank=true;assert(v.Calculate());tank.hp=90;assert(!v.Calculate());tank.hp=80;
 tank.combat=false;assert(!v.Calculate());tank.combat=true;tank.alive=false;assert(!v.Calculate());
 tank.alive=true;tank.same=false;assert(!v.Calculate());tank.same=true;tank.friendly=false;assert(!v.Calculate());
 tank.friendly=true;group.raid=false;assert(!v.Calculate());group.raid=true;ai.healer=false;assert(!v.Calculate());ai.healer=true;
 healer.map.raid=false;assert(!v.Calculate());healer.map.raid=true;
 peer.soak=true;boss.entry=1;assert(!IsPatchwerkHealingAssignment(&tank));boss.entry=16028;
 tank.mapid=1;assert(!IsPatchwerkHealingAssignment(&tank));tank.mapid=533;
 assert(IsRaidHealerFiller("Smite"));assert(IsRaidHealerFiller("Holy Fire"));assert(IsRaidHealerFiller("Shoot"));
 assert(IsRaidHealerFiller("Shadow Word: Pain"));assert(!IsRaidHealerFiller("Flash Heal"));assert(!IsRaidHealerFiller("fade"));
 Info damage{{"Smite"}},heal{{"Flash Heal"}};Spell cast{&damage},wand{&damage};healer.spells[0]=&cast;healer.spells[1]=&wand;
 assert(StopUnsafeRaidHealerFiller(&ai));assert(healer.interrupts==2);
 cast.m_spellInfo=&heal;healer.spells[0]=&cast;assert(!StopUnsafeRaidHealerFiller(&ai));assert(healer.spells[0]==&cast);
 cast.m_spellInfo=&damage;ai.hold=false;assert(!StopUnsafeRaidHealerFiller(&ai));
 ai.hold=true;cast.state=2;assert(!StopUnsafeRaidHealerFiller(&ai));
}
'''
        for key,marker in [('ASSIGN','bool ai::IsPatchwerkHealingAssignment'),('FILLER','bool ai::IsRaidHealerFiller'),('STOP','bool ai::StopUnsafeRaidHealerFiller'),('CALCULATE','bool RaidTankHealingValue::Calculate')]:cpp=cpp.replace(key,body(s,marker))
        run(cpp)
if __name__=='__main__':unittest.main()
