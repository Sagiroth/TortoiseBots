#pragma once

#include <cstdint>

// Pure decision rules for the trinket-use filters (mod-playerbots parity,
// CD-1). Donor: mod-playerbots @ 79bd4281, UseTrinketAction::UseTrinket
// (src/Ai/Base/Actions/GenericSpellActions.cpp:509-663): positive-only,
// aura-or-mana-restore-only, mana/health gates, per-item + per-category
// cooldown memory. No core includes: callers translate the 1.12 SpellEntry
// effects into plain effect-class inputs, so the rules stay testable in
// tools/test_trinket_use_policy.cpp.
//
// The 1.12 category mapping lives in the action (SpellEntry effect ->
// TrinketEffectClass); this header holds the donor's gate verdicts.

namespace ai
{
    // What the trinket's ON_USE spell does, as classified from its effects.
    enum class TrinketEffectClass : std::uint8_t
    {
        None = 0,        // no usable effect (damage proc leaking into use path, etc.)
        Aura = 1,        // applies an aura (stat buff, shield, speed, ...)
        ManaRestore = 2, // restores mana now (energize / periodic energize)
        ManaEfficiency = 3, // improves mana economy (regen, cost reduction)
        Defensive = 4,   // tank survival (armor/resistance/health/dodge/parry/block)
    };

    // Donor positive-only gate: never fire a non-positive use effect.
    inline bool TrinketSpellAllowed(bool isPositive)
    {
        return isPositive;
    }

    // Donor aura-or-mana-restore-only gate: a use effect must apply an aura
    // or restore mana, otherwise it is a damage/proc effect that does not
    // belong on the automatic path. Defensive tank effects and mana
    // efficiency effects are auras by construction (see the classifier), so
    // they pass here and reach the health/mana gates below.
    inline bool TrinketEffectAllowed(TrinketEffectClass effect)
    {
        return effect == TrinketEffectClass::Aura ||
            effect == TrinketEffectClass::ManaRestore ||
            effect == TrinketEffectClass::ManaEfficiency ||
            effect == TrinketEffectClass::Defensive;
    }

    // Donor mana gates: a restore trinket fires below medium mana; an
    // efficiency trinket fires below high mana (65). Non-mana users never
    // fire either.
    inline bool TrinketManaAllowed(TrinketEffectClass effect, bool hasMana,
        std::uint8_t manaPct, std::uint8_t mediumMana, std::uint8_t highMana = 65)
    {
        if (effect != TrinketEffectClass::ManaRestore &&
            effect != TrinketEffectClass::ManaEfficiency)
            return true;
        if (!hasMana)
            return false;
        if (effect == TrinketEffectClass::ManaRestore)
            return manaPct < mediumMana;
        return manaPct < highMana;
    }

    // Donor tank-defensive gate: a defensive trinket fires at or below the
    // low-health line only.
    inline bool TrinketHealthAllowed(TrinketEffectClass effect,
        std::uint8_t healthPct, std::uint8_t lowHealth)
    {
        if (effect != TrinketEffectClass::Defensive)
            return true;
        return healthPct <= lowHealth;
    }

    // Donor per-item + per-category cooldown memory: a use is allowed when
    // neither the item key nor the category key is still cooling down.
    inline bool TrinketCooldownAllowed(bool itemCoolingDown, bool categoryCoolingDown)
    {
        return !itemCoolingDown && !categoryCoolingDown;
    }
}
