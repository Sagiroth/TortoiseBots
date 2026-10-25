"""Execute production Fankriss eligibility, election and taunt arbitration.
World/spell readiness is stubbed; native compilation checks the real API seam.
--root can point at the frozen stack to reproduce its missing role/rotation.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

class FankrissTanks(unittest.TestCase):
    def test_rotation_and_scope(self):
        self.assertIsNotNone(shutil.which('g++'), 'C++ compiler required')
        path = ROOT / 'ai/playerbot/strategy/actions/FankrissTankActions.cpp'
        source = path.read_text() if path.exists() else ''
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <string>
#include <map>
#include <mutex>
#include <list>
#include <vector>
#include "ai/playerbot/EbonrocSwapPolicy.h"
using uint32 = std::uint32_t; using uint64 = std::uint64_t;
constexpr int EFFECT_INDEX_0=0;
namespace BotState { constexpr int BOT_STATE_COMBAT=0; }
struct ObjectGuid {
    uint64 id=0; uint64 GetRawValue() const { return id; }
    bool operator<(ObjectGuid b) const { return id<b.id; }
};
struct Group;
struct Aura { unsigned stacks=0; unsigned GetStackAmount() const {return stacks;} };
struct Unit {
    virtual ~Unit()=default;
    ObjectGuid guid{100}; uint32 map=531, instance=104, entry=15510;
    bool alive=true,inWorld=true,combat=true,player=false,creature=true,valid=true;
    Unit* victim=nullptr; Aura aura;
    bool IsAlive(){return alive;} bool IsInWorld(){return inWorld;}
    bool IsInCombat(){return combat;} bool IsInMap(Unit* b){return b && map==b->map && instance==b->instance;}
    bool IsPlayer(){return player;} bool IsCreature(){return creature;}
    uint32 GetEntry(){return entry;} uint32 GetInstanceId(){return instance;}
    ObjectGuid GetObjectGuid(){return guid;} Unit* GetVictim(){return victim;}
    Aura* GetAura(int id,int){return id==25646 && aura.stacks ? &aura : nullptr;}
};
struct Creature : Unit { bool evade=false; bool IsInEvadeMode(){return evade;} };
struct Player : Unit {
    Player(){player=true;creature=false;entry=0;}
    bool tank=true,enabled=true,teleport=false; uint32 spell=355;
    Group* group=nullptr;
    uint32 GetMapId(){return map;} Group* GetGroup(){return group;}
    bool IsBeingTeleported(){return teleport;}
    bool IsValidAttackTarget(Unit* u){return u->valid;}
};
struct Group { std::vector<Player*> members; };
struct AiObjectContext {};
namespace ai {struct PlayerbotAI; Unit* GetAssignedFankrissTankTarget(PlayerbotAI*);}
namespace ai {
struct PlayerbotAI {
    Player* bot; bool manual=false; std::list<ObjectGuid> attackers{{100}};
    Player* GetBot(){return bot;} bool IsTank(Player* p){return p->tank;}
    bool HasStrategy(char const*,int){return bot->enabled;}
    AiObjectContext* GetAiObjectContext(){return nullptr;}
    Unit* GetUnit(ObjectGuid);
};
}
using namespace ai;
Creature boss, add;
Unit* PlayerbotAI::GetUnit(ObjectGuid g){return g.id==100 ? &boss : nullptr;}
struct PlayerbotAIStorage {
    std::map<Player*,PlayerbotAI*> ais;
    static PlayerbotAIStorage& Instance(){static PlayerbotAIStorage s; return s;}
    PlayerbotAI* GetAI(Player* p){return ais[p];}
};
auto LiveGroupMembers(Group* g){return g->members;}
struct Config { unsigned fankrissSwapStacks=5; } sPlayerbotAIConfig;
uint32 now=10000;
struct WorldTimer { static uint32 getMSTime(){return now;} };
#define AI_VALUE(type,key) (ai->attackers)
bool OrderedElsewhere(PlayerbotAI* ai, Unit*) {return ai->manual;}
uint32 ReadyTaunt(PlayerbotAI* ai, Unit*) {return ai->GetBot()->spell;}
struct Action {
    std::string name; Unit* target;
    std::string getName(){return name;} Unit* GetTarget(){return target;}
};
struct Multiplier {
    PlayerbotAI* ai; Multiplier(PlayerbotAI* a,char const*):ai(a){}
    virtual float GetValue(Action*)=0;
};
'''
        if source:
            fixture += '#include "ai/playerbot/FankrissTankPolicy.h"\n'
            fixture += function(source, 'bool Assigned(')
            fixture += function(source, 'unsigned Wounds(')
            fixture += function(source, 'Unit* ai::GetAssignedFankrissTankTarget(')
            fixture += source[source.index('struct SwapRecord'):source.index('class FankrissTauntMultiplier')]
            fixture += function(source, 'class FankrissTauntMultiplier') + ';\n'
        else:
            # Frozen stack has no Fankriss role or stack-driven request.
            fixture += '''
struct SwapRequest { Unit* boss=nullptr; uint32 spell=0; };
SwapRequest RequestSwap(PlayerbotAI*,bool){return {};}
Unit* ai::GetAssignedFankrissTankTarget(PlayerbotAI*){return nullptr;}
struct FankrissTauntMultiplier : Multiplier {
 using Multiplier::Multiplier;
 FankrissTauntMultiplier(PlayerbotAI* a):Multiplier(a,"baseline"){}
 float GetValue(Action*) override {return 1.0f;}
};
'''
        fixture += r'''
int main(){
 Group raid,other;
 Player a,b,c,d; a.guid={1};b.guid={2};c.guid={3};d.guid={4};d.tank=false;
 for(auto p:{&a,&b,&c,&d})p->group=&raid;
 raid.members={&c,&b,&a,&d}; // election must not depend on roster order
 PlayerbotAI aa{&a},ab{&b},ac{&c},ad{&d};
 auto& store=PlayerbotAIStorage::Instance().ais;store={{&a,&aa},{&b,&ab},{&c,&ac},{&d,&ad}};
 c.enabled=false;boss.victim=&a;
 a.aura.stacks=4; assert(!RequestSwap(&ab,false).boss);
 a.aura.stacks=5;
 // Live failure: main at five wounds, clean off-tank, no automatic handoff.
 assert(RequestSwap(&ab,false).boss==&boss);
 assert(RequestSwap(&ab,true).spell==355);
 assert(!RequestSwap(&ab,true).boss); // one attempt even if first cast fails
 now+=1999;assert(!RequestSwap(&ab,true).boss);
 now+=1;assert(RequestSwap(&ab,true).boss==&boss); // resisted/cooldown retry
 boss.victim=&b;assert(!RequestSwap(&aa,false).boss); // wounded outgoing tank
 now+=40000;b.aura.stacks=5;
 assert(!RequestSwap(&aa,false).boss); // must actually lose the aura
 a.aura.stacks=0;
 assert(RequestSwap(&aa,true).boss==&boss); // swap back
 boss.victim=&a;a.aura.stacks=0;assert(!RequestSwap(&ab,false).boss);
 // Three tanks: exactly one ready zero-stack volunteer, even before victim updates.
 now+=40000;a.aura.stacks=5;b.aura.stacks=0;c.enabled=true;
 assert(!RequestSwap(&ac,true).boss);assert(RequestSwap(&ab,true).boss);
 b.spell=0;assert(!RequestSwap(&ac,true).boss); // guard survives readiness change
 now+=2000;assert(RequestSwap(&ac,true).boss);boss.victim=&c;
 assert(!RequestSwap(&ab,false).boss); // third tank never taunts a healthy tank
 // Off-tanks keep selecting the boss and their ordinary damage is never throttled.
 assert(GetAssignedFankrissTankTarget(&aa)==&boss);
 FankrissTauntMultiplier gate(&aa);
 for(auto name:{"taunt","taunt on snare target","growl","mocking blow","challenging shout","challenging roar"}){
  Action action{name,&boss};assert(gate.GetValue(&action)==0);
 }
 for(auto name:{"sunder armor","shield slam","revenge","heroic strike","melee","fankriss swap"}){
  Action action{name,&boss};assert(gate.GetValue(&action)==1);
 }
 Action addTaunt{"taunt",&add};assert(gate.GetValue(&addTaunt)==1);
 // Custom threshold and no safe replacement.
 now+=2000;boss.victim=&c;c.aura.stacks=3;a.aura.stacks=0;
 sPlayerbotAIConfig.fankrissSwapStacks=4;assert(!RequestSwap(&aa,false).boss);
 sPlayerbotAIConfig.fankrissSwapStacks=3;assert(RequestSwap(&aa,false).boss);
 a.aura.stacks=1;assert(!RequestSwap(&aa,false).boss);a.aura.stacks=0;
 // Scope: disabled/dead/teleported/non-tank/other map/instance/manual orders.
 a.enabled=false;assert(!GetAssignedFankrissTankTarget(&aa));a.enabled=true;
 a.alive=false;assert(!GetAssignedFankrissTankTarget(&aa));a.alive=true;
 a.inWorld=false;assert(!GetAssignedFankrissTankTarget(&aa));a.inWorld=true;
 a.teleport=true;assert(!GetAssignedFankrissTankTarget(&aa));a.teleport=false;
 a.tank=false;assert(!GetAssignedFankrissTankTarget(&aa));a.tank=true;
 a.map=509;assert(!GetAssignedFankrissTankTarget(&aa));a.map=531;
 a.instance=105;assert(!GetAssignedFankrissTankTarget(&aa));a.instance=104;
 aa.manual=true;assert(!GetAssignedFankrissTankTarget(&aa));aa.manual=false;
 boss.combat=false;assert(!GetAssignedFankrissTankTarget(&aa));boss.combat=true;
 boss.alive=false;assert(!GetAssignedFankrissTankTarget(&aa));boss.alive=true;
 boss.evade=true;assert(!GetAssignedFankrissTankTarget(&aa));boss.evade=false;
 boss.entry=15348;assert(!GetAssignedFankrissTankTarget(&aa));boss.entry=15510;
 c.group=&other;assert(!GetAssignedFankrissTankTarget(&aa));c.group=&raid;
 // Emergency rescue from a non-tank, still single volunteer; no DPS hold.
 boss.victim=&d;assert(RequestSwap(&aa,true).boss==&boss);
 assert(!GetAssignedFankrissTankTarget(&ad));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp=Path(tmp)/'fankriss.cpp'; exe=Path(tmp)/'fankriss'
            cpp.write_text(fixture)
            subprocess.run(['g++','-std=c++17','-I',str(ROOT),str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_runtime_wiring(self):
        root=ROOT/'ai/playerbot'
        strategy=(root/'strategy/StrategyContext.h').read_text()
        self.assertIn('creators["fankriss tank"]',strategy)
        source=(root/'strategy/actions/FankrissTankActions.cpp').read_text()
        ready=function(source,'uint32 ReadyTaunt(')
        for check in ['MissingStance','HasSpell','IsImmuneToSpellEffect','CanCastSpell']:
            self.assertIn(check,ready)
        request=function(source,'SwapRequest RequestSwap(')
        self.assertLess(request.index('lock(swapMutex)'),request.index('record.window.Claim'))
        execute=function(source,'bool FankrissSwapAction::Execute(')
        self.assertLess(execute.index('RequestSwap(ai, true)'),execute.index('ai->CastSpell'))
        engage=function(source,'bool FankrissTankEngageAction::Execute(')
        self.assertIn('MissingStance',engage);self.assertIn('Attack(GetMaster(), boss)',engage)
        selector=(root/'strategy/values/TankTargetValue.cpp').read_text()
        body=function(selector,'Unit* TankTargetValue::Calculate(')
        self.assertLess(body.index('GetExplicitAttackTarget'),body.index('GetAssignedFankrissTankTarget'))
        self.assertLess(body.index('GetAssignedFankrissTankTarget'),body.index('FindTankTargetSmartStrategy strategy'))
        self.assertIn('ai->IsTank(bot)',(root/'strategy/values/FankrissSpawnTarget.cpp').read_text())
        for path,names in [('strategy/actions/ActionContext.h',['fankriss swap','fankriss tank engage']),
                           ('strategy/triggers/TriggerContext.h',['fankriss swap needed','fankriss tank engage needed'])]:
            text=(root/path).read_text()
            for name in names:self.assertIn('creators["'+name+'"]',text)
        self.assertIn('GetIntDefault("AiPlayerbot.FankrissSwapStacks", 5)',(root/'PlayerbotAIConfig.cpp').read_text())
        self.assertIn('AiPlayerbot.FankrissSwapStacks = 5',(ROOT/'conf/tortoise_bots.conf.dist').read_text())

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--root',type=Path)
    args,remaining=parser.parse_known_args()
    if args.root:ROOT=args.root
    unittest.main(argv=[__file__]+remaining)
