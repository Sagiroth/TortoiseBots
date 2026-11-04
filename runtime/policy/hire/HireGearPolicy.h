#pragma once

// Issue #490 PR 1: static best-in-slot sets for level-60 hires. The offline
// generator (tools/hire_gear/, owned by another worker) fills
// `tortoise_bots_hire_gear` once; the server only reads it at hire time.
//
// Pure policy: no core/engine headers, unit-tested standalone
// (tools/test_hire_gear_policy.cpp). The runtime cache
// (runtime/HireGearCache.{h,cpp}) owns the DB load; the provisioner
// (HireProvisionService::ProvisionHeavy) owns the equip pass.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../../HireSpecPolicy.h"
#include "HireRespecPolicy.h"

namespace TortoiseBots
{

// Gear tiers 1..7 (0 = today's random hire gear, untouched). PR 2 adds
// per-player unlocks; until then the effective hire tier is HireGearStartTier.
constexpr uint8_t kHireGearTierBase = 0;
constexpr uint8_t kHireGearTierMax = 7;
constexpr uint8_t kHireGearHireLevel = 60;

// Canonical spec keys for the hire-gear table. One per recruiter spec-menu
// option (29 total), in HireSpecOptions order: every HireSpecPolicy pathName
// except druid, where Bear and Cat share the "feral" premade path but need
// distinct sets ("bear"/"cat"). The generator must emit exactly these keys.
inline char const* HireGearSpecKey(uint8_t classId, int specIndex)
{
    uint32_t count = 0;
    HireSpecOption const* options = HireSpecOptions(classId, count);
    if (!options || specIndex < 0 || static_cast<uint32_t>(specIndex) >= count)
        return nullptr;
    if (classId == kHireClassDruid)
    {
        // Menu order: Bear, Restoration, Balance, Cat.
        static char const* const druidKeys[] = { "bear", "restoration", "balance", "cat" };
        return druidKeys[specIndex];
    }
    return options[specIndex].pathName;
}

// Role-only requests (`.bot hire` without a gossip spec step, specIndex -1)
// carry no menu position: pin them to the menu's first option for the role so
// the same input always resolves to the same key.
inline char const* HireGearRoleFallbackKey(uint8_t classId, uint8_t role)
{
    uint32_t count = 0;
    HireSpecOption const* options = HireSpecOptions(classId, count);
    if (!options)
        return nullptr;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (options[i].role == role)
            return HireGearSpecKey(classId, static_cast<int>(i));
    }
    return nullptr;
}

// Spec key for a hire: the menu position when known, else the role fallback.
// Returns nullptr for an unknown class (caller keeps today's gear logic).
inline char const* ResolveHireGearSpecKey(uint8_t classId, int specIndex, uint8_t role)
{
    char const* key = HireGearSpecKey(classId, specIndex);
    if (key)
        return key;
    return HireGearRoleFallbackKey(classId, role);
}

// Spec key for a bot's own premade path (its talent spec). An empty path or
// one that matches no menu option passes -1, so the role decides.
inline char const* ResolveHireGearKeyForPath(uint8_t classId, char const* pathName, uint8_t role)
{
    int const specIndex = pathName && *pathName ? HireSpecIndexForPathName(classId, pathName, role) : -1;
    return ResolveHireGearSpecKey(classId, specIndex, role);
}

// Effective hire tier for PR 1: the master switch gates everything, StartTier
// (0..7, clamped) is what every player has. 0 keeps today's behaviour.
inline uint8_t EffectiveHireGearTier(bool enabled, uint32_t startTier)
{
    if (!enabled)
        return kHireGearTierBase;
    if (startTier > kHireGearTierMax)
        return kHireGearTierMax;
    return static_cast<uint8_t>(startTier);
}

// Eligibility for the static pass: enabled, level 60, tier >= 1. Pool bots,
// hires below 60 and tier 0 keep today's behaviour untouched.
inline bool HireGearTierEligible(uint8_t level, bool enabled, uint8_t tier)
{
    return enabled && level == kHireGearHireLevel && tier >= 1 && tier <= kHireGearTierMax;
}

// One table row, as loaded by the runtime cache.
struct HireGearRow
{
    uint8_t classId = 0;
    char spec[16] = {};
    uint8_t race = 0;
    uint8_t tier = 0;
    uint8_t slot = 0;
    uint32_t itemId = 0;
    uint32_t enchantSpell = 0;
};

// Lookup over a row array: the race-specific row wins, else the race-0 row.
// A missing row means "fall back to today's gear logic for that slot".
// Pure and deterministic: the same input always finds the same row.
inline bool LookupHireGearRow(HireGearRow const* rows, size_t count,
    uint8_t classId, char const* spec, uint8_t race, uint8_t tier, uint8_t slot,
    HireGearRow const*& out)
{
    out = nullptr;
    if (!rows || !spec)
        return false;
    HireGearRow const* generic = nullptr;
    for (size_t i = 0; i < count; ++i)
    {
        HireGearRow const& row = rows[i];
        if (row.classId != classId || row.tier != tier || row.slot != slot)
            continue;
        if (std::strcmp(row.spec, spec) != 0)
            continue;
        if (row.race == race)
        {
            out = &row;
            return true;
        }
        if (row.race == 0)
            generic = &row;
    }
    if (generic)
    {
        out = generic;
        return true;
    }
    return false;
}

} // namespace TortoiseBots
