#pragma once

// Pure policy for a queued taunt at cast time: the taunt stands down when the
// mob is already on another tank. The triggers that queue a taunt ("lose
// aggro", "party member attacked") check this when they fire, but a taunt
// queued while a fresh mob had no victim yet survives its reach prerequisite
// and runs after the mob has turned onto a tank player (Tortoise Trials
// tank-*-no-taunt-tank-player, 2026-11-10). Kept std-only so the standalone
// g++ policy test compiles the real rule.

namespace ai
{
    // The victim facts the cast-time check reads, in call order: whether the
    // mob's current victim is a player (bot or real), whether that victim is
    // the casting bot itself, and what PlayerbotAI::IsTank says of it
    // (forced role first). Bundled so the helper cannot shuffle its inputs.
    struct TauntHeldInputs
    {
        bool victimIsPlayer = false;
        bool victimIsSelf = false;
        bool victimIsTank = false;
    };

    // victimIsPlayer: the mob's current victim is a player (bot or real).
    // victimIsSelf:   that victim is the casting bot itself.
    // victimIsTank:   PlayerbotAI::IsTank(victim) (forced role first).
    inline bool TauntStandsDownForTank(bool victimIsPlayer, bool victimIsSelf, bool victimIsTank)
    {
        return victimIsPlayer && !victimIsSelf && victimIsTank;
    }

    inline bool TauntStandsDownForTank(TauntHeldInputs const& in)
    {
        return TauntStandsDownForTank(in.victimIsPlayer, in.victimIsSelf, in.victimIsTank);
    }
}
