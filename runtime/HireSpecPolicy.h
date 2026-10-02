#pragma once

// Issue #386: the "<Mercenary Hire>" spec step must be honoured for every
// class, not only for druids. The gossip menu position (specIndex, SpecsFor
// order) selects a premade talent path by name, so the recruiter menu and the
// provisioner read the same table and can never disagree about which build an
// option means. An "Arms (DPS)" warrior therefore arrives with Arms talents,
// not a coin flip against Fury.
//
// Pure policy: no core/engine headers, unit-tested standalone
// (tools/test_hire_spec_policy.cpp).

#include <cstdint>
#include <string>

namespace TortoiseBots
{

// ai::BotRoles bit values (ChatHelper.h), repeated so this header stays
// core-free; HireProvisionService.cpp static_asserts them against the enum.
constexpr uint8_t kHireRoleTank = 0x01;
constexpr uint8_t kHireRoleHealer = 0x02;
constexpr uint8_t kHireRoleDps = 0x04;

// Core class ids (SharedDefines.h), likewise repeated and asserted there.
constexpr uint8_t kHireClassWarrior = 1;
constexpr uint8_t kHireClassPaladin = 2;
constexpr uint8_t kHireClassHunter = 3;
constexpr uint8_t kHireClassRogue = 4;
constexpr uint8_t kHireClassPriest = 5;
constexpr uint8_t kHireClassShaman = 7;
constexpr uint8_t kHireClassMage = 8;
constexpr uint8_t kHireClassWarlock = 9;
constexpr uint8_t kHireClassDruid = 11;

// One recruiter spec option. pathName is matched as a substring of the
// premade path name loaded from AiPlayerbot.PremadeSpecName.<class>.<n>
// (PlayerbotAIConfig.classSpecs), exactly as ChangeTalentsAction does.
struct HireSpecOption
{
    char const* label;
    uint8_t role;
    char const* pathName;
};

// Menu options for a hireable class, in gossip order. Unknown/unhireable
// classes yield nullptr and count 0.
inline HireSpecOption const* HireSpecOptions(uint8_t classId, uint32_t& count)
{
    static HireSpecOption const warrior[] = {
        { "Protection (Tank)", kHireRoleTank, "protection" },
        { "Arms (DPS)", kHireRoleDps, "arms" },
        { "Fury (DPS)", kHireRoleDps, "fury" },
    };
    static HireSpecOption const paladin[] = {
        { "Protection (Tank)", kHireRoleTank, "protection" },
        { "Holy (Healer)", kHireRoleHealer, "holy" },
        { "Retribution (DPS)", kHireRoleDps, "retribution" },
    };
    static HireSpecOption const hunter[] = {
        { "Beast Mastery (DPS)", kHireRoleDps, "beastmastery" },
        { "Marksmanship (DPS)", kHireRoleDps, "marksmanship" },
        { "Survival (DPS)", kHireRoleDps, "survival" },
    };
    static HireSpecOption const rogue[] = {
        { "Assassination (DPS)", kHireRoleDps, "assassination" },
        { "Combat (DPS)", kHireRoleDps, "combat" },
        { "Subtlety (DPS)", kHireRoleDps, "subtlety" },
    };
    static HireSpecOption const priest[] = {
        { "Holy (Healer)", kHireRoleHealer, "holy" },
        { "Discipline (Healer)", kHireRoleHealer, "discipline" },
        { "Shadow (DPS)", kHireRoleDps, "shadow" },
    };
    static HireSpecOption const shaman[] = {
        { "Restoration (Healer)", kHireRoleHealer, "restoration" },
        { "Elemental (DPS)", kHireRoleDps, "elemental" },
        { "Enhancement (DPS)", kHireRoleDps, "enhancement" },
    };
    static HireSpecOption const mage[] = {
        { "Frost (DPS)", kHireRoleDps, "frost" },
        { "Fire (DPS)", kHireRoleDps, "fire" },
        { "Arcane (DPS)", kHireRoleDps, "arcane" },
    };
    static HireSpecOption const warlock[] = {
        { "Affliction (DPS)", kHireRoleDps, "affliction" },
        { "Demonology (DPS)", kHireRoleDps, "demonology" },
        { "Destruction (DPS)", kHireRoleDps, "destruction" },
    };
    // Bear and Cat share the single feral premade path; the role bit picks the
    // tank or DPS kit on top of it.
    static HireSpecOption const druid[] = {
        { "Feral Bear (Tank)", kHireRoleTank, "feral" },
        { "Restoration (Healer)", kHireRoleHealer, "restoration" },
        { "Balance (DPS)", kHireRoleDps, "balance" },
        { "Feral Cat (DPS)", kHireRoleDps, "feral" },
    };

    switch (classId)
    {
        case kHireClassWarrior: count = 3; return warrior;
        case kHireClassPaladin: count = 3; return paladin;
        case kHireClassHunter: count = 3; return hunter;
        case kHireClassRogue: count = 3; return rogue;
        case kHireClassPriest: count = 3; return priest;
        case kHireClassShaman: count = 3; return shaman;
        case kHireClassMage: count = 3; return mage;
        case kHireClassWarlock: count = 3; return warlock;
        case kHireClassDruid: count = 4; return druid;
        default: count = 0; return nullptr;
    }
}

// Premade path-name fragment for a gossip spec index. nullptr when the index
// is out of range or the class is unknown: a role-only request (`.bot hire`)
// leaves specIndex at -1 and must stay role-driven.
inline char const* HireSpecPathName(uint8_t classId, int specIndex)
{
    if (specIndex < 0)
        return nullptr;
    uint32_t count = 0;
    HireSpecOption const* options = HireSpecOptions(classId, count);
    if (!options || static_cast<uint32_t>(specIndex) >= count)
        return nullptr;
    return options[specIndex].pathName;
}

// Post-provision check: does the build that was applied honour the request?
// A role-only request (empty/null name) is honoured by any role-matching
// build; a named request is honoured only by a build whose name contains it,
// so a silent fall back to the role is detectable.
inline bool HireSpecHonoured(char const* requestedPathName, char const* appliedPathName)
{
    if (!requestedPathName || !*requestedPathName)
        return true;
    if (!appliedPathName)
        return false;
    return std::string(appliedPathName).find(requestedPathName) != std::string::npos;
}

} // namespace TortoiseBots
