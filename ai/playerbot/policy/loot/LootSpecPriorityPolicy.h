#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Pure policy for loot roll spec priority:
// An item goes first to the group member whose spec it is made for.
// Only when nobody like that is in the group may any bot NEED it as a plain upgrade.
//
// 1. Item "primary stat": derived from item stats (agility / strength /
//    intellect+spell power+healing / stamina+defense+armor for tanks).
// 2. Spec "primary stat": derived from the weight scales (ai_playerbot_weightscale_data).
// 3. A member "fits better" when the item's primary stat is that member's
//    spec primary stat and NOT the rolling bot's.
//    Tie-break for weapons when both share the primary stat: a class whose
//    damage comes from the weapon itself (rogue, warrior, retribution paladin,
//    enhancement shaman) beats a stat-stick user (hunter melee slot, feral druid, casters).
// 4. For another BOT member: it only counts if the item is also spec-legal
//    and an upgrade for that bot (its own item usage says equip).
//    For a real PLAYER member: spec is unknown, so use the class
//    (rogue = agility, warrior = strength/tank, mage = intellect, hybrids = all possible)
//    and that they can use the item.
// 5. If a better-fitting member exists, the bot steps down to GREED ("<Name> fits it better").
//
// Kept literal and core-free so standalone g++ policy tests can include it.

constexpr uint32_t SPEC_PRIORITY_CLASS_WARRIOR = 1;
constexpr uint32_t SPEC_PRIORITY_CLASS_PALADIN = 2;
constexpr uint32_t SPEC_PRIORITY_CLASS_HUNTER = 3;
constexpr uint32_t SPEC_PRIORITY_CLASS_ROGUE = 4;
constexpr uint32_t SPEC_PRIORITY_CLASS_PRIEST = 5;
constexpr uint32_t SPEC_PRIORITY_CLASS_SHAMAN = 7;
constexpr uint32_t SPEC_PRIORITY_CLASS_MAGE = 8;
constexpr uint32_t SPEC_PRIORITY_CLASS_WARLOCK = 9;
constexpr uint32_t SPEC_PRIORITY_CLASS_DRUID = 11;

namespace ai
{
    enum class ItemStatFamily : uint8_t
    {
        None = 0,
        Agility,
        Strength,
        Spell,
        Tank
    };

    inline const char* StatFamilyToString(ItemStatFamily family)
    {
        switch (family)
        {
        case ItemStatFamily::Agility:
            return "Agility";
        case ItemStatFamily::Strength:
            return "Strength";
        case ItemStatFamily::Spell:
            return "Spell";
        case ItemStatFamily::Tank:
            return "Tank";
        default:
            return "None";
        }
    }

    struct ItemStatSummary
    {
        uint32_t agility = 0;
        uint32_t strength = 0;
        uint32_t spell = 0;
        uint32_t tank = 0;
        uint32_t stamina = 0;
    };

    // Derive an item's primary stat family from its aggregated stats.
    // Pure stamina or untyped items return None (no clear primary stat).
    inline ItemStatFamily DeriveItemStatFamily(ItemStatSummary const& stats)
    {
        // Tank stats: defense rating, dodge, parry, block, shield block value, bonus armor
        if (stats.tank > 0 && stats.tank >= stats.spell && stats.tank >= stats.strength && stats.tank >= stats.agility)
            return ItemStatFamily::Tank;

        // Spell stats: intellect, spirit, spell damage, spell healing, spell crit/hit/pen, mp5
        if (stats.spell > 0 && stats.spell > stats.strength && stats.spell > stats.agility)
            return ItemStatFamily::Spell;

        // Physical stats: Agility vs Strength
        if (stats.agility > 0 && stats.agility > stats.strength)
            return ItemStatFamily::Agility;

        if (stats.strength > 0 && stats.strength > stats.agility)
            return ItemStatFamily::Strength;

        return ItemStatFamily::None;
    }

    struct SpecWeightSummary
    {
        uint32_t strWeight = 0;
        uint32_t agiWeight = 0;
        uint32_t spellWeight = 0;
        uint32_t tankWeight = 0;
    };

    // Derive a spec's primary stat family from its ai_playerbot_weightscale_data weights.
    inline ItemStatFamily DeriveSpecStatFamily(SpecWeightSummary const& w)
    {
        // Tank specs in ai_playerbot_weightscale_data: prot warrior (tank=30, defrtng=8),
        // prot paladin (tank=35, defrtng=8), feral tank (tank=114, dodgertng=100), shaman tank.
        if (w.tankWeight > 0 && (w.tankWeight > 20 || w.tankWeight >= w.strWeight))
            return ItemStatFamily::Tank;

        // Caster/healer specs in ai_playerbot_weightscale_data: spellWeight is 100+
        if (w.spellWeight > 0 && w.spellWeight > w.strWeight && w.spellWeight > w.agiWeight)
            return ItemStatFamily::Spell;

        // Agility specs: hunters (agi=4, str=0), rogues (agi=4, str=2)
        if (w.agiWeight > w.strWeight)
            return ItemStatFamily::Agility;

        // Strength specs: arms/fury (str=4, agi=2), retrib (str=4, agi=2), enhance (str=4, agi=2), feral dps (str=11, agi=10)
        if (w.strWeight > 0)
            return ItemStatFamily::Strength;

        return ItemStatFamily::None;
    }

    // Natural class fallback when weight scales are missing or uninitialized.
    inline ItemStatFamily GetFallbackClassStatFamily(uint32_t playerClass)
    {
        switch (playerClass)
        {
        case SPEC_PRIORITY_CLASS_WARRIOR:
        case SPEC_PRIORITY_CLASS_PALADIN:
        case SPEC_PRIORITY_CLASS_SHAMAN:
        case SPEC_PRIORITY_CLASS_DRUID:
            return ItemStatFamily::Strength;
        case SPEC_PRIORITY_CLASS_HUNTER:
        case SPEC_PRIORITY_CLASS_ROGUE:
            return ItemStatFamily::Agility;
        case SPEC_PRIORITY_CLASS_PRIEST:
        case SPEC_PRIORITY_CLASS_MAGE:
        case SPEC_PRIORITY_CLASS_WARLOCK:
            return ItemStatFamily::Spell;
        default:
            return ItemStatFamily::None;
        }
    }

    // True when a real player of this class may use the stat family.
    // For real players, spec is unknown, so hybrids cover all possible families.
    inline bool PlayerClassMayUseStatFamily(uint32_t playerClass, ItemStatFamily family)
    {
        switch (playerClass)
        {
        case SPEC_PRIORITY_CLASS_ROGUE:
        case SPEC_PRIORITY_CLASS_HUNTER:
            return family == ItemStatFamily::Agility;
        case SPEC_PRIORITY_CLASS_WARRIOR:
            return family == ItemStatFamily::Strength || family == ItemStatFamily::Tank;
        case SPEC_PRIORITY_CLASS_MAGE:
        case SPEC_PRIORITY_CLASS_WARLOCK:
        case SPEC_PRIORITY_CLASS_PRIEST:
            return family == ItemStatFamily::Spell;
        case SPEC_PRIORITY_CLASS_PALADIN:
        case SPEC_PRIORITY_CLASS_SHAMAN:
            return family == ItemStatFamily::Strength || family == ItemStatFamily::Spell || family == ItemStatFamily::Tank;
        case SPEC_PRIORITY_CLASS_DRUID:
            return family == ItemStatFamily::Strength || family == ItemStatFamily::Agility || family == ItemStatFamily::Spell || family == ItemStatFamily::Tank;
        default:
            return false;
        }
    }

    // Weapon tie-break: true if the class/spec derives its damage directly from weapon swings.
    // For melee weapons:
    // - Direct weapon damage: Rogue, Warrior, Retribution Paladin, Enhancement Shaman
    // - Stat-stick: Hunter (melee slot), Feral Druid (cat/bear form paw damage is normalized form damage), Casters
    // For ranged weapons:
    // - Direct weapon damage: Hunter
    // - Stat-stick: Warrior, Rogue
    inline bool IsPrimaryWeaponUser(uint32_t playerClass, std::string_view specName, bool isRangedWeapon)
    {
        if (isRangedWeapon)
            return playerClass == SPEC_PRIORITY_CLASS_HUNTER;

        // Melee weapons
        if (playerClass == SPEC_PRIORITY_CLASS_ROGUE || playerClass == SPEC_PRIORITY_CLASS_WARRIOR)
            return true;

        if (playerClass == SPEC_PRIORITY_CLASS_PALADIN)
            return specName.empty() || specName == "retrib";

        if (playerClass == SPEC_PRIORITY_CLASS_SHAMAN)
            return specName.empty() || specName == "enhance";

        return false;
    }

    // Does member fit the item better than the rolling bot?
    // 1. A member fits better when the item's primary stat is that member's spec primary stat and NOT the rolling bot's.
    // 2. Tie-break for weapons when both share the primary stat: a class whose damage comes from the weapon itself beats a stat-stick user.
    // 3. For a real player member: spec is unknown, so check if player's class includes the family, with the same weapon tie-break.
    inline bool MemberFitsBetter(
        ItemStatFamily botFamily, uint32_t botClass, std::string_view botSpecName,
        ItemStatFamily memberFamily, uint32_t memberClass, std::string_view memberSpecName,
        ItemStatFamily itemFamily, bool isWeapon, bool isRangedWeapon, bool isMemberRealPlayer)
    {
        if (itemFamily == ItemStatFamily::None)
            return false;

        if (isMemberRealPlayer)
        {
            if (!PlayerClassMayUseStatFamily(memberClass, itemFamily))
                return false;

            // Player class matches item family while bot spec does not: player fits better
            if (botFamily != itemFamily)
                return true;

            // Both share the stat family: check weapon tie-break
            if (isWeapon)
            {
                bool botIsPrimary = IsPrimaryWeaponUser(botClass, botSpecName, isRangedWeapon);
                bool memberIsPrimary = IsPrimaryWeaponUser(memberClass, memberSpecName, isRangedWeapon);
                if (memberIsPrimary && !botIsPrimary)
                    return true;
            }

            return false;
        }

        // Another bot member:
        // Rule 1: Item's primary stat is member's spec primary stat and NOT rolling bot's
        if (memberFamily == itemFamily && botFamily != itemFamily)
            return true;

        // Rule 2: Weapon tie-break when both share the primary stat
        if (isWeapon && memberFamily == itemFamily && botFamily == itemFamily)
        {
            bool botIsPrimary = IsPrimaryWeaponUser(botClass, botSpecName, isRangedWeapon);
            bool memberIsPrimary = IsPrimaryWeaponUser(memberClass, memberSpecName, isRangedWeapon);
            if (memberIsPrimary && !botIsPrimary)
                return true;
        }

        return false;
    }
}
