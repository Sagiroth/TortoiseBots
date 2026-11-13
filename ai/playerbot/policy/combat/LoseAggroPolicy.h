#pragma once

// Pure rule for the "lose aggro" trigger's tank branch: a victimless mob is
// not "held" by anyone. A taunt queued on a fresh refill mob (no victim yet)
// used to survive its reach prerequisite and fire after the mob turned onto
// a tank player (Tortoise Trials tank-*-no-taunt-tank-player, 2026-11-10).
// Kept std-only so the standalone g++ policy test compiles the real rule.

namespace ai
{
    // The victim facts the trigger reads, in read order: whether the mob has
    // a victim yet (GetVictim() != null), whether that victim is a player
    // (bot or real), and what PlayerbotAI::IsTank says of it (forced role
    // first). Bundled so the trigger cannot shuffle its inputs.
    struct LoseAggroSeen
    {
        bool victimExists = false;
        bool victimIsPlayer = false;
        bool victimIsTank = false;
    };

    // Verdict of the tank-no-aggro branch of LoseAggroTrigger: a mob with no
    // victim yet stays quiet (it picks a victim within a tick and the rules
    // below then decide); a mob on a tank is held; a mob on a pet or NPC, or
    // on a non-tank player, fires.
    inline bool LoseAggroTankBranch(LoseAggroSeen const& in)
    {
        if (!in.victimExists)
            return false;
        if (!in.victimIsPlayer)
            return true;
        return !in.victimIsTank;
    }
}
