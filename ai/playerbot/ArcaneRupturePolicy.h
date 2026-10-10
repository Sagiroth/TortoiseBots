#pragma once

#include <cstdint>

// Pure decision rule for the mage arcane rupture -> missiles rhythm port
// (MAG-5): Turtle's Arcane Rupture (cast 51955+, instant, trainer-taught
// 28-60) deals direct damage (trigger 51949) AND lands a self buff
// (52502/52588, +19% Arcane Missiles, 8s, implicit target A=1 self). The
// rotation is rupture while the self buff is absent, then missiles (the
// arcane default) while buffed.
//
// The donor's arcane rhythm (blast stacks + missile-barrage proc timing,
// `ArcaneMageStrategy.cpp:56-64`) does not transfer — those WotLK spells
// do not exist here. Only the transferable idea ports: arcane has a
// builder/spender rhythm, mapped onto Turtle's rupture->missiles pair.
// Arcane Surge (51933+, usable-after-resist nuke) is out of scope: no
// "resist happened" value exists (report MAG-5), exposed as a manual
// action only in a follow-up if wanted.
//
// No core includes: callers in strategy/ translate game state into these
// plain inputs, so the rule stays testable in
// tools/test_arcane_rupture_policy.cpp without the server.

namespace ai
{
    // Arcane Rupture cast spell ids (trainer-taught ranks; the cast
    // triggers damage 51949+ on the enemy):
    // 51955 rank 1, 51956-51960 ranks 2-6.
    inline bool IsArcaneRuptureCastId(std::uint32_t spellId)
    {
        return spellId >= 51955 && spellId <= 51960;
    }

    struct ArcaneRuptureState
    {
        bool hasRuptureBuff;   // self buff 52502/52588 present on the bot
        bool ruptureKnown;     // bot trained any Arcane Rupture rank
        bool fightingBoss;     // reserved: future boss-only gating
    };

    // True when the bot should cast Arcane Rupture now: the spell is known
    // and the missiles-buffing self buff is absent. While the buff is up,
    // the arcane default (missiles) owns the GCD — no row needed.
    inline bool ShouldCastArcaneRupture(ArcaneRuptureState const& state)
    {
        if (!state.ruptureKnown)
            return false;
        return !state.hasRuptureBuff;
    }
}
