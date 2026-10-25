"""Production Kri cloud hazard/escape dispatch with world and movement boundaries."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest
from tools.test_nefarian_bone_target import function


class KriCloudTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_live_hazards_and_dispatch(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / 'ai/playerbot/strategy/actions/KriCloudActions.cpp').read_text()
        strategy = (root / 'ai/playerbot/strategy/generic/KriCloudStrategy.cpp').read_text()
        production = '\n'.join(function(source, name) for name in (
            'std::list<HazardPosition> ai::GetKriCloudHazards(',
            'bool ai::KriCloudTooClose(',
            'bool KriCloudEscapeAction::Execute('))
        production += '\n' + function(strategy, 'class KriCloudMultiplier') + ';\n'
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
struct DynamicObject;
struct Unit {
    DynamicObject* cloud=nullptr;
    DynamicObject* GetDynObject(unsigned spell) { assert(spell==25786); return cloud; }
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
struct DynamicObject : Unit {
    float radius=7; unsigned duration=1000;
    float GetRadius() const { return radius; }
    unsigned GetDuration() const { return duration; }
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
    assert(key=="nearest corpses"); ++listReads; return possible;
}
#define AI_VALUE(type,key) context->Value<type>(key)
#define RESET_AI_VALUE(type,key) (++context->resets)
enum class BotState { BOT_STATE_COMBAT, BOT_STATE_NON_COMBAT };
struct PlayerbotAI {
    Player bot;
    AiObjectContext context;
    std::set<std::string> strategies, noncombat;
    std::map<unsigned,Unit*> units;
    Player* GetBot() { return &bot; }
    AiObjectContext* GetAiObjectContext() { return &context; }
    bool HasStrategy(std::string s,BotState state) const { return state==BotState::BOT_STATE_COMBAT?strategies.count(s):noncombat.count(s); }
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
    std::list<HazardPosition> GetKriCloudHazards(PlayerbotAI*);
    bool KriCloudTooClose(PlayerbotAI*);
    struct MoveAwayFromHazard {
        PlayerbotAI* ai;
        AiObjectContext* context;
        unsigned calls=0;
        bool result=true;
        explicit MoveAwayFromHazard(PlayerbotAI* a):ai(a),context(&a->context){}
        virtual bool Execute(Event&) { ++calls; assert(context->resets==calls); return result; }
    };
    struct KriCloudEscapeAction : MoveAwayFromHazard {
        using MoveAwayFromHazard::MoveAwayFromHazard;
        bool Execute(Event&) override;
    };
}
using namespace ai;
'''
        checks = r'''
int main() {
    PlayerbotAI ai;
    Unit kri,other; DynamicObject cloud;
    kri.entry=15511; kri.alive=false; kri.cloud=&cloud; kri.x=100;
    other.entry=1; other.alive=false; other.cloud=&cloud;
    ai.units={{1,&kri},{2,&other}}; ai.context.possible={1,2,3};
    assert(GetKriCloudHazards(nullptr).empty());
    assert(GetKriCloudHazards(&ai).empty() && ai.context.listReads==0);
    ai.strategies={"kri cloud"};
    auto h=GetKriCloudHazards(&ai);
    assert(h.size()==1 && h.front().second==9 && h.front().first.x==0);
    assert(KriCloudTooClose(&ai)); // cloud position, NOT distant corpse
    cloud.x=25; assert(!KriCloudTooClose(&ai)); cloud.x=0;
    kri.cloud=nullptr; assert(GetKriCloudHazards(&ai).empty()); kri.cloud=&cloud;
    kri.alive=true; assert(GetKriCloudHazards(&ai).empty()); kri.alive=false;
    kri.map=1; assert(GetKriCloudHazards(&ai).empty()); kri.map=531;
    kri.world=false; assert(GetKriCloudHazards(&ai).empty()); kri.world=true;
    cloud.map=1; assert(GetKriCloudHazards(&ai).empty()); cloud.map=531;
    cloud.world=false; assert(GetKriCloudHazards(&ai).empty()); cloud.world=true;
    cloud.duration=0; assert(GetKriCloudHazards(&ai).empty()); cloud.duration=1;
    cloud.radius=0; assert(GetKriCloudHazards(&ai).empty());
    cloud.radius=std::nanf(""); assert(GetKriCloudHazards(&ai).empty()); cloud.radius=7;
    for (std::string hold:{"stay","follow","wait for attack","grind"}) {
        ai.strategies.insert(hold); ai.context.listReads=0;
        assert(GetKriCloudHazards(&ai).empty() && ai.context.listReads==0);
        ai.strategies.erase(hold);
        ai.noncombat.insert(hold);
        assert(!GetKriCloudHazards(&ai).empty()); // idle follow must not veto combat
        ai.bot.combat=false; assert(GetKriCloudHazards(&ai).empty());
        ai.noncombat.erase(hold); assert(!GetKriCloudHazards(&ai).empty());
        ai.bot.combat=true;
    }
    ai.context.positions["pull hold"].set=true;
    assert(GetKriCloudHazards(&ai).empty()); ai.context.positions["pull hold"].set=false;
    ai.bot.map=1; assert(GetKriCloudHazards(&ai).empty()); ai.bot.map=531;
    ai.bot.teleport=true; assert(GetKriCloudHazards(&ai).empty()); ai.bot.teleport=false;
    ai.bot.alive=false; assert(GetKriCloudHazards(&ai).empty()); ai.bot.alive=true;
    ai.bot.world=false; assert(GetKriCloudHazards(&ai).empty()); ai.bot.world=true;
    KriCloudEscapeAction escape(&ai); Event event;
    assert(escape.Execute(event) && escape.calls==1 && ai.context.resets==1);
    kri.cloud=nullptr; assert(!escape.Execute(event) && escape.calls==1); kri.cloud=&cloud;
    ai.strategies.insert("stay"); assert(!escape.Execute(event) && escape.calls==1);
    ai.strategies.erase("stay"); escape.result=false;
    assert(!escape.Execute(event) && escape.calls==2 && ai.context.resets==2);
    KriCloudMultiplier multiplier(&ai);
    MovementAction reach("reach melee"), flee("flee"), retreat("kri cloud escape");
    CastReachTargetSpellAction cast("cast reach"); Action heal("heal");
    assert(multiplier.GetValue(&reach)==0 && multiplier.GetValue(&cast)==0);
    assert(multiplier.GetValue(&heal)==1 && multiplier.GetValue(&flee)==1);
    assert(multiplier.GetValue(&retreat)==1 && multiplier.GetValue(nullptr)==1);
    kri.cloud=nullptr; assert(multiplier.GetValue(&reach)==1 && multiplier.GetValue(&cast)==1);
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
        self.assertIn('auto kriClouds = GetKriCloudHazards(ai);', hazards)
        self.assertIn('hazards.splice(hazards.end(), kriClouds);', hazards)
        for path, text in (
            ('StrategyContext.h', 'creators["kri cloud"]'),
            ('triggers/TriggerContext.h', 'creators["kri cloud nearby"]'),
            ('actions/ActionContext.h', 'creators["kri cloud escape"]')):
            self.assertIn(text, (root / 'ai/playerbot/strategy' / path).read_text())
        source = (root / 'ai/playerbot/strategy/generic/KriCloudStrategy.cpp').read_text()
        self.assertIn('new NextAction("kri cloud escape", ACTION_EMERGENCY + 7)', source)
