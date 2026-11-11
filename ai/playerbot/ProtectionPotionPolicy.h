#pragma once

#include <cstdint>

// Pure decision rule: which Greater Protection Potion a raid boss calls for.
// Keyed on the boss's creature entry, so the caller only walks the bot's own
// cached attacker list (no world scan). Each school was checked against the
// 1.18.1 core: the spells the boss script casts (src/scripts/dungeons/
// molten_core, onyxias_lair, blackwing_lair, temple_of_ahnqiraj) and their
// `school` in sql/base/tw_world_spell_template.sql (2 fire, 3 nature,
// 5 shadow). Bosses whose damage is mixed or not school-based (Garr, Hakkar,
// Chromaggus, the BWL drakes) are left out on purpose.
//
// The entry table stays the whole rule for the listed bosses. A `spread` row
// can name the school for any other boss instead (Hakkar): the row's spell is
// the damage the raid spreads for, its `priority` a role mask of who drinks.
// Same three potions; no other kit is touched.
// No core includes: tools/test_protection_potion_policy.cpp covers it.

namespace ai
{
    struct ProtectionPotion
    {
        std::uint32_t itemId;      // potion item
        std::uint32_t buffSpellId; // aura the potion applies
    };

    constexpr ProtectionPotion kNoProtectionPotion = { 0, 0 };
    constexpr ProtectionPotion kFireProtectionPotion = { 13457, 17543 };   // Greater Fire Protection Potion
    constexpr ProtectionPotion kShadowProtectionPotion = { 13459, 17548 }; // Greater Shadow Protection Potion
    constexpr ProtectionPotion kNatureProtectionPotion = { 13458, 17546 }; // Greater Nature Protection Potion

    // A `spread` row's `priority` is a role mask: the roles that drink the row's potion. 0 (every row before
    // DungeonRunBossProtectionPotion) is nobody, so key-off and an old row behave alike.
    constexpr std::uint8_t kPotionRoleTank = 1;
    constexpr std::uint8_t kPotionRoleMelee = 2;
    constexpr std::uint8_t kPotionRoleRanged = 4;
    constexpr std::uint8_t kPotionRoleHealer = 8;

    // The one role bit a bot stands for: a tank first, then a healer (both keep their own answer when they also
    // count as ranged), then the ranged and the melee damage dealers.
    inline std::uint8_t PotionRoleBit(bool tank, bool healer, bool ranged)
    {
        if (tank)
            return kPotionRoleTank;
        if (healer)
            return kPotionRoleHealer;
        return ranged ? kPotionRoleRanged : kPotionRoleMelee;
    }

    // The spell's `School` column as spell_template stores it: 2 fire, 3 nature, 5 shadow. Anything else (a physical
    // or multi-school spell, an unholy one) has no Greater Protection Potion here.
    inline ProtectionPotion ProtectionPotionForSchool(std::uint32_t school)
    {
        switch (school)
        {
            case 2:
                return kFireProtectionPotion;
            case 3:
                return kNatureProtectionPotion;
            case 5:
                return kShadowProtectionPotion;
            default:
                return kNoProtectionPotion;
        }
    }

    // The row's answer for one bot: nothing with the key off, nothing for a role the mask does not list, else the
    // row's school's potion. `botRoleBit` is PotionRoleBit.
    inline ProtectionPotion SpreadRowPotion(bool flagOn, std::uint32_t school, std::uint8_t roleMask,
        std::uint8_t botRoleBit)
    {
        if (!flagOn || (roleMask & botRoleBit) == 0)
            return kNoProtectionPotion;
        return ProtectionPotionForSchool(school);
    }

    // Whether the row's potion is worth drinking now: a potion exists, its aura is not up, the bot is at least level
    // 48 (the Greater potions' own requirement) and its health is not below the low line, where a healing potion
    // comes first.
    inline bool RowPotionDue(bool hasPotion, bool hasAura, std::uint8_t healthPct, std::uint8_t lowHealthPct,
        std::uint32_t level)
    {
        if (!hasPotion || hasAura || level < 48)
            return false;
        return healthPct >= lowHealthPct;
    }

    // The row potion's health rule at one instant, over the potion a live row named for the bot. The row trigger and
    // the action's execution-time re-check both call it: a potion picked above the low line is not drunk once the bot
    // has dropped below it, where a healing potion comes first.
    inline bool BossRowPotionDue(const ProtectionPotion& potion, bool hasAura, std::uint8_t healthPct,
        std::uint8_t lowHealthPct, std::uint32_t level)
    {
        return RowPotionDue(potion.itemId != 0, hasAura, healthPct, lowHealthPct, level);
    }

    inline ProtectionPotion BossProtectionPotion(std::uint32_t entry)
    {
        switch (entry)
        {
            // Fire
            case 11982: // Magmadar: Lava Breath
            case 12056: // Baron Geddon: Living Bomb, Inferno
            case 11988: // Golemagg the Incinerator: Pyroblast, Magma Splash
            case 11502: // Ragnaros: Wrath of Ragnaros, Magma Blast, Elemental Fire
            case 10184: // Onyxia: Flame Breath, Fireball, Deep Breath
            case 12435: // Razorgore the Untamed: Fireball Volley, Conflagration
            case 13020: // Vaelastrasz the Corrupt: Flame Breath, Fire Nova
                return kFireProtectionPotion;
            // Shadow
            case 12118: // Lucifron: Impending Doom, Shadow Shock
            case 12259: // Gehennas: Shadow Bolt (Rain of Fire is stepped out of)
            case 11583: // Nefarian: Shadow Flame
                return kShadowProtectionPotion;
            // Nature
            case 15509: // Princess Huhuran: Noxious Poison, Acid Spit, Wyvern Sting
            case 15299: // Viscidus: Poison Bolt Volley, Poison Shock, Toxin
                return kNatureProtectionPotion;
            default:
                return kNoProtectionPotion;
        }
    }
}
