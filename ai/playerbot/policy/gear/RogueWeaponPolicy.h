#pragma once
#include <cstdint>
#include <string_view>

// Pure policy for rogue weapon types (owner weapon matrix).
// Assassination and Subtlety need a dagger main hand (core Spell::CheckItems
// refuses Backstab/Ambush otherwise: SPELL_FAILED_EQUIPPED_ITEM_CLASS), so both
// specs allow daggers only in both hands. Combat without points in any weapon-
// specialization talent allows any rogue one-hander (dagger, sword, mace, fist),
// letting stat weight decide. Combat with points in Sword, Mace, Fist Weapon or
// Dagger Specialization allows only that type (or union if several are learned).
//
// Kept literal and core-free so the standalone g++ policy test can include it;
// the names match the ai_playerbot_weightscales rows ("assas", "combat", "subtle").

constexpr uint32_t ROGUE_POLICY_WS_MACE = 4;
constexpr uint32_t ROGUE_POLICY_WS_SWORD = 7;
constexpr uint32_t ROGUE_POLICY_WS_FIST = 13;
constexpr uint32_t ROGUE_POLICY_WS_DAGGER = 15;

// Rogue weapon-specialization talent rank spell IDs (Turtle 1.18.1 / Talent.dbc):
// Hack and Slash (Sword Specialization, Talent ID 123):
constexpr uint32_t ROGUE_TALENT_HACK_AND_SLASH_R1 = 13960;
constexpr uint32_t ROGUE_TALENT_HACK_AND_SLASH_R2 = 13961;

// Concussive Blows (Mace Specialization, Talent ID 119):
constexpr uint32_t ROGUE_TALENT_CONCUSSIVE_BLOWS_R1 = 13709;
constexpr uint32_t ROGUE_TALENT_CONCUSSIVE_BLOWS_R2 = 13800;
constexpr uint32_t ROGUE_TALENT_CONCUSSIVE_BLOWS_R3 = 13801;
constexpr uint32_t ROGUE_TALENT_CONCUSSIVE_BLOWS_R4 = 13802;
constexpr uint32_t ROGUE_TALENT_CONCUSSIVE_BLOWS_R5 = 13803;

// Close Quarters Combat (Dagger Specialization, Talent ID 121):
constexpr uint32_t ROGUE_TALENT_CLOSE_QUARTERS_COMBAT_R1 = 13706;
constexpr uint32_t ROGUE_TALENT_CLOSE_QUARTERS_COMBAT_R2 = 13804;

namespace ai
{
    // True when the weight-scale spec name is a dagger-only rogue spec.
    inline bool RogueSpecWantsDaggers(std::string_view specName)
    {
        return specName == "assas" || specName == "subtle";
    }

    // Pure policy for rogue one-handed melee weapon legality.
    inline bool RogueWeaponAllowed(std::string_view specName,
                                   bool hasSwordSpec,
                                   bool hasMaceSpec,
                                   bool hasFistSpec,
                                   bool hasDaggerSpec,
                                   uint32_t weaponSubclass)
    {
        if (RogueSpecWantsDaggers(specName))
            return weaponSubclass == ROGUE_POLICY_WS_DAGGER;

        if (specName == "combat")
        {
            bool const hasAnySpec = hasSwordSpec || hasMaceSpec || hasFistSpec || hasDaggerSpec;
            if (!hasAnySpec)
            {
                return weaponSubclass == ROGUE_POLICY_WS_DAGGER ||
                       weaponSubclass == ROGUE_POLICY_WS_SWORD ||
                       weaponSubclass == ROGUE_POLICY_WS_MACE ||
                       weaponSubclass == ROGUE_POLICY_WS_FIST;
            }

            if (hasSwordSpec && weaponSubclass == ROGUE_POLICY_WS_SWORD)
                return true;
            if (hasMaceSpec && weaponSubclass == ROGUE_POLICY_WS_MACE)
                return true;
            if (hasFistSpec && weaponSubclass == ROGUE_POLICY_WS_FIST)
                return true;
            if (hasDaggerSpec && weaponSubclass == ROGUE_POLICY_WS_DAGGER)
                return true;
            return false;
        }

        return weaponSubclass == ROGUE_POLICY_WS_DAGGER ||
               weaponSubclass == ROGUE_POLICY_WS_SWORD ||
               weaponSubclass == ROGUE_POLICY_WS_MACE ||
               weaponSubclass == ROGUE_POLICY_WS_FIST;
    }
}
