#pragma once

// Pure policy for the elemental mana loop (SHM-6): Water Shield (trains at
// 34; verified in npc_trainer) replaces Lightning Shield (trains at 8) once
// trained, with Lightning Shield kept as the pre-34 fallback so low-level
// elemental bots always have a shield. Chain Lightning fires only into
// ranged packs on cooldown (never single-target: no CC breaks, no OOM
// spam) — the pack gate lives in the trigger conjunction, below earthquake.

namespace ai
{
    // Ele shield pick: water whenever trained (mana return on being hit);
    // lightning only as the pre-water fallback. Both rows stay queued; the
    // water trigger stays quiet until trained.
    inline bool ElementalWantsWaterShield(bool knowsWaterShield)
    {
        return knowsWaterShield;
    }

    // Filler relevance: inside the AoE strategy, below earthquake, so the
    // pack opener wins and CL fills on cooldown.
    inline bool ChainLightningPackFillerOnly() { return true; }
}
