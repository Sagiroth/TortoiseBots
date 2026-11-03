#pragma once

// Alive gates for the branches of "party member to heal" that the player
// loop's own alive test does not reach. "Below full health" is not a life
// sign: a corpse sits at 0% health and would otherwise top the heal list
// while the healer casts into the body.

namespace ai
{
    // A pet is a heal target only while it is alive; a mini pet is a
    // companion, not a combat pet.
    inline bool HealablePet(bool isMiniPet, bool isAlive)
    {
        return !isMiniPet && isAlive;
    }

    // A unit picked off the bot's selection or its rpg target.
    inline bool HealableTarget(bool isAlive)
    {
        return isAlive;
    }

    // The "already being healed" filter of the target pick. A member below the
    // almost-full line is a candidate unless a heal is already on its way, but
    // only above the critical line: a member at or below it stays a candidate
    // for every healer, so a second heal can follow the first (one heal cannot
    // be trusted to save a dying member). "beingHealed" is a callable that
    // only runs when it can change the answer.
    template <typename BeingHealed>
    inline bool MemberNeedsHeal(float healthPct, float almostFullHealthPct, float criticalHealthPct,
        BeingHealed beingHealed)
    {
        if (healthPct >= almostFullHealthPct)
            return false;
        return healthPct <= criticalHealthPct || !beingHealed();
    }

    // A pet joins the candidates only when it is hurt AND nobody is already
    // healing it ("&&": the old "||" added pets almost always).
    template <typename BeingHealed>
    inline bool PetNeedsHeal(float healthPct, float almostFullHealthPct, BeingHealed beingHealed)
    {
        return healthPct < almostFullHealthPct && !beingHealed();
    }
}
