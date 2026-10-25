"""Production cure predicate and cast gate, with only engine services mocked."""
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


class ZulGurubCureTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_protected_aura_survives_automatic_cures(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        actions = (root / 'ai/playerbot/strategy/actions/GenericSpellActions.cpp').read_text()
        ai = (root / 'ai/playerbot/PlayerbotAISpells.cpp').read_text()
        # Baseline lacks these helpers. Neutral definitions allow the SAME
        # production predicate/Execute fixture to demonstrate the failure.
        helpers = ('std::uint32_t GetZulGurubProtectedDispelMask(PlayerbotAI*, Unit*) { return 0; }\n'
                   'bool IsZulGurubCureSpellProtected(PlayerbotAI*, Unit*, std::uint32_t) { return false; }')
        if 'std::uint32_t GetZulGurubProtectedDispelMask(' in actions:
            helpers = function(actions, 'std::uint32_t GetZulGurubProtectedDispelMask(')
            helpers += '\n' + function(actions, 'bool IsZulGurubCureSpellProtected(')
        # This fixture models cures, never healer offensive filler.
        helpers += '\nbool IsRaidHealerFiller(std::string) { return false; }\n'
        helpers += 'bool IsRaidHealerMaintenance(std::string) { return false; }\n'
        helpers += 'bool RaidHealerShouldHoldDamage(PlayerbotAI*) { return false; }\n'
        production = '\n'.join((
            'namespace ai {', helpers, '}',
            function(ai, 'bool PlayerbotAI::HasAuraToDispel('),
            function(ai, 'bool PlayerbotAI::canDispel('),
            function(actions, 'bool CastSpellAction::isUseful('),
            function(actions, 'bool CastSpellAction::Execute(')))
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <list>
#include <map>
#include <set>
#include <string>
#include <strings.h>
using uint32 = std::uint32_t;
using int32 = std::int32_t;
enum DispelType { DISPEL_NONE=0, DISPEL_MAGIC=1, DISPEL_CURSE=2, DISPEL_DISEASE=3, DISPEL_POISON=4, DISPEL_ALL=7 };
uint32 GetDispellMask(DispelType t) { return t == DISPEL_ALL ? 30 : 1u << t; }
enum { SPELL_AURA_NONE=0, TOTAL_AURAS=1, EFFECT_INDEX_0=0, EFFECT_INDEX_2=2,
    SPELL_EFFECT_DISPEL=38, SPELL_EFFECT_CREATE_ITEM=24, EQUIP_ERR_OK=0,
    CURRENT_MELEE_SPELL=0, CURRENT_AUTOREPEAT_SPELL=1, CLASS_HUNTER=3 };
using AuraType = int;
struct SpellEntry {
    uint32 Id=0, Dispel=0;
    std::string SpellName[1] = {"fixture spell"};
    int Effect[3] = {}, EffectMiscValue[3] = {};
    uint32 EffectItemType[3] = {};
};
struct Aura {
    SpellEntry* spell;
    SpellEntry const* GetSpellProto() const { return spell; }
    int GetAuraDuration() const { return 10000; }
};
struct ItemPrototype {};
using PlayerSpellMap = std::map<uint32, int>;
struct Unit {
    using AuraList = std::list<Aura*>;
    unsigned map=309, instance=1;
    bool world=true, friendly=true;
    std::set<uint32> ids;
    AuraList auras;
    mutable unsigned auraReads=0;
    uint32 GetMapId() const { return map; }
    bool IsInWorld() const { return world; }
    bool IsInMap(Unit* other) const { return map==other->map && instance==other->instance; }
    bool HasAura(uint32 id) const { ++auraReads; return ids.count(id); }
    AuraList const& GetAurasByType(AuraType) const { return auras; }
};
struct Player : Unit {
    PlayerSpellMap spells;
    PlayerSpellMap& GetSpellMap() { return spells; }
    int CanUseItem(ItemPrototype const*) { return EQUIP_ERR_OK; }
    char const* GetName() { return "fixture"; }
    bool GetCurrentSpell(int) { return false; }
    int GetClass() { return 2; }
};
struct Facade {
    std::map<uint32, SpellEntry> spells;
    unsigned lookups=0;
    bool IsFriendlyTo(Unit*, Unit* target) { return target->friendly; }
    SpellEntry const* LookupSpellInfo(uint32 id) {
        ++lookups; auto it=spells.find(id); return it==spells.end()?nullptr:&it->second;
    }
    float getDistance2d(Unit*, Unit*) { return 0; }
} sServerFacade;
struct { unsigned globalCoolDown=1500; int dispelAuraDuration=0; } sPlayerbotAIConfig;
struct { ItemPrototype const* GetItemPrototype(uint32) { return nullptr; } } sObjectMgr;
struct { template<class... T> void outDebug(T...) {} } sLog;
void strToLower(std::string&) {}
int strcmpi(char const* a, char const* b) { return strcasecmp(a,b); }
// Both encounter auras are negative in checked world/core metadata.
bool IsPositiveSpell(uint32) { return false; }
enum class BotState { BOT_STATE_COMBAT };
enum class BotCheatMask { attackspeed };
// Diagnostics are observed separately; this fixture tests cure decisions.
struct HealerDiagnosticsStub {
    template<class... T> void CastAttempt(T...) {}
};
struct PlayerbotAI {
    HealerDiagnosticsStub& GetHealerDiagnostics() {
        static HealerDiagnosticsStub diagnostics;
        return diagnostics;
    }
    Player* bot;
    std::set<std::string> strategies;
    unsigned casts=0;
    Player* GetBot() { return bot; }
    bool HasStrategy(std::string key, BotState) { return strategies.count(key); }
    bool HasCheat(BotCheatMask) { return false; }
    template<class T> bool CastSpell(T, Unit*, void* =nullptr, bool=false, uint32* =nullptr) { ++casts; return true; }
    bool HasAuraToDispel(Unit*, uint32);
    bool canDispel(SpellEntry const*, uint32);
};
struct Event {};
struct CastSpellAction {
    PlayerbotAI* ai;
    Player* bot;
    Unit* target;
    std::string spellName="fixture cure";
    std::string getName(){ return spellName; }
    uint32 spellId;
    Unit* GetTarget() { return target; }
    std::string GetTargetName() { return "party member to dispel"; }
    void SetDuration(uint32) {}
    bool isUseful();
    bool Execute(Event&);
};
#define AI_VALUE2(type, key, qualifier) true
using namespace std;
namespace ai {}
using namespace ai;
'''
        checks = r'''
int main() {
    Player bot;
    PlayerbotAI ai{&bot, {}, 0};
    Unit party;
    SpellEntry poison; poison.Id=24321; poison.Dispel=DISPEL_POISON;
    SpellEntry other; other.Id=1; other.Dispel=DISPEL_POISON;
    Aura p{&poison}, o{&other};
    party.ids={24321}; party.auras={&p,&o};
    SpellEntry cleanse; cleanse.Id=4987;
    cleanse.Effect[0]=cleanse.Effect[1]=cleanse.Effect[2]=SPELL_EFFECT_DISPEL;
    cleanse.EffectMiscValue[0]=DISPEL_POISON;
    cleanse.EffectMiscValue[1]=DISPEL_DISEASE;
    cleanse.EffectMiscValue[2]=DISPEL_MAGIC;
    sServerFacade.spells[4987]=cleanse;
    SpellEntry magic; magic.Effect[0]=SPELL_EFFECT_DISPEL; magic.EffectMiscValue[0]=DISPEL_MAGIC;
    sServerFacade.spells[527]=magic;
    SpellEntry all; all.Effect[2]=SPELL_EFFECT_DISPEL; all.EffectMiscValue[2]=-1;
    sServerFacade.spells[999]=all;
    SpellEntry abolish; abolish.Effect[0]=6; abolish.Effect[1]=SPELL_EFFECT_DISPEL;
    abolish.EffectMiscValue[1]=DISPEL_POISON; sServerFacade.spells[2893]=abolish;
    CastSpellAction action{&ai,&bot,&party,"cleanse",4987}; Event event;
    // Off by default: original predicate and cast behavior, no aura/metadata work.
    assert(ai.HasAuraToDispel(&party,DISPEL_POISON));
    assert(action.isUseful() && action.Execute(event));
    assert(party.auraReads==0 && sServerFacade.lookups==0);
    ai.strategies.insert("hakkar poison");
    assert(!ai.HasAuraToDispel(&party,DISPEL_POISON));
    assert(!action.isUseful());
    unsigned casts=ai.casts;
    assert(!action.Execute(event) && ai.casts==casts);
    // A different poison must not open the gate. Nor may a disease-triggered
    // Cleanse remove the protected poison through another effect slot.
    other.Dispel=DISPEL_DISEASE;
    assert(ai.HasAuraToDispel(&party,DISPEL_DISEASE));
    assert(!action.Execute(event));
    action.spellId=527;
    assert(action.isUseful() && action.Execute(event)); // magic-only cure unaffected
    action.spellId=999; assert(!action.Execute(event)); // core negative/all mask
    action.spellId=2893; assert(!action.Execute(event)); // direct Abolish effect
    action.spellId=4987;
    party.ids.clear(); assert(action.isUseful());
    party.ids.insert(24321); assert(!action.Execute(event)); // aura lands after queueing
    action.target=&bot; bot.ids={24321}; bot.auras={&p};
    assert(!ai.HasAuraToDispel(&bot,DISPEL_POISON));
    assert(!action.Execute(event)); // self cure
    action.target=&party;
    bot.map=party.map=1; party.auraReads=0;
    unsigned lookups=sServerFacade.lookups;
    assert(action.Execute(event) && party.auraReads==0 && sServerFacade.lookups==lookups);
    bot.map=party.map=309;
    party.instance=2; assert(action.Execute(event)); party.instance=1;
    party.friendly=false; assert(action.Execute(event)); party.friendly=true;
    party.world=false; assert(!action.isUseful()); party.world=true;
    ai.strategies.clear(); assert(action.Execute(event)); // immediate opt-out

    // Jin'do: Delusions grants shade damage access. Protect the whole curse
    // type, including a cure requested for a different curse on this target.
    SpellEntry delusions; delusions.Id=24306; delusions.Dispel=DISPEL_CURSE;
    Aura d{&delusions}; other.Dispel=DISPEL_CURSE;
    party.ids={24306}; party.auras={&d,&o};
    SpellEntry decurse; decurse.Effect[0]=SPELL_EFFECT_DISPEL;
    decurse.EffectMiscValue[0]=DISPEL_CURSE; sServerFacade.spells[2782]=decurse;
    action.spellId=2782;
    assert(ai.HasAuraToDispel(&party,DISPEL_CURSE));
    assert(action.isUseful() && action.Execute(event)); // default off
    ai.strategies={"hakkar poison"}; assert(action.Execute(event)); // independent toggles
    ai.strategies={"jindo curse"};
    assert(!ai.HasAuraToDispel(&party,DISPEL_CURSE));
    assert(!action.isUseful() && !action.Execute(event));
    action.spellId=527; assert(action.Execute(event)); // Hex is magic, not curse
    action.spellId=2782;
    party.ids.clear(); assert(action.isUseful());
    party.ids={24306}; assert(!action.Execute(event)); // queued before curse landed
    bot.ids={24306}; bot.auras={&d}; action.target=&bot;
    assert(!ai.HasAuraToDispel(&bot,DISPEL_CURSE) && !action.Execute(event));
    action.target=&party;
    bot.map=party.map=1; assert(action.Execute(event)); bot.map=party.map=309;
    party.instance=2; assert(action.Execute(event)); party.instance=1;
    party.friendly=false; assert(action.Execute(event)); party.friendly=true;
    action.spellId=999; assert(!action.Execute(event));
    ai.strategies.clear(); assert(action.Execute(event));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source = pathlib.Path(tmp) / 'fixture.cpp'
            binary = pathlib.Path(tmp) / 'fixture'
            source.write_text(fixture + production + checks)
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Wno-unused-parameter',
                            str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
