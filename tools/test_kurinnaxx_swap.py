"""Compile the production handoff coordinator with bounded world stubs."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class KurinnaxxSwapTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_coordinator_and_hold(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / 'ai/playerbot/strategy/actions/KurinnaxxActions.cpp').read_text()
        begin = source.index('struct SwapRecord')
        end = source.index('bool ShouldHold', begin)
        coordinator = source[begin:end]
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>
#include "ai/playerbot/EbonrocSwapPolicy.h"
#include "ai/playerbot/GarrAoePolicy.h"
#include "ai/playerbot/strategy/KurinnaxxSwapPolicy.h"
using namespace ai;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
constexpr int EFFECT_INDEX_0 = 0;
struct Guid {
    uint64 id;
    uint64 GetRawValue() const { return id; }
    bool operator<(Guid b) const { return id < b.id; }
};
struct Aura { int applied = 1; int GetAuraApplyTime() const { return applied; } };
struct Group;
namespace BotState { constexpr int BOT_STATE_COMBAT=0; }
struct Unit {
    Guid guid{100};
    uint32 instance = 1;
    Guid GetObjectGuid() const { return guid; }
    uint32 GetInstanceId() const { return instance; }
};
struct Player : Unit {
    unsigned stacks = 0;
    bool eligible = true;
    bool alive=true, inWorld=true, teleporting=false, combat=true, tank=true;
    bool enabled=true, hold=false, manual=false;
    uint32 map=509;
    uint32 GetMapId() { return map; }
    bool IsInWorld() { return inWorld; }
    bool IsAlive() { return alive; }
    bool IsBeingTeleported() { return teleporting; }
    bool IsInCombat() { return combat; }
    bool IsInMap(Unit* b) { return b && instance==b->instance; }
    uint32 readySpell = 355;
    Aura aura;
    Group* group = nullptr;
    Aura* GetAura(int, int) { return stacks ? &aura : nullptr; }
    Group* GetGroup() { return group; }
};
struct Group { std::vector<Player*> members; };
struct PlayerbotAI {
    Player* bot;
    Player* GetBot() { return bot; }
    bool IsTank(Player* p) { return p->tank; }
    bool HasStrategy(char const*, int) { return bot->enabled; }
};
struct PlayerbotAIStorage {
    std::map<Player*, PlayerbotAI*> ais;
    static PlayerbotAIStorage& Instance() { static PlayerbotAIStorage s; return s; }
    PlayerbotAI* GetAI(Player* p) { return ais[p]; }
};
uint32 now = 10000;
struct WorldTimer { static uint32 getMSTime() { return now; } };
Unit boss;
Player* victim;
bool active = true;
Unit* ActiveBoss(PlayerbotAI*) { return active ? &boss : nullptr; }
Player* ParticipatingVictim(PlayerbotAI*, Unit* b) { return b && victim->eligible ? victim : nullptr; }
unsigned WoundStacks(Player* p) { return p ? p->stacks : 0; }
bool HoldOrder(PlayerbotAI* a) { return a->bot->hold || !a->bot->eligible; }
bool ManualOrder(PlayerbotAI* a, Unit*) { return a->bot->manual; }
uint32 ReadyTaunt(PlayerbotAI* a, Unit*) { return a->bot->readySpell; }
std::vector<Player*> LiveGroupMembers(Group* g) { return g->members; }
'''
        fixture += function(source, 'bool Participates(') + coordinator + function(source, 'bool ShouldHold(')
        fixture += r'''
enum class ActionThreatType { ACTION_THREAT_NONE, ACTION_THREAT_SINGLE, ACTION_THREAT_AOE };
struct Action {
    std::string name;
    Unit* target=nullptr;
    ActionThreatType threat=ActionThreatType::ACTION_THREAT_NONE;
    virtual ~Action()=default;
    std::string getName() { return name; }
    Unit* GetTarget() { return target; }
    ActionThreatType getThreatType() { return threat; }
};
struct AttackAction : Action {};
struct CastSpellAction : Action {};
struct CastHealingSpellAction : CastSpellAction {};
struct Multiplier {
    PlayerbotAI* ai;
    Multiplier(PlayerbotAI* a, char const*) : ai(a) {}
    virtual ~Multiplier()=default;
    virtual float GetValue(Action*)=0;
};
'''
        fixture += function(source, 'class KurinnaxxMultiplier :') + ';\n'
        fixture += r'''
int main() {
    Player oldTank, first, second;
    oldTank.guid.id=1; first.guid.id=2; second.guid.id=3;
    PlayerbotAI oldAI{&oldTank}, firstAI{&first}, secondAI{&second};
    Group group{{&oldTank,&first,&second}};
    for (Player* p : group.members) p->group=&group;
    auto& ais=PlayerbotAIStorage::Instance().ais;
    ais[&oldTank]=&oldAI; ais[&first]=&firstAI; ais[&second]=&secondAI;
    victim=&oldTank;
    for (unsigned stacks=0;stacks<3;++stacks) {
        oldTank.stacks=stacks;
        assert(!RequestSwap(&firstAI,false).boss);
    }
    oldTank.stacks=3;
    active=false; assert(!RequestSwap(&firstAI,false).boss); active=true;
    victim->eligible=false; assert(!RequestSwap(&firstAI,false).boss); victim->eligible=true;
    assert(!RequestSwap(&oldAI,false).boss);
    first.stacks=1;
    assert(!RequestSwap(&firstAI,false).boss);
    assert(RequestSwap(&secondAI,false).boss==&boss);
    first.stacks=0;
    first.eligible=false; assert(RequestSwap(&secondAI,false).boss==&boss); first.eligible=true;
    first.readySpell=0; assert(RequestSwap(&secondAI,false).boss==&boss); first.readySpell=355;
    for (bool* gate : {&first.alive,&first.inWorld,&first.combat,&first.tank,&first.enabled}) {
        *gate=false; assert(RequestSwap(&secondAI,false).boss==&boss); *gate=true;
    }
    for (bool* gate : {&first.teleporting,&first.hold,&first.manual}) {
        *gate=true; assert(RequestSwap(&secondAI,false).boss==&boss); *gate=false;
    }
    first.map=1; assert(RequestSwap(&secondAI,false).boss==&boss); first.map=509;
    first.instance=2; assert(RequestSwap(&secondAI,false).boss==&boss); first.instance=1;
    assert(!Participates(nullptr,&boss));
    assert(RequestSwap(&firstAI,false).spell==355);
    assert(!RequestSwap(&secondAI,false).boss); // deterministic single volunteer
    assert(RequestSwap(&firstAI,true).boss==&boss);
    assert(!RequestSwap(&firstAI,true).boss); // one claim, even before victim changes
    now+=1999; assert(!RequestSwap(&firstAI,false).boss);
    ++now; assert(RequestSwap(&firstAI,true).boss);
    now+=2000; assert(RequestSwap(&firstAI,true).boss);
    now+=2000; assert(!RequestSwap(&firstAI,false).boss); // bounded resisted attempts
    victim=&first; // success must be a real change of victim, not CastSpell's return
    assert(!RequestSwap(&secondAI,false).boss);
    assert(ShouldHold(&oldAI,&boss));
    assert(!ShouldHold(&firstAI,&boss));
    oldTank.stacks=0; assert(!ShouldHold(&oldAI,&boss));
    first.stacks=3;
    assert(RequestSwap(&oldAI,true).boss); // reverse handoff after aura expiration
    victim=&oldTank;
    assert(ShouldHold(&firstAI,&boss));
    victim->eligible=false; assert(!ShouldHold(&firstAI,&boss)); victim->eligible=true;
    first.stacks=0;
    oldTank.aura.applied=2;
    oldTank.stacks=3;
    assert(RequestSwap(&firstAI,true).boss); // new debuff window
    boss.instance=2;
    for (Player* p : group.members) p->instance=2;
    assert(RequestSwap(&firstAI,true).boss); // independent instance
    now+=60001;
    RequestSwap(&firstAI,false);
    assert(swaps.size()==1); // expired encounter record pruned

    assert(KurinnaxxBlockAction(true,false,false,true,false,true)); // no generic taunt racing
    assert(KurinnaxxBlockAction(true,false,true,false,true,true));
    assert(KurinnaxxBlockAction(false,true,false,true,true,true)); // boss attack/spell
    assert(KurinnaxxBlockAction(false,true,true,false,true,true)); // self-centered damage
    assert(!KurinnaxxBlockAction(false,true,false,false,true,true)); // heal/defensive self spell
    assert(!KurinnaxxBlockAction(false,false,false,true,true,true)); // coordinated plain Action
    assert(!KurinnaxxBlockAction(false,true,false,true,false,true)); // clean tank attacks
    assert(!KurinnaxxBlockAction(true,false,false,true,false,false)); // rescue nonparticipant

    victim=&first; first.stacks=0; oldTank.stacks=3;
    KurinnaxxMultiplier multiplier(&oldAI);
    AttackAction attack; attack.target=&boss;
    assert(multiplier.GetValue(&attack)==0);
    CastSpellAction spell; spell.target=&boss;
    assert(multiplier.GetValue(&spell)==0);
    spell.target=&oldTank; spell.name="shield block";
    assert(multiplier.GetValue(&spell)==1);
    for (char const* name : {"consecration","thunder clap","swipe","swipe (bear)","whirlwind"}) {
        spell.name=name; assert(multiplier.GetValue(&spell)==0);
    }
    CastHealingSpellAction heal; heal.target=&oldTank; heal.threat=ActionThreatType::ACTION_THREAT_AOE;
    assert(multiplier.GetValue(&heal)==1);
    Action coordinated; coordinated.target=&boss; coordinated.name="kurinnaxx swap";
    assert(multiplier.GetValue(&coordinated)==1);
    oldTank.stacks=0; assert(multiplier.GetValue(&attack)==1);
    spell.target=&boss; spell.name="taunt";
    assert(multiplier.GetValue(&spell)==0); // clean volunteer cannot race generic taunt
    victim=&oldTank; assert(multiplier.GetValue(&spell)==1); // current tank unchanged
    active=false; oldTank.stacks=3;
    assert(multiplier.GetValue(&attack)==1); // outside opt-in encounter

}
'''
        with tempfile.TemporaryDirectory(prefix='kurinnaxx-swap-') as tmp:
            src = pathlib.Path(tmp) / 'fixture.cpp'
            exe = pathlib.Path(tmp) / 'fixture'
            src.write_text(fixture)
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-pedantic',
                            '-I', str(root), str(src), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
