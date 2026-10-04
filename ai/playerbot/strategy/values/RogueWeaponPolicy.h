#pragma once
#include <string_view>

// Pure policy for rogue spec weapon types: Assassination and Subtlety need a
// dagger main hand (core Spell::CheckItems refuses Backstab/Ambush otherwise:
// SPELL_FAILED_EQUIPPED_ITEM_CLASS), so both specs allow daggers only. Combat
// prefers swords, maces and fist weapons (daggers only when nothing else fits,
// via the generic fallback in ShouldEquipWeaponForSpec). Kept literal and
// core-free so the standalone g++ policy test can include it; the names must
// match the ai_playerbot_weightscales rows ("assas", "combat", "subtle").

namespace ai
{
    // True when the weight-scale spec name is a dagger-only rogue spec.
    inline bool RogueSpecWantsDaggers(std::string_view specName)
    {
        return specName == "assas" || specName == "subtle";
    }
}
