#pragma once
#include <cstdint>
#include <string_view>

// Pure policy for rogue weapon types (owner weapon matrix / Turtle 1.18.1).
// Assassination and Subtlety need a dagger main hand (core Spell::CheckItems
// refuses Backstab/Ambush otherwise: SPELL_FAILED_EQUIPPED_ITEM_CLASS), so both
// specs allow daggers only in both hands. Combat without points in any weapon
// preference talent allows every rogue one-hander (dagger, sword, mace, fist,
// and axe), letting stat weight decide. Combat with points in one or more
// preference talents allows the union of their types:
// - Hack and Slash: axe + sword
// - Close Quarters Combat: mace + dagger + fist
// - Concussive Blows: mace
// (Weapon Expertise grants skill for all rogue 1H types without preference).
//
// Kept literal and core-free so the standalone g++ policy test can include it;
// the names match the ai_playerbot_weightscales rows ("assas", "combat", "subtle").

constexpr uint32_t ROGUE_POLICY_WS_AXE = 0;
constexpr uint32_t ROGUE_POLICY_WS_MACE = 4;
constexpr uint32_t ROGUE_POLICY_WS_SWORD = 7;
constexpr uint32_t ROGUE_POLICY_WS_FIST = 13;
constexpr uint32_t ROGUE_POLICY_WS_DAGGER = 15;

// Rogue weapon preference talent rank spell IDs (Turtle 1.18.1 / Talent.dbc):
// Hack and Slash (Talent ID 123 -> Axe + Sword):
constexpr uint32_t ROGUE_TALENT_HACK_AND_SLASH_R1 = 13960;
constexpr uint32_t ROGUE_TALENT_HACK_AND_SLASH_R2 = 13961;

// Close Quarters Combat (Talent ID 121 -> Mace + Dagger + Fist):
constexpr uint32_t ROGUE_TALENT_CLOSE_QUARTERS_COMBAT_R1 = 13706;
constexpr uint32_t ROGUE_TALENT_CLOSE_QUARTERS_COMBAT_R2 = 13804;

// Concussive Blows (Talent ID 119 -> Mace):
constexpr uint32_t ROGUE_TALENT_CONCUSSIVE_BLOWS_R1 = 13709;
constexpr uint32_t ROGUE_TALENT_CONCUSSIVE_BLOWS_R2 = 13800;
constexpr uint32_t ROGUE_TALENT_CONCUSSIVE_BLOWS_R3 = 13801;
constexpr uint32_t ROGUE_TALENT_CONCUSSIVE_BLOWS_R4 = 13802;
constexpr uint32_t ROGUE_TALENT_CONCUSSIVE_BLOWS_R5 = 13803;

namespace ai
{
    // True when the weight-scale spec name is a dagger-only rogue spec.
    inline bool RogueSpecWantsDaggers(std::string_view specName)
    {
        return specName == "assas" || specName == "subtle";
    }

    // Pure policy for rogue one-handed melee weapon legality under Turtle 1.18.1 rules.
    inline bool RogueWeaponAllowed(std::string_view specName,
                                   bool hasHackAndSlash,
                                   bool hasCloseQuartersCombat,
                                   bool hasConcussiveBlows,
                                   uint32_t weaponSubclass)
    {
        if (RogueSpecWantsDaggers(specName))
            return weaponSubclass == ROGUE_POLICY_WS_DAGGER;

        if (specName == "combat")
        {
            bool const hasAnyPreference = hasHackAndSlash || hasCloseQuartersCombat || hasConcussiveBlows;
            if (!hasAnyPreference)
            {
                return weaponSubclass == ROGUE_POLICY_WS_DAGGER ||
                       weaponSubclass == ROGUE_POLICY_WS_SWORD ||
                       weaponSubclass == ROGUE_POLICY_WS_MACE ||
                       weaponSubclass == ROGUE_POLICY_WS_FIST ||
                       weaponSubclass == ROGUE_POLICY_WS_AXE;
            }

            if (hasHackAndSlash && (weaponSubclass == ROGUE_POLICY_WS_AXE || weaponSubclass == ROGUE_POLICY_WS_SWORD))
                return true;
            if (hasCloseQuartersCombat && (weaponSubclass == ROGUE_POLICY_WS_MACE ||
                                          weaponSubclass == ROGUE_POLICY_WS_DAGGER ||
                                          weaponSubclass == ROGUE_POLICY_WS_FIST))
                return true;
            if (hasConcussiveBlows && weaponSubclass == ROGUE_POLICY_WS_MACE)
                return true;
            return false;
        }

        return weaponSubclass == ROGUE_POLICY_WS_DAGGER ||
               weaponSubclass == ROGUE_POLICY_WS_SWORD ||
               weaponSubclass == ROGUE_POLICY_WS_MACE ||
               weaponSubclass == ROGUE_POLICY_WS_FIST ||
               weaponSubclass == ROGUE_POLICY_WS_AXE;
    }
}
