// Minimal TortoiseBots shim - provides only what the foundational Engine needs.
// Full Shyalya shim is at cmangos-compat-shim.h.orig - this minimal version
// is used for the first checkpoint to keep the PCH tractable. As more
// subsystems are ported, extend this shim incrementally.

#pragma once

#include <future>
#include <chrono>
#include <random>
#include <cstdio>

class Transport;
typedef Transport GenericTransport;

class CreatureAI;
typedef CreatureAI UnitAI;

#include "ObjectGuid.h"
typedef ObjectGuidSet GuidSet;

struct AreaEntry;
typedef AreaEntry AreaTableEntry;

struct AreaTriggerEntry;
typedef AreaTriggerEntry AreaTrigger;

#ifndef ITEM_CLASS_MISC
#define ITEM_CLASS_MISC ITEM_CLASS_JUNK
#endif

#ifndef DEFAULT_MAX_LEVEL
#define DEFAULT_MAX_LEVEL 60
#endif

#ifndef TEAM_BOTH_ALLOWED
#define TEAM_BOTH_ALLOWED TEAM_NONE
#endif

#ifndef DIST_CALC_COMBAT_REACH
enum DistanceCalculation { DIST_CALC_NONE = 0, DIST_CALC_BOUNDING_RADIUS = 1, DIST_CALC_COMBAT_REACH = 2 };
#endif

#ifndef UNIT_FLAG_CLIENT_CONTROL_LOST
#define UNIT_FLAG_CLIENT_CONTROL_LOST 0
#endif

#ifndef movementFlagsMask
constexpr uint32 movementFlagsMask = 0xFFFFFFFFu;
#endif

class BarGoLink {
public:
    BarGoLink() {}
    template<typename T> BarGoLink(T /*total*/) {}
    void step() {}
    void Step() {}
    static void SetOutputState(bool /*state*/) {}
};

struct InstanceTemplate {
    uint32 levelMin = 0;
    uint32 levelMax = 0;
    uint32 maxPlayers = 0;
    uint32 reset_delay = 0;
    uint32 parent = 0;
};

// Minimal DBC proxies needed for Engine
struct CmangosSpellTemplateProxy
{
    template<typename T = SpellEntry>
    T const* LookupEntry(uint32 id) const { return sSpellMgr.GetSpellEntry(id); }
    uint32 GetMaxEntry() const { return sSpellMgr.GetMaxSpellId(); }
};
inline CmangosSpellTemplateProxy sSpellTemplate;

struct CmangosItemStorageProxy
{
    template<typename T = ItemPrototype>
    T const* LookupEntry(uint32 id) const { return sObjectMgr.GetItemPrototype(id); }
    uint32 GetMaxEntry() const { return 100000; }
};
inline CmangosItemStorageProxy sItemStorage;

// WorldPosition field mapping is handled directly in WorldPosition.h via
// explicit Penqle names (mapId/x/y/z/o) rather than macros, because macros
// break setX(const float x) { coord_x = x; } -> x = x and WorldObject::GetPositionX().
// See ai/playerbot/WorldPosition.h for the Penqle-adapted version.

// Talent helper is in Talentspec.cpp, not here

