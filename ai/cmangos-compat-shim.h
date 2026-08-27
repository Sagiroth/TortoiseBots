// pi-lens-ignore: clang:pp_file_not_found,clang:unknown_typename,clang:undeclared_var_use,clang:use_of_undeclared_identifier,clang:unknown_type_name
// cmangos/playerbots → Penqle/tortoise-wow compatibility shim.
//
// Provides the cmangos-side names/constants the vendored bot module references
// but Penqle either names differently or doesn't expose. Included by botpch.h
// as the first header in the PCH chain so all bot TUs see it.
//
// What's here:
//   - Type renames / typedef forwards
//   - Define mappings (cmangos constants → Penqle equivalents)
//   - Standard-library headers cmangos uses without explicit include
//
// What's NOT here (handled by per-call-site rewrites because they need
// contextual changes, not name remapping):
//   - DBC-store globals (sMapStore ↔ sMapStorage architecture)
//   - WorldPacket move-only assignment sites
//   - CreatureData::id (single field) vs Penqle's creature_id (array)
//   - PlayerbotAI internal signature mismatches
//   - GuidPosition diamond-inheritance ambiguity

#pragma once

// === Standard library headers the bot module uses without explicit includes ===
// PlayerbotAI.h declares methods taking std::future<...> but doesn't #include
// <future>. Penqle's botpch.h already pulls in many std headers but not this one.
#include <future>
#include <chrono>
#include <random>
#include <vector>
#include <algorithm>
#include <limits>

// === Small public-field adapters ===
// The core keeps quest-log slot helpers private. Use the same field layout
// through the public Object accessors instead of widening the core surface.
inline uint32 GetQuestSlotIdCompat(Player const* player, uint16 slot)
{
    return player && slot < MAX_QUEST_LOG_SIZE
        ? player->GetUInt32Value(PLAYER_QUEST_LOG_1_1 + slot * MAX_QUEST_OFFSET + QUEST_ID_OFFSET)
        : 0;
}

inline void SetQuestSlotCompat(Player* player, uint16 slot, uint32 questId)
{
    if (!player || slot >= MAX_QUEST_LOG_SIZE)
        return;

    player->SetUInt32Value(PLAYER_QUEST_LOG_1_1 + slot * MAX_QUEST_OFFSET + QUEST_ID_OFFSET, questId);
    player->SetUInt32Value(PLAYER_QUEST_LOG_1_1 + slot * MAX_QUEST_OFFSET + QUEST_COUNT_STATE_OFFSET, 0);
    player->SetUInt32Value(PLAYER_QUEST_LOG_1_1 + slot * MAX_QUEST_OFFSET + QUEST_TIME_OFFSET, 0);
}

inline void SetQuestSlotStateCompat(Player* player, uint16 slot, uint8 state)
{
    if (player && slot < MAX_QUEST_LOG_SIZE)
        player->SetByteFlag(PLAYER_QUEST_LOG_1_1 + slot * MAX_QUEST_OFFSET + QUEST_COUNT_STATE_OFFSET, 3, state);
}

inline uint32 GetRequiredLootSkillCompat(CreatureInfo const* creature)
{
    return creature && creature->skinning_loot_id ? SKILL_SKINNING : SKILL_NONE;
}

// === Type renames ===
// Mature strategy code uses GenericTransport for the core's ordinary
// Transport type. Keep the alias local to the module; it is not a vehicle API.
class Transport;
typedef Transport GenericTransport;

// cmangos's CreatureAI base is named UnitAI; Penqle uses CreatureAI. Same shape.
class CreatureAI;
typedef CreatureAI UnitAI;

// cmangos uses GuidSet typedef. Penqle uses ObjectGuidSet.
// Pull ObjectGuid header transitively to ensure the typedef target is visible
// before the alias is used.
#include "ObjectGuid.h"
typedef ObjectGuidSet GuidSet;
using GuidVector = std::vector<ObjectGuid>;

// cmangos uses AreaTableEntry; Penqle has AreaEntry (defined in Maps/Map.h).
// They model the same data. Forward-declare and typedef.
struct AreaEntry;
typedef AreaEntry AreaTableEntry;

// cmangos uses AreaTrigger; Penqle has AreaTriggerEntry (DBCStructure.h).
struct AreaTriggerEntry;
typedef AreaTriggerEntry AreaTrigger;

// === Define mappings ===
// cmangos's ItemClass enum has ITEM_CLASS_MISC at value 15. Penqle renamed
// this to ITEM_CLASS_JUNK (also at 15). The bot module's ahbot/Category.h
// uses the cmangos name.
#ifndef ITEM_CLASS_MISC
#define ITEM_CLASS_MISC ITEM_CLASS_JUNK
#endif

// The bot module uses the Vanilla/Turtle level cap for fixed-size tables.
// Penqle exposes MAX_LEVEL/STRONG_MAX_LEVEL but not this exact name.
#ifndef DEFAULT_MAX_LEVEL
#define DEFAULT_MAX_LEVEL 60
#endif

// cmangos's Team enum has TEAM_BOTH_ALLOWED for queries that span both factions.
// Penqle's Team enum has TEAM_NONE=0 (used as "no faction filter" sentinel).
// Map TEAM_BOTH_ALLOWED to TEAM_NONE so default-arg conversions work.
#ifndef TEAM_BOTH_ALLOWED
#define TEAM_BOTH_ALLOWED TEAM_NONE
#endif

// cmangos has DIST_CALC_COMBAT_REACH for Unit::GetDistance variants.
// Penqle uses DIST_CALC_BOUNDING_RADIUS / DIST_CALC_COMBAT_REACH naming.
// Need to verify per use site; for now define as a passthrough constant.
#ifndef DIST_CALC_COMBAT_REACH
enum DistanceCalculation { DIST_CALC_NONE = 0, DIST_CALC_BOUNDING_RADIUS = 1, DIST_CALC_COMBAT_REACH = 2 };
#endif

// cmangos has BarGoLink (console progress bar). Penqle has no equivalent.
// Define as a complete stub class so the bot's BarGoLink pointer dereferences
// and method calls compile (no-op at runtime).
class BarGoLink {
public:
    BarGoLink() {}
    template<typename T> BarGoLink(T /*total*/) {}
    void step() {}
    void Step() {}
    static void SetOutputState(bool /*state*/) {}
};

// === DBC store aliases ===
// cmangos accesses spell DBC via `sSpellTemplate.LookupEntry<SpellEntry>(id)`.
// Penqle uses `sSpellMgr.GetSpellEntry(id)`. The bot's `sSpellTemplate` is used
// in 600+ call sites; rather than rewrite each, provide a header-only wrapper
// object that exposes a templated LookupEntry() forwarding to Penqle's API.
//
// The forward-decls below need ObjectMgr / SpellMgr access. Because this header
// is included EARLY in botpch.h (before SpellMgr.h), we declare the proxy class
// inline-only — its methods get instantiated at the call sites, after Penqle's
// SpellMgr/ObjectMgr are already in scope via later botpch.h includes.

// Note: this shim is included AFTER Penqle's SpellMgr.h / ObjectMgr.h /
// SpellEntry / ItemPrototype headers in botpch.h, so we can call those APIs
// directly in inline bodies.

// Singleton-like wrapper for cmangos's sSpellTemplate. Inline LookupEntry<>()
// forwards to Penqle's sSpellMgr.GetSpellEntry().
struct CmangosSpellTemplateProxy
{
    template<typename T = SpellEntry>
    T const* LookupEntry(uint32 id) const { return sSpellMgr.GetSpellEntry(id); }
    // cmangos's DBCStorage exposes GetMaxEntry. Bot uses it to iterate spells.
    // Penqle's sSpellMgr exposes GetMaxSpellId() — same purpose.
    uint32 GetMaxEntry() const { return sSpellMgr.GetMaxSpellId(); }
};
inline CmangosSpellTemplateProxy sSpellTemplate;

// Singleton-like wrapper for cmangos's sItemStorage. Forwards to sObjectMgr.GetItemPrototype().
// CMaNGOS' stores expose a one-past-the-largest-ID GetMaxEntry(). Some of the
// Tortoise stores are sparse maps instead, so a fixed donor-era bound is not
// safe: Turtle custom entries are well above the classic ranges. Cache the
// derived upper bound while the loaded store has the same size. These stores
// are loaded before the bot module and are not mutated by the bot update loop;
// a reload that changes the number of records naturally refreshes the bound.
template<typename Store>
class CmangosMapUpperBoundCache
{
public:
    uint32 Get(Store const& store) const
    {
        size_t const size = store.size();
        if (size != m_size)
        {
            uint32 upperBound = 0;
            for (auto const& entry : store)
            {
                if (entry.first == std::numeric_limits<uint32>::max())
                {
                    upperBound = entry.first;
                    break;
                }

                upperBound = std::max(upperBound, entry.first + 1);
            }

            m_size = size;
            m_upperBound = upperBound;
        }

        return m_upperBound;
    }

private:
    mutable size_t m_size = std::numeric_limits<size_t>::max();
    mutable uint32 m_upperBound = 0;
};

struct CmangosItemStorageProxy
{
    template<typename T = ItemPrototype>
    T const* LookupEntry(uint32 id) const { return sObjectMgr.GetItemPrototype(id); }
    uint32 GetMaxEntry() const { return m_upperBound.Get(sObjectMgr.GetItemPrototypeMap()); }

private:
    mutable CmangosMapUpperBoundCache<ItemPrototypeMap> m_upperBound;
};
inline CmangosItemStorageProxy sItemStorage;

// Singleton-like wrapper for cmangos's sMapStore. Penqle uses sMapStorage (SQLStorage).
struct MapEntry;  // defined in Maps/Map.h
struct CmangosMapStoreProxy
{
    template<typename T = MapEntry>
    T const* LookupEntry(uint32 id) const { return sMapStorage.LookupEntry<MapEntry>(id); }
    uint32 GetNumRows() const { return sMapStorage.GetMaxEntry(); }
};
inline CmangosMapStoreProxy sMapStore;

// Singleton-like wrapper for cmangos's sFactionTemplateStore.
struct FactionTemplateEntry;  // defined in Database/DBCStructure.h
struct CmangosFactionTemplateStoreProxy
{
    template<typename T = FactionTemplateEntry>
    T const* LookupEntry(uint32 id) const { return sObjectMgr.GetFactionTemplateEntry(id); }
    uint32 GetNumRows() const { return m_upperBound.Get(sObjectMgr.GetFactionTemplateMap()); }

private:
    mutable CmangosMapUpperBoundCache<FactionTemplatesMap> m_upperBound;
};
inline CmangosFactionTemplateStoreProxy sFactionTemplateStore;

// === Other defines ===
// cmangos has ITEM_FLAG_HAS_LOOT (lootable item). Penqle uses ITEM_FLAG_HAS_LOOT or ITEM_FLAG_OPENABLE.
#ifndef ITEM_FLAG_HAS_LOOT
#define ITEM_FLAG_HAS_LOOT ITEM_FLAG_LOOTABLE
#endif

// === Type renames (cmangos→Penqle struct name diffs) ===
// cmangos's ItemPrototype has _Spell substruct (older naming);
// Penqle uses _ItemSpell (current naming). They're the same shape.
typedef _ItemSpell _Spell;

// cmangos has TEMPSPAWN_* enum values; Penqle has TEMPSUMMON_*. Map.
#ifndef TEMPSPAWN_TIMED_DESPAWN
#define TEMPSPAWN_TIMED_DESPAWN TEMPSUMMON_TIMED_DESPAWN
#endif
#ifndef TEMPSPAWN_TIMED_OR_DEAD_DESPAWN
#define TEMPSPAWN_TIMED_OR_DEAD_DESPAWN TEMPSUMMON_TIMED_OR_DEAD_DESPAWN
#endif
#ifndef TEMPSPAWN_TIMED_OR_CORPSE_DESPAWN
#define TEMPSPAWN_TIMED_OR_CORPSE_DESPAWN TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN
#endif
#ifndef TEMPSPAWN_DEAD_DESPAWN
#define TEMPSPAWN_DEAD_DESPAWN TEMPSUMMON_DEAD_DESPAWN
#endif
#ifndef TEMPSPAWN_CORPSE_DESPAWN
#define TEMPSPAWN_CORPSE_DESPAWN TEMPSUMMON_CORPSE_DESPAWN
#endif
#ifndef TEMPSPAWN_CORPSE_TIMED_DESPAWN
#define TEMPSPAWN_CORPSE_TIMED_DESPAWN TEMPSUMMON_CORPSE_TIMED_DESPAWN
#endif
#ifndef TEMPSPAWN_MANUAL_DESPAWN
#define TEMPSPAWN_MANUAL_DESPAWN TEMPSUMMON_MANUAL_DESPAWN
#endif

// === Spells namespace functions hoisted to global scope ===
// cmangos's bot calls IsPositiveSpell / GetDispellMask without namespace.
// Penqle wraps these in `namespace Spells`. Bring them into global scope
// for the bot's consumption.
using Spells::IsPositiveSpell;
using Spells::GetDispellMask;
using Spells::IsPassiveSpell;
// SpellEntry* overload: bot passes spellInfo directly.
inline bool IsPositiveSpell(SpellEntry const* spellInfo) { return spellInfo && spellInfo->IsPositiveSpell(); }
inline bool IsPositiveSpell(SpellEntry const* spellInfo, WorldObject const* caster, WorldObject const* victim) { return spellInfo && spellInfo->IsPositiveSpell(caster, victim); }

// === TRIGGERED_* spell-cast flags ===
// cmangos's CastSpell takes a TriggerCastFlags bitmask while Penqle takes a
// bool. The active module call sites only need the ordinary triggered/untriggered
// distinction; keep those two aliases bool-typed so the core's overloads select
// the native API rather than private template traps.
#ifndef TRIGGERED_NONE
#define TRIGGERED_NONE false
#endif
#ifndef TRIGGERED_OLD_TRIGGERED
#define TRIGGERED_OLD_TRIGGERED true
#endif

// === BG_AV_NODE_STATUS_ defines ===
// cmangos has these in BattleGroundAV.h; Penqle may use different naming.
// Define as constants so bot's symbolic references compile.
#ifndef BG_AV_NODE_STATUS_ALLY_OCCUPIED
#define BG_AV_NODE_STATUS_ALLY_OCCUPIED 0
#endif
#ifndef BG_AV_NODE_STATUS_HORDE_OCCUPIED
#define BG_AV_NODE_STATUS_HORDE_OCCUPIED 1
#endif

// === Additional cmangos-only DBC store proxies ===
// sFactionStore (faction.dbc) — distinct from sFactionTemplateStore (factiontemplate.dbc).
struct FactionEntry;  // defined in DBCStructure.h
struct CmangosFactionStoreProxy
{
    template<typename T = FactionEntry>
    T const* LookupEntry(uint32 id) const { return sObjectMgr.GetFactionEntry(id); }
    uint32 GetNumRows() const { return m_upperBound.Get(sObjectMgr.GetFactionMap()); }

private:
    mutable CmangosMapUpperBoundCache<FactionsMap> m_upperBound;
};
inline CmangosFactionStoreProxy sFactionStore;

// sCreatureStorage (creature_template SQL).
struct CmangosCreatureStorageProxy
{
    template<typename T = CreatureInfo>
    T const* LookupEntry(uint32 id) const { return sObjectMgr.GetCreatureTemplate(id); }
    uint32 GetMaxEntry() const { return m_upperBound.Get(sObjectMgr.GetCreatureInfoMap()); }

private:
    mutable CmangosMapUpperBoundCache<CreatureInfoMap> m_upperBound;
};
inline CmangosCreatureStorageProxy sCreatureStorage;

// === Helpers ===
// strstri overload: bot's PlayerbotAI.cpp forward-declares strstri(std::string, std::string).
// Penqle's playerbot/Helpers.cpp now provides the implementation (added).
// Re-declare here for visibility at all bot TUs.
char* strstri(std::string const& s1, std::string const& s2);

// Overload of strstr taking std::string haystack — bot calls strstr(proto->Name1, "literal")
// where Name1 is std::string. Forward to libc strstr via .c_str().
inline const char* strstr(std::string const& haystack, const char* needle) {
    return std::strstr(haystack.c_str(), needle);
}

// === BattleGroundMgr alias ===
// Done via forwarder in Penqle's BattleGroundMgr.h (BgTemplateId → BGTemplateId).

// === BG_AV_NODE_STATUS_ contested (additional) ===
#ifndef BG_AV_NODE_STATUS_ALLY_CONTESTED
#define BG_AV_NODE_STATUS_ALLY_CONTESTED 2
#endif
#ifndef BG_AV_NODE_STATUS_HORDE_CONTESTED
#define BG_AV_NODE_STATUS_HORDE_CONTESTED 3
#endif

// === TEAM_INDEX_ aliases (cmangos) ===
// Penqle uses BG_TEAM_ALLIANCE/BG_TEAM_HORDE. cmangos uses TEAM_INDEX_ALLIANCE/HORDE/NEUTRAL.
#ifndef TEAM_INDEX_ALLIANCE
#define TEAM_INDEX_ALLIANCE BG_TEAM_ALLIANCE
#endif
#ifndef TEAM_INDEX_HORDE
#define TEAM_INDEX_HORDE BG_TEAM_HORDE
#endif
#ifndef TEAM_INDEX_NEUTRAL
#define TEAM_INDEX_NEUTRAL 2
#endif

// === IsAutocastable (cmangos free function) ===
// Penqle's Vanilla SpellEntry does not carry cmangos' separate
// SPELL_ATTR_EX_NO_AUTOCAST_AI bit. Pet::ToggleAutocast uses the same native
// rule available here: passive spells cannot be toggled, while non-passive pet
// spells may be placed in the autocast list.
inline bool IsAutocastable(SpellEntry const* spellInfo)
{
    return spellInfo && !spellInfo->IsPassiveSpell();
}

inline bool IsAutocastable(uint32 spellId)
{
    return IsAutocastable(sSpellMgr.GetSpellEntry(spellId));
}

// === IsSpellAppliesAura / IsSpellHaveEffect / IsAreaAuraEffect (cmangos free functions) ===
inline bool IsSpellAppliesAura(SpellEntry const* spellInfo, uint32 effectMask = 0xFFFFFFFF) {
    return spellInfo && spellInfo->IsSpellAppliesAura(effectMask);
}
inline bool IsSpellHaveEffect(SpellEntry const* spellInfo, uint32 effect) {
    if (!spellInfo) return false;
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i) {
        if (spellInfo->Effect[i] == effect) return true;
    }
    return false;
}
inline bool IsAreaAuraEffect(uint32 effect) {
    return effect == SPELL_EFFECT_APPLY_AREA_AURA_PARTY || effect == SPELL_EFFECT_APPLY_AREA_AURA_FRIEND
        || effect == SPELL_EFFECT_APPLY_AREA_AURA_ENEMY || effect == SPELL_EFFECT_APPLY_AREA_AURA_PET
        || effect == SPELL_EFFECT_APPLY_AREA_AURA_OWNER;
}

// === NAV_AREA_* / NAV_* (cmangos navmesh area types) ===
#ifndef NAV_AREA_WATER
#define NAV_AREA_WATER 7
#endif
#ifndef NAV_AREA_GROUND
#define NAV_AREA_GROUND 4
#endif
#ifndef NAV_AREA_GROUND_STEEP
#define NAV_AREA_GROUND_STEEP 3
#endif
#ifndef NAV_MAGMA_SLIME
#define NAV_MAGMA_SLIME 0x18
#endif
#ifndef NAV_GROUND_STEEP
#define NAV_GROUND_STEEP 0x02
#endif

// === CONDITION_FROM_AREATRIGGER_TELEPORT (cmangos) ===
#ifndef CONDITION_FROM_AREATRIGGER_TELEPORT
#define CONDITION_FROM_AREATRIGGER_TELEPORT 0
#endif

// === MINIMUM_LOOTING_TIME ===
#ifndef MINIMUM_LOOTING_TIME
#define MINIMUM_LOOTING_TIME 1000
#endif

// === AuctionHouseType (cmangos enum) ===
enum AuctionHouseType {
    AUCTION_HOUSE_ALLIANCE = 0,
    AUCTION_HOUSE_HORDE = 1,
    AUCTION_HOUSE_NEUTRAL = 2,
    MAX_AUCTION_HOUSE_TYPE = 3,
};

// === SPELL_INTERRUPT_FLAG_COMBAT ===
#ifndef SPELL_INTERRUPT_FLAG_COMBAT
#define SPELL_INTERRUPT_FLAG_COMBAT 0x10
#endif

// === SPELL_RANGE_FLAG_MELEE / RANGED (cmangos defines on SpellRangeEntry::Flags) ===
#ifndef SPELL_RANGE_FLAG_MELEE
#define SPELL_RANGE_FLAG_MELEE 1
#endif
#ifndef SPELL_RANGE_FLAG_RANGED
#define SPELL_RANGE_FLAG_RANGED 2
#endif

// === SPELL_ATTR_USES_RANGED_SLOT (cmangos) ===
#ifndef SPELL_ATTR_USES_RANGED_SLOT
#define SPELL_ATTR_USES_RANGED_SLOT 0x00000010
#endif

// === BG_AV_NODE_STATUS_NEUTRAL_OCCUPIED ===
#ifndef BG_AV_NODE_STATUS_NEUTRAL_OCCUPIED
#define BG_AV_NODE_STATUS_NEUTRAL_OCCUPIED 4
#endif

// === CREATURE_EXTRA_FLAG_INVISIBLE ===
#ifndef CREATURE_EXTRA_FLAG_INVISIBLE
#define CREATURE_EXTRA_FLAG_INVISIBLE 0x00040000
#endif

// === TARGET_FLAG_GAMEOBJECT (cmangos) ===
#ifndef TARGET_FLAG_GAMEOBJECT
#define TARGET_FLAG_GAMEOBJECT 0x800
#endif

// === TAXI_MOTION_TYPE (cmangos) → FLIGHT_MOTION_TYPE (Penqle) ===
#ifndef TAXI_MOTION_TYPE
#define TAXI_MOTION_TYPE FLIGHT_MOTION_TYPE
#endif

// === sAreaTriggerStore (cmangos) ===
struct CmangosAreaTriggerStoreProxy
{
    template<typename T = AreaTriggerEntry>
    T const* LookupEntry(uint32 id) const { return sObjectMgr.GetAreaTrigger(id); }
    uint32 GetNumRows() const { return m_upperBound.Get(sObjectMgr.GetAreaTriggersMap()); }

private:
    mutable CmangosMapUpperBoundCache<ObjectMgr::AreaTriggerMap> m_upperBound;
};
inline CmangosAreaTriggerStoreProxy sAreaTriggerStore;

// === LfgRoles / LfgRolePriority (cmangos) — bot module's own ClassRoles is similar ===
typedef ClassRoles LfgRoles;
typedef RolesPriority LfgRolePriority;

// === Taxi route view (cmangos has Taxi::Map for in-flight spline tracking) ===
// Penqle exposes the active route through Player::GetTaxi().GetTaxiPath(). Keep
// the donor-facing view local to the module while reading that live core path.
namespace Taxi {
    struct PathNode {
        uint32 mapId = 0; float x = 0, y = 0, z = 0;
    };
    class Map {
    public:
        explicit Map(TaxiPathNodeList const& path) : m_path(&path) {}

        bool empty() const { return !m_path || m_path->empty(); }

        PathNode const* back() const
        {
            if (empty())
                return nullptr;

            TaxiPathNodeEntry const& node = (*m_path)[m_path->size() - 1];
            m_back = { node.mapid, node.x, node.y, node.z };
            return &m_back;
        }

        PathNode const* front() const
        {
            if (empty())
                return nullptr;

            TaxiPathNodeEntry const& node = (*m_path)[0];
            m_front = { node.mapid, node.x, node.y, node.z };
            return &m_front;
        }

    private:
        TaxiPathNodeList const* m_path;
        mutable PathNode m_front;
        mutable PathNode m_back;
    };
}

// === Other small defines ===
#ifndef ITEM_FLAG_UNIQUE_EQUIPPABLE
#define ITEM_FLAG_UNIQUE_EQUIPPABLE 0
#endif
#ifndef LOOT_SLOT_NORMAL
#define LOOT_SLOT_NORMAL 0
#endif
#ifndef ROLL_DISENCHANT
#define ROLL_DISENCHANT 4
#endif
#ifndef SPELL_STATE_CHANNELING
#define SPELL_STATE_CHANNELING 3
#endif
#ifndef SKILL_FLAG_CAN_UNLEARN
#define SKILL_FLAG_CAN_UNLEARN 0x10
#endif

// === sScriptDevAIMgr (cmangos has ScriptDevAI; Penqle uses sScriptMgr) ===
// Preserve the donor call shape while delegating to the core's real gossip
// registry. This keeps creature gossip scripts visible to the bot instead of
// silently converting every callback into false.
struct CmangosScriptDevAIMgrAdapter {
    bool OnGossipHello(Player* player, Creature* creature)
    {
        return sScriptMgr.OnGossipHello(player, creature);
    }
};
inline CmangosScriptDevAIMgrAdapter sScriptDevAIMgr;

// === BG_AB GO/banner additional defines (cmangos) ===
#ifndef BG_AB_BANNER_ALLIANCE
#define BG_AB_BANNER_ALLIANCE 0
#endif
#ifndef BG_AB_BANNER_HORDE
#define BG_AB_BANNER_HORDE 1
#endif
#ifndef BG_AB_BANNER_CONTESTED_A
#define BG_AB_BANNER_CONTESTED_A 2
#endif
#ifndef BG_AB_BANNER_CONTESTED_H
#define BG_AB_BANNER_CONTESTED_H 3
#endif

// === BG WSG GO defines (cmangos) — Penqle uses BG_OBJECT_* maybe ===
#ifndef GO_WS_SILVERWING_FLAG
#define GO_WS_SILVERWING_FLAG 179830
#endif
#ifndef GO_WS_WARSONG_FLAG
#define GO_WS_WARSONG_FLAG 179831
#endif
#ifndef GO_WS_SILVERWING_FLAG_DROP
#define GO_WS_SILVERWING_FLAG_DROP 179785
#endif
#ifndef GO_WS_WARSONG_FLAG_DROP
#define GO_WS_WARSONG_FLAG_DROP 179786
#endif

// === BG WSG areatrigger defines (cmangos) ===
#ifndef WS_AT_SILVERWING_ROOM
#define WS_AT_SILVERWING_ROOM 3646
#endif
#ifndef WS_AT_WARSONG_ROOM
#define WS_AT_WARSONG_ROOM 3647
#endif

// === BG_AV node/banner defines (cmangos) ===
#ifndef BG_AV_NODE_CAPTAIN_DEAD_A
#define BG_AV_NODE_CAPTAIN_DEAD_A 0x10
#endif
#ifndef BG_AV_NODE_CAPTAIN_DEAD_H
#define BG_AV_NODE_CAPTAIN_DEAD_H 0x20
#endif
#ifndef BG_AV_GO_BANNER_ALLIANCE
#define BG_AV_GO_BANNER_ALLIANCE 178925
#endif
#ifndef BG_AV_GO_BANNER_ALLIANCE_CONT
#define BG_AV_GO_BANNER_ALLIANCE_CONT 178940
#endif
#ifndef BG_AV_GO_BANNER_HORDE
#define BG_AV_GO_BANNER_HORDE 178943
#endif
#ifndef BG_AV_GO_BANNER_HORDE_CONT
#define BG_AV_GO_BANNER_HORDE_CONT 178944
#endif
#ifndef BG_AV_GO_GY_BANNER_ALLIANCE
#define BG_AV_GO_GY_BANNER_ALLIANCE 180058
#endif
#ifndef BG_AV_GO_GY_BANNER_ALLIANCE_CONT
#define BG_AV_GO_GY_BANNER_ALLIANCE_CONT 180059
#endif
#ifndef BG_AV_GO_GY_BANNER_HORDE
#define BG_AV_GO_GY_BANNER_HORDE 180060
#endif
#ifndef BG_AV_GO_GY_BANNER_HORDE_CONT
#define BG_AV_GO_GY_BANNER_HORDE_CONT 180061
#endif
#ifndef BG_AV_GO_GY_BANNER_SNOWFALL
#define BG_AV_GO_GY_BANNER_SNOWFALL 180062
#endif

// === GetSpellCastResultString (cmangos free function) ===
// Penqle does not expose the donor's localized result-name table. Preserve the
// useful failure reason for the owner instead of collapsing every core result
// into one generic message.
inline char const* GetSpellCastResultString(SpellCastResult res)
{
    switch (res)
    {
    case SPELL_FAILED_NOT_READY: return "spell not ready";
    case SPELL_FAILED_REQUIRES_SPELL_FOCUS: return "requires spell focus";
    case SPELL_FAILED_REQUIRES_AREA: return "cannot cast here";
    case SPELL_FAILED_EQUIPPED_ITEM_CLASS:
    case SPELL_FAILED_EQUIPPED_ITEM_CLASS_MAINHAND:
    case SPELL_FAILED_EQUIPPED_ITEM_CLASS_OFFHAND: return "requires item or weapon";
    case SPELL_FAILED_NOT_INFRONT:
    case SPELL_FAILED_UNIT_NOT_INFRONT: return "must face the target";
    case SPELL_FAILED_NOT_STANDING: return "must be standing";
    case SPELL_FAILED_MOVING: return "cannot cast while moving";
    case SPELL_FAILED_OUT_OF_RANGE: return "target is out of range";
    case SPELL_FAILED_LINE_OF_SIGHT: return "target is not in line of sight";
    case SPELL_FAILED_NO_POWER: return "not enough power";
    case SPELL_FAILED_AFFECTING_COMBAT: return "cannot cast in combat";
    case SPELL_FAILED_NOT_MOUNTED: return "must be mounted";
    case SPELL_FAILED_PREVENTED_BY_MECHANIC: return "prevented by a mechanic";
    case SPELL_FAILED_BAD_TARGETS: return "invalid target";
    default: return "spell cast failed";
    }
}

// === TARGET_FLAG_LOCKED / SPELL_STATE_TARGETING (cmangos) ===
#ifndef TARGET_FLAG_LOCKED
#define TARGET_FLAG_LOCKED 0x100
#endif
#ifndef SPELL_STATE_TARGETING
#define SPELL_STATE_TARGETING 0
#endif

// === DIST_CALC_COMBAT_REACH_WITH_MELEE / MAX_GOSSIP_TEXT_OPTIONS ===
#ifndef DIST_CALC_COMBAT_REACH_WITH_MELEE
#define DIST_CALC_COMBAT_REACH_WITH_MELEE 3
#endif
#ifndef MAX_GOSSIP_TEXT_OPTIONS
#define MAX_GOSSIP_TEXT_OPTIONS 8
#endif

// === FACTION_GROUP_MASK ===
#ifndef FACTION_GROUP_MASK_ALLIANCE
#define FACTION_GROUP_MASK_ALLIANCE 0x4
#endif
#ifndef FACTION_GROUP_MASK_HORDE
#define FACTION_GROUP_MASK_HORDE 0x2
#endif

// === BG_AB_BANNER_* / BG_AB_NODE_STATUS_NEUTRAL (cmangos) ===
// Penqle has these in BattleGroundAB.h but with different naming.
#ifndef BG_AB_NODE_STATUS_NEUTRAL
#define BG_AB_NODE_STATUS_NEUTRAL 0
#endif
#ifndef BG_AB_BANNER_STABLE
#define BG_AB_BANNER_STABLE 0
#endif
#ifndef BG_AB_BANNER_BLACKSMITH
#define BG_AB_BANNER_BLACKSMITH 1
#endif
#ifndef BG_AB_BANNER_FARM
#define BG_AB_BANNER_FARM 2
#endif
#ifndef BG_AB_BANNER_LUMBER_MILL
#define BG_AB_BANNER_LUMBER_MILL 3
#endif
#ifndef BG_AB_BANNER_MINE
#define BG_AB_BANNER_MINE 4
#endif

// === SEC_GAMEMASTER alias ===
// Penqle has no intermediate GM rank: SEC_ADMINISTRATOR is the first elevated
// account level. Native command ownership deliberately uses that level for
// the documented GM override.
#ifndef SEC_GAMEMASTER
#define SEC_GAMEMASTER SEC_ADMINISTRATOR
#endif

// === FORCED_MOVEMENT_RUN / ForcedMovement (cmangos) ===
// Penqle uses different movement-flag set; bot only checks symbolic value.
typedef int ForcedMovement;
#ifndef FORCED_MOVEMENT_RUN
#define FORCED_MOVEMENT_RUN 1
#endif
#ifndef FORCED_MOVEMENT_WALK
#define FORCED_MOVEMENT_WALK 0
#endif
#ifndef FORCED_MOVEMENT_FLIGHT
#define FORCED_MOVEMENT_FLIGHT 2
#endif

// === SkillLineAbility store proxy ===
// cmangos exposes sSkillLineAbilityStore (DBCStorage<SkillLineAbilityEntry>);
// Penqle exposes sObjectMgr.GetSkillLineAbility(id).
struct SkillLineAbilityEntry;
struct CmangosSkillLineAbilityStoreProxy
{
    template<typename T = SkillLineAbilityEntry>
    T const* LookupEntry(uint32 id) const { return sObjectMgr.GetSkillLineAbility(id); }
    uint32 GetMaxEntry() const { return sObjectMgr.GetMaxSkillLineAbilityId(); }
    uint32 GetNumRows() const { return GetMaxEntry(); }
};
inline CmangosSkillLineAbilityStoreProxy sSkillLineAbilityStore;

// === sAreaStore proxy (cmangos uses sAreaStore; Penqle has sAreaStorage) ===
// Same shape; keep both names.
#define sAreaStore sAreaStorage

// === sChatChannelsStore proxy ===
// Tortoise exposes the loaded ChatChannels data through ObjectMgr accessors.
// Use that source instead of returning an empty donor-shaped store: the native
// client/core already owns the channel definitions and JoinChatChannels can
// make an informed decision from them.
struct CmangosChatChannelsStoreProxy
{
    template<typename T = ChatChannelsEntry>
    T const* LookupEntry(uint32 id) const { return sObjectMgr.GetChannelEntryFor(id); }
    uint32 GetNumRows() const { return 0; }
};
inline CmangosChatChannelsStoreProxy sChatChannelsStore;

// === sGOStorage (cmangos) → sObjectMgr.GetGameObjectInfo ===
struct CmangosGOStorageProxy
{
    template<typename T = GameObjectInfo>
    T const* LookupEntry(uint32 id) const { return sObjectMgr.GetGameObjectInfo(id); }
    uint32 GetMaxEntry() const { return sObjectMgr.GetMaxGameObjectInfoEntry() + 1; }
};
inline CmangosGOStorageProxy sGOStorage;

// === sTaxiNodesStore (cmangos) → sObjectMgr.GetTaxiNodeEntry ===
struct TaxiNodesEntry;
struct CmangosTaxiNodesStoreProxy
{
    template<typename T = TaxiNodesEntry>
    T const* LookupEntry(uint32 id) const { return sObjectMgr.GetTaxiNodeEntry(id); }
    uint32 GetNumRows() const { return sObjectMgr.GetMaxTaxiNodeId(); }
};
inline CmangosTaxiNodesStoreProxy sTaxiNodesStore;

// === sLootMgr shim (cmangos global; Penqle has LootStore but no equivalent singleton) ===
// Bot calls sLootMgr.GetLoot(player[, guid]) to fetch the loot the player is currently looking at.
// Penqle stores the Loot object directly on the looted entity (Creature/GameObject/Item/Corpse),
// resolved by the player's current loot guid. This MUST return the real loot: StoreLootAction
// consumes it to actually take items + release. A previous nullptr stub made StoreLoot abort
// before sending CMSG_LOOT_RELEASE, so bots kneeled on an open corpse forever ("crouch loop").
// Mirrors the core resolution in Handlers/LootHandler.cpp (no distance gate: StoreLoot is reacting
// to a server loot response, so the target is already validated).
struct CmangosLootMgrAdapter
{
    Loot* GetLoot(Player* player, ObjectGuid guid = ObjectGuid()) const
    {
        if (!player || !player->IsInWorld())
            return nullptr;

        if (!guid)
            guid = player->GetLootGuid();
        if (!guid)
            return nullptr;

        switch (guid.GetHigh())
        {
            case HIGHGUID_GAMEOBJECT:
                if (GameObject* go = player->GetMap()->GetGameObject(guid))
                    return &go->loot;
                break;
            case HIGHGUID_CORPSE:
                if (Corpse* bones = player->GetMap()->GetCorpse(guid))
                    return &bones->loot;
                break;
            case HIGHGUID_ITEM:
                if (Item* item = player->GetItemByGuid(guid))
                    return &item->loot;
                break;
            case HIGHGUID_UNIT:
                if (Creature* creature = player->GetMap()->GetCreature(guid))
                    return &creature->loot;
                break;
            default:
                break;
        }

        return nullptr;
    }
};
inline CmangosLootMgrAdapter sLootMgr;

// === Map::GetHitPosition forwarder (cmangos name) ===
// Penqle uses GetLosHitPosition. The bot module's call sites were patched at
// the source level (TravelMgr.cpp / WorldPosition.h).

// === Free-function helpers (cmangos style) wrapping Penqle SpellEntry methods ===
// cmangos exposes these as free functions; Penqle wraps them in SpellEntry::method.
inline uint32 GetSpellCastTime(SpellEntry const* spellInfo, Spell const* spell = nullptr) {
    return spellInfo ? spellInfo->GetCastTime(nullptr, const_cast<Spell*>(spell)) : 0;
}
// 3-arg form: cmangos signature is GetSpellCastTime(SpellEntry, caster, Spell).
inline uint32 GetSpellCastTime(SpellEntry const* spellInfo, WorldObject* caster, Spell const* spell = nullptr) {
    return spellInfo ? spellInfo->GetCastTime(caster, const_cast<Spell*>(spell)) : 0;
}
// IsNextMeleeSwingSpell: cmangos free function checking SPELL_ATTR_ON_NEXT_SWING_1/_2.
inline bool IsNextMeleeSwingSpell(SpellEntry const* spellInfo) {
    return spellInfo && (spellInfo->Attributes & (SPELL_ATTR_ON_NEXT_SWING_1 | SPELL_ATTR_ON_NEXT_SWING_2));
}
inline uint32 GetSpellRecoveryTime(SpellEntry const* spellInfo) {
    return spellInfo ? spellInfo->GetRecoveryTime() : 0;
}
inline int32 GetSpellDuration(SpellEntry const* spellInfo) {
    return spellInfo ? spellInfo->GetDuration() : 0;
}
inline bool IsChanneledSpell(SpellEntry const* spellInfo) {
    return spellInfo && spellInfo->IsChanneledSpell();
}
inline SpellSchoolMask GetSpellSchoolMask(SpellEntry const* spellInfo) {
    return spellInfo ? SpellSchoolMask(spellInfo->GetSpellSchoolMask()) : SpellSchoolMask(0);
}
inline bool IsNonCombatSpell(SpellEntry const* spellInfo) {
    return spellInfo && spellInfo->IsNonCombatSpell();
}
inline bool IsPositiveEffect(SpellEntry const* spellInfo, SpellEffectIndex eff) {
    return spellInfo && spellInfo->IsPositiveEffect(eff);
}

// === GetAreaEntryByAreaID free function (cmangos) → AreaEntry::GetById (Penqle) ===
inline AreaEntry const* GetAreaEntryByAreaID(uint32 id) { return AreaEntry::GetById(id); }
inline AreaEntry const* GetAreaEntryByMapId(uint32 mapId) {
    auto* mapEntry = sMapStorage.LookupEntry<MapEntry>(mapId);
    return mapEntry ? AreaEntry::GetById(mapEntry->linkedZone) : nullptr;
}

// === LFGQueue ===
// Penqle has its own LFGQueue in src/game/LFG/LFGMgr.h with stub methods added.
// World::GetLFGQueue() forwards to sLFGMgr. Bot module uses the existing types.

// === GetSpellStore (cmangos) → sSpellMgr (Penqle) ===
// cmangos exposes a global GetSpellStore() returning the DBC store as a POINTER.
inline CmangosSpellTemplateProxy* GetSpellStore() { return &sSpellTemplate; }

// === GetApplicationStartTime (cmangos) — free function returning startup timestamp ===
inline std::chrono::system_clock::time_point GetApplicationStartTime() {
    static auto s_start = std::chrono::system_clock::now();
    return s_start;
}

// === GetTeamIndexByTeamId (cmangos) → BattleGround static method ===
// Provide free-function forwarder. (BattleGround.h has it as a static.)
inline BattleGroundTeamIndex GetTeamIndexByTeamId(Team team) {
    return team == ALLIANCE ? BG_TEAM_ALLIANCE : BG_TEAM_HORDE;
}

// === GetRandomGenerator (cmangos) === stub: cmangos has its own thread-local PRNG;
// Penqle uses urand/frand. Bot module's TravelMgr seeds a default_random_engine via this.
// Returns pointer-style — bot does *GetRandomGenerator() in some sites.
inline std::mt19937* GetRandomGenerator() {
    thread_local std::mt19937 s_rng(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    return &s_rng;
}

// === Loot status flags (cmangos LootMgr.h) ===
// Bot's LootValues.cpp returns bitflags describing loot state. Penqle has no equivalent
// (its Loot just exposes items/gold). Define as bitflags so bot computes a value (which
// is consumed only on the bot side via AI_VALUE comparisons; runtime semantic is harmless).
#ifndef LOOT_STATUS_FAKE_LOOT
enum LootStatusFlags : uint32 {
    LOOT_STATUS_FAKE_LOOT              = 0x01,
    LOOT_STATUS_CONTAIN_GOLD           = 0x02,
    LOOT_STATUS_NOT_FULLY_LOOTED       = 0x04,
    LOOT_STATUS_CONTAIN_FFA            = 0x08,
    LOOT_STATUS_CONTAIN_RELEASED_ITEMS = 0x10,
};
#endif

// === SPELL_ATTR_ON_NEXT_SWING aliases ===
// cmangos has SPELL_ATTR_ON_NEXT_SWING / _NO_DAMAGE; Penqle has SPELL_ATTR_ON_NEXT_SWING_1/_2.
#ifndef SPELL_ATTR_ON_NEXT_SWING
#define SPELL_ATTR_ON_NEXT_SWING SPELL_ATTR_ON_NEXT_SWING_1
#endif
#ifndef SPELL_ATTR_ON_NEXT_SWING_NO_DAMAGE
#define SPELL_ATTR_ON_NEXT_SWING_NO_DAMAGE SPELL_ATTR_ON_NEXT_SWING_2
#endif

// === UNIT_FLAG_UNTARGETABLE / UNIT_FLAG_UNINTERACTIBLE (cmangos names) ===
// Penqle uses UNIT_FLAG_NOT_SELECTABLE for both concepts.
#ifndef UNIT_FLAG_UNTARGETABLE
#define UNIT_FLAG_UNTARGETABLE UNIT_FLAG_NOT_SELECTABLE
#endif
#ifndef UNIT_FLAG_UNINTERACTIBLE
#define UNIT_FLAG_UNINTERACTIBLE UNIT_FLAG_NOT_SELECTABLE
#endif

// === IsAutoRepeatRangedSpell (cmangos free function) ===
// Penqle's SpellEntry has IsAutoRepeatRangedSpell as a method. Wrap as free fn.
inline bool IsAutoRepeatRangedSpell(SpellEntry const* spellInfo) {
    return spellInfo && (spellInfo->AttributesEx2 & SPELL_ATTR_EX2_AUTOREPEAT_FLAG);
}

// === Penqle/CMaNGOS spelling compatibility ===
// These are module-local aliases only; they never enter the Tortoise core.
#ifndef getObjectGuid
#define getObjectGuid GetObjectGuid
#endif
#ifndef getPositionX
#define getPositionX GetPositionX
#define getPositionY GetPositionY
#define getPositionZ GetPositionZ
#define getOrientation GetOrientation
#endif
#ifndef getOpcode
#define getOpcode GetOpcode
#endif
#ifndef getZoneId
#define getZoneId GetZoneId
#endif
#ifndef GetSource
#define GetSource getSource
#endif
#ifndef IsRaidGroup
#define IsRaidGroup isRaidGroup
#endif
#ifndef IsSpellReady
#define IsSpellReady HasSpellCooldown
#endif
#ifndef getDistance2d
#define getDistance2d GetDistance2d
#endif
#ifndef getXPForLevel
#define getXPForLevel GetXPForLevel
#endif

#ifndef LogCommon_h
#define LogCommon_h
#endif
// RandomBotFacade.h owns the sRandomBotFacade name. Do not install a
// header-only stub here: the module-local facade implementation must be the
// single behavior-facing owner, while BotManager remains the session owner.
// === Headless transport helpers ===
inline bool isRealPlayer_Helper(Player* p) {
    return p && p->GetSession() && p->GetSession()->GetSocket() != nullptr;
}
inline bool IsInGroup_Helper(Player* a, Player* b, bool sameGroup=false) { if (!a || !b) return false; Group* g = a->GetGroup(); if (!g) return false; if (sameGroup) { Group* og = b->GetGroup(); return g == og; } return g->IsMember(b->GetObjectGuid()); }
inline bool IsRealPlayer_Helper(Player* p) { return isRealPlayer_Helper(p); }
