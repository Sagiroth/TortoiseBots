#pragma once

// Pure policy for the elemental mana loop (SHM-6): Water Shield (R1 trains
// at 26; verified in tw_world.spell_template) replaces Lightning Shield
// (trains at 8) once trained, with Lightning Shield kept as the pre-water
// fallback so low-level elemental bots always have a shield. Chain
// Lightning stays pack-only via the pre-existing "ranged light aoe" row
// (2+ pack, CC interlock in AoeTrigger) — never single-target: no CC
// breaks, no OOM spam. Test-only spec header; runtime uses the trigger.
namespace ai
{
    // Ele shield pick: water whenever trained (mana return on being hit);
    // lightning only as the pre-water fallback. Both rows stay queued; the
    // water trigger stays quiet until trained, and the fallback trigger
    // gates itself off once water is known (mutually exclusive shields).
    inline bool ElementalWantsWaterShield(bool knowsWaterShield)
    {
        return knowsWaterShield;
    }

    // Chain Lightning is pack-only (pre-existing "ranged light aoe" row).
    inline bool ChainLightningPackFillerOnly() { return true; }
}
