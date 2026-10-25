"""Production Sartura hazard/escape dispatch with world and movement boundaries."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest
from tools.test_nefarian_bone_target import function


class SarturaWhirlwindTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_live_hazards_and_dispatch(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / 'ai/playerbot/strategy/actions/SarturaWhirlwindActions.cpp').read_text()
        strategy = (root / 'ai/playerbot/strategy/generic/SarturaWhirlwindStrategy.cpp').read_text()
        production = '\n'.join(function(source, name) for name in (
            'std::list<HazardPosition> ai::GetSarturaWhirlwindHazards(',
            'bool ai::SarturaWhirlwindTooClose(',
            'bool SarturaWhirlwindEscapeAction::Execute('))
        production += '\n' + function(strategy, 'class SarturaWhirlwindMultiplier') + ';\n'
        fixture = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <list>
#include <map>
#include <set>
#include <string>
#include <utility>
using uint32 = std::uint32_t;
using ObjectGuid = unsigned;
struct Unit {
    unsigned entry=0, map=531;
    bool alive=true, combat=true, world=true;
    float x=0, y=0, z=0, leeway=0;
    std::set<unsigned> auras;
    bool IsAlive() const { return alive; }
    bool IsInCombat() const { return combat; }
    bool IsInWorld() const { return world; }
    bool IsInMap(Unit* b) const { return world && b->world && map==b->map; }
    unsigned GetEntry() const { return entry; }
    bool HasAura(unsigned a) const { return auras.count(a); }
    float GetLeewayBonusRadius() const { return leeway; }
};
struct Player : Unit {
    bool teleport=false;
    unsigned GetMapId() const { return map; }
    bool IsBeingTeleported() const { return teleport; }
};
struct WorldPosition {
    float x,y,z;
    explicit WorldPosition(Unit* u):x(u->x),y(u->y),z(u->z){}
    float distance(WorldPosition const& b) const {
        return std::hypot(std::hypot(x-b.x,y-b.y),z-b.z);
    }
};
using HazardPosition=std::pair<WorldPosition,float>;
struct Position { bool set=false; bool isSet() const { return set; } };
using PositionMap=std::map<std::string,Position>;
struct AiObjectContext {
    PositionMap positions;
    std::list<ObjectGuid> possible;
    unsigned listReads=0, resets=0;
    template<class T> T Value(std::string);
};
template<> PositionMap& AiObjectContext::Value(std::string) { return positions; }
template<> std::list<ObjectGuid> AiObjectContext::Value(std::string key) {
    assert(key=="possible targets"); ++listReads; return possible;
}
#define AI_VALUE(type,key) context->Value<type>(key)
#define RESET_AI_VALUE(type,key) (++context->resets)
enum class BotState { BOT_STATE_COMBAT };
struct PlayerbotAI {
    Player bot;
    AiObjectContext context;
    std::set<std::string> strategies;
    std::map<unsigned,Unit*> units;
    Player* GetBot() { return &bot; }
    AiObjectContext* GetAiObjectContext() { return &context; }
    bool HasStrategy(std::string s,BotState) const { return strategies.count(s); }
    Unit* GetUnit(unsigned id) { return units[id]; }
};
struct SpellEntry { unsigned EffectTriggerSpell[3]={}; unsigned EffectRadiusIndex[3]={}; };
struct SpellStore {
    std::map<unsigned,SpellEntry> rows;
    template<class T> T const* LookupEntry(unsigned id) const {
        auto it=rows.find(id); return it==rows.end()?nullptr:&it->second;
    }
} sSpellTemplate;
struct RadiusEntry { float Radius=0; };
struct RadiusStore {
    std::map<unsigned,RadiusEntry> rows;
    RadiusEntry const* LookupEntry(unsigned id) const {
        auto it=rows.find(id); return it==rows.end()?nullptr:&it->second;
    }
} sSpellRadiusStore;
namespace Spells { float GetSpellRadius(RadiusEntry const* r) { return r?r->Radius:0; } }
struct Event {};
struct Action {
    std::string name;
    explicit Action(std::string n):name(n){}
    virtual ~Action()=default;
    std::string const& getName() const { return name; }
};
struct MovementAction : Action { using Action::Action; };
struct CastReachTargetSpellAction : Action { using Action::Action; };
struct Multiplier {
    PlayerbotAI* ai;
    Multiplier(PlayerbotAI* a,std::string):ai(a){}
    virtual float GetValue(Action*)=0;
};
namespace ai {
    std::list<HazardPosition> GetSarturaWhirlwindHazards(PlayerbotAI*);
    bool SarturaWhirlwindTooClose(PlayerbotAI*);
    struct MoveAwayFromHazard {
        PlayerbotAI* ai;
        AiObjectContext* context;
        unsigned calls=0;
        bool result=true;
        explicit MoveAwayFromHazard(PlayerbotAI* a):ai(a),context(&a->context){}
        virtual bool Execute(Event&) { ++calls; assert(context->resets==calls); return result; }
    };
    struct SarturaWhirlwindEscapeAction : MoveAwayFromHazard {
        using MoveAwayFromHazard::MoveAwayFromHazard;
        bool Execute(Event&) override;
    };
}
using namespace ai;
'''
        checks = r'''
int main() {
    PlayerbotAI ai;
    Unit boss,guard,other;
    boss.entry=15516; boss.auras={26083};
    guard.entry=15984; guard.auras={26038}; guard.x=30;
    other.entry=1; other.auras={26083};
    ai.units={{1,&boss},{2,&guard},{3,&other}};
    ai.context.possible={1,2,3,4};
    sSpellTemplate.rows[26083].EffectTriggerSpell[0]=26084;
    sSpellTemplate.rows[26038].EffectTriggerSpell[0]=26686;
    sSpellTemplate.rows[26084].EffectRadiusIndex[0]=13;
    sSpellTemplate.rows[26686].EffectRadiusIndex[0]=13;
    // Fixture data, deliberately non-default: verify loaded radius, not a hardcoded ten.
    sSpellRadiusStore.rows[13].Radius=7;
    assert(GetSarturaWhirlwindHazards(nullptr).empty());
    assert(GetSarturaWhirlwindHazards(&ai).empty() && ai.context.listReads==0);
    ai.strategies={"sartura whirlwind","focus rti targets"};
    auto h=GetSarturaWhirlwindHazards(&ai);
    assert(h.size()==2 && h.front().second==9 && h.back().first.x==30);
    assert(SarturaWhirlwindTooClose(&ai));
    boss.x=25; assert(!SarturaWhirlwindTooClose(&ai)); // no stale cached center
    ai.bot.x=30; assert(SarturaWhirlwindTooClose(&ai));
    guard.auras.clear(); boss.auras.clear(); assert(!SarturaWhirlwindTooClose(&ai));
    boss.auras={26038}; assert(GetSarturaWhirlwindHazards(&ai).empty()); // wrong aura for entry
    boss.auras={26083}; boss.x=0; ai.bot.x=0;
    boss.leeway=1.5f; assert(GetSarturaWhirlwindHazards(&ai).front().second==10.5f); boss.leeway=0;
    boss.alive=false; assert(GetSarturaWhirlwindHazards(&ai).empty()); boss.alive=true;
    boss.combat=false; assert(GetSarturaWhirlwindHazards(&ai).empty()); boss.combat=true;
    boss.map=1; assert(GetSarturaWhirlwindHazards(&ai).empty()); boss.map=531;
    boss.world=false; assert(GetSarturaWhirlwindHazards(&ai).empty()); boss.world=true;
    sSpellRadiusStore.rows[13].Radius=0; assert(GetSarturaWhirlwindHazards(&ai).empty());
    sSpellRadiusStore.rows[13].Radius=std::nanf(""); assert(GetSarturaWhirlwindHazards(&ai).empty());
    sSpellRadiusStore.rows[13].Radius=7;
    sSpellTemplate.rows.erase(26084); assert(GetSarturaWhirlwindHazards(&ai).empty());
    sSpellTemplate.rows[26084].EffectRadiusIndex[0]=13;
    for (std::string hold:{"stay","follow","wait for attack","grind"}) {
        ai.strategies.insert(hold); ai.context.listReads=0;
        assert(GetSarturaWhirlwindHazards(&ai).empty() && ai.context.listReads==0);
        ai.strategies.erase(hold);
    }
    ai.context.positions["pull hold"].set=true;
    assert(GetSarturaWhirlwindHazards(&ai).empty());
    ai.context.positions["pull hold"].set=false;
    ai.bot.map=1; ai.context.listReads=0;
    assert(GetSarturaWhirlwindHazards(&ai).empty() && ai.context.listReads==0); ai.bot.map=531;
    ai.bot.teleport=true; assert(GetSarturaWhirlwindHazards(&ai).empty()); ai.bot.teleport=false;
    ai.bot.alive=false; assert(GetSarturaWhirlwindHazards(&ai).empty()); ai.bot.alive=true;
    ai.bot.combat=false; assert(GetSarturaWhirlwindHazards(&ai).empty()); ai.bot.combat=true;
    ai.bot.world=false; assert(GetSarturaWhirlwindHazards(&ai).empty()); ai.bot.world=true;
    SarturaWhirlwindEscapeAction escape(&ai); Event event;
    assert(escape.Execute(event) && escape.calls==1 && ai.context.resets==1);
    boss.x=30; assert(!escape.Execute(event) && escape.calls==1); boss.x=0;
    ai.strategies.insert("stay"); assert(!escape.Execute(event) && escape.calls==1);
    ai.strategies.erase("stay"); escape.result=false;
    assert(!escape.Execute(event) && escape.calls==2 && ai.context.resets==2);
    SarturaWhirlwindMultiplier multiplier(&ai);
    MovementAction reach("reach melee"), flee("flee"), retreat("sartura whirlwind escape");
    CastReachTargetSpellAction cast("cast reach"); Action heal("heal");
    assert(multiplier.GetValue(&reach)==0 && multiplier.GetValue(&cast)==0);
    assert(multiplier.GetValue(&heal)==1 && multiplier.GetValue(&flee)==1);
    assert(multiplier.GetValue(&retreat)==1 && multiplier.GetValue(nullptr)==1);
    boss.auras.clear(); assert(multiplier.GetValue(&reach)==1 && multiplier.GetValue(&cast)==1);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp = pathlib.Path(tmp) / 'fixture.cpp'
            binary = pathlib.Path(tmp) / 'fixture'
            cpp.write_text(fixture + production + checks)
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_wiring(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        hazards = (root / 'ai/playerbot/strategy/values/HazardsValue.cpp').read_text()
        self.assertIn('auto whirlwinds = GetSarturaWhirlwindHazards(ai);', hazards)
        self.assertIn('hazards.splice(hazards.end(), whirlwinds);', hazards)
        for path, text in (
            ('StrategyContext.h', 'creators["sartura whirlwind"]'),
            ('triggers/TriggerContext.h', 'creators["sartura whirlwind nearby"]'),
            ('actions/ActionContext.h', 'creators["sartura whirlwind escape"]')):
            self.assertIn(text, (root / 'ai/playerbot/strategy' / path).read_text())
        source = (root / 'ai/playerbot/strategy/generic/SarturaWhirlwindStrategy.cpp').read_text()
        self.assertIn('new NextAction("sartura whirlwind escape", ACTION_EMERGENCY + 7)', source)
