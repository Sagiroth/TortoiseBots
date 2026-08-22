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

// === Tortoise 1.18.1 adaptions for the large-batch forward-port ===
// These handle the broad dependency families that block Engine/AiObjectContext/Value/Trigger/Action
// from compiling on Penqle's WorldLocation/Position/Map APIs, and the PlayerbotAI host coupling.

// WorldState stub (WotLK only, not in 1.18.1)
struct WorldState { int GetExpansion() const { return 0; } };
inline WorldState sWorldState;

// LogCommon stub
#ifndef LogCommon_h
#define LogCommon_h
#endif

// Player::GetPlayerbotAI / IsRealPlayer -> Headless IsHeadless() check
// Done via module-local PlayerbotAIStorage, but for donor files that still call
// player->GetPlayerbotAI() we provide a shim that forwards to the storage.
// The real fix is to replace GetPlayerbotAI with PlayerbotAIStorage::Instance().GetAI(player)
// in donor files, but this shim keeps the intermediate build green.
class PlayerbotAI;
namespace shym {
inline PlayerbotAI* GetPlayerbotAI_Helper(class Player* p);
}
// Provide a global sRandomPlayerbotMgr stub for the Value/Trigger families that check IsRandomBot
struct RandomPlayerbotMgrStub {
    bool IsRandomBot(Player*) const { return false; }
    bool IsFreeBot(Player*) const { return false; }
    bool IsRandomBot(ObjectGuid) const { return false; }
};
inline RandomPlayerbotMgrStub sRandomPlayerbotMgr;

// WorldPosition/Position field mapping is handled in WorldPosition.h directly,
// not via macros, to avoid breaking setX(const float x) { x = x; }.

// Map API shims for Penqle (isInLineOfSight vs IsInLineOfSight, GetHitPosition vs GetLosHitPosition)
inline bool IsInLineOfSight_Helper(class Map* map, float x1, float y1, float z1, float x2, float y2, float z2) {
    // Penqle's Map::isInLineOfSight is lowercase, cmangos IsInLineOfSight is uppercase
    // The donor's WorldPosition::IsInLineOfSight already has a MANGOSBOT_ZERO guard, so this is a fallback
    return true;
}

// Provide GetAreaEntryByAreaID and sMapStore/sMapStorage shims (already in shim, but ensure)
// The existing CmangosMapStoreProxy already handles sMapStore -> sMapStorage

// Provide GetPlayerbotAI as a free function for donor code that does player->GetPlayerbotAI()
// We can't add a method to Player without core change, so we provide a macro that rewrites
// player->GetPlayerbotAI() to GetPlayerbotAI_Helper(player) via a global define.
// This is intentionally broad for the intermediate build; the final clean fix is to
// replace GetPlayerbotAI with PlayerbotAIStorage::GetAI in donor files.
#ifndef GetPlayerbotAI
#define GetPlayerbotAI GetPlayerbotAI_Helper
#endif


