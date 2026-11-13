#pragma once

// Pure policy for a queued taunt at cast time: the taunt stands down when the
// mob is already on another tank. The triggers that queue a taunt refuse a
// mob held by a tank when they fire (a victimless mob is not held by
// anyone: "lose aggro" stays quiet until it picks a victim), but a taunt
// queued on that older build while a fresh mob had no victim yet could
// survive its reach prerequisite and run after the mob turned onto a tank
// player (Tortoise Trials tank-*-no-taunt-tank-player, 2026-11-10). Kept
// std-only so the standalone g++ policy test compiles the real rule.

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
