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
}
