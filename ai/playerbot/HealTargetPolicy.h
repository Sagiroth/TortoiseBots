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

    // A raid member below this health is an emergency a tank healer with a
    // quiet seat may take over.
    constexpr float kRaidHelpBelowHealth = 70.0f;

    // A focused tank healer whose whole assignment (the tank and itself) is at
    // or above "almost full" may heal a raid member below 70%: the seat is
    // safe, the raid is not. Disabled keeps the healer on its seat.
    inline bool FocusedHealerMayHelpRaid(bool enabled, float ownLowestHealthPercent,
                                         float memberHealthPercent, float almostFullHealth)
    {
        return enabled && ownLowestHealthPercent >= almostFullHealth &&
            memberHealthPercent < kRaidHelpBelowHealth;
    }
}
