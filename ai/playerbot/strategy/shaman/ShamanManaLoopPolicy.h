#pragma once

// Pure policy for the elemental mana loop (mod-playerbots parity SHM-6):
// Water Shield (Turtle mana shield, verified ranks in tw_world) replaces
// Lightning Shield on a caster that never procs it, and Chain Lightning
// joins Lightning Bolt as a single-target filler below the shock lines so
// it alternates casts instead of stealing AoE duty.

namespace ai
{
    // Ele shield pick: water whenever trained (mana return on being hit and
    // on orb consumption); lightning only as the pre-water fallback.
    inline bool ElementalWantsWaterShield(bool knowsWaterShield)
    {
        return knowsWaterShield;
    }

    // CL filler relevance: strictly below the shock lines (ACTION_NORMAL)
    // and at/below the lightning-bolt default so it reads as an alternate
    // filler, never a priority steal.
    inline float ChainLightningFillerOffset() { return -1.0f; }
}
