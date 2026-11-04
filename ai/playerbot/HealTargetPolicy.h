#pragma once

#include <cstdint>

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

    // A tank seat is quiet when the tank it heals is at or above this health
    // after the damage it is taking: the seat has nothing to heal.
    constexpr float kTankSeatQuietHealth = 90.0f;

    // A raid member below this health is an emergency a quiet seat may take.
    constexpr float kRaidAssistBelowHealth = 70.0f;

    // Only a tank's second (or later) heal seat may leave for the raid: its
    // first seat never does, so the tank keeps one healer.
    inline bool TankSeatMayAssist(std::uint8_t seat)
    {
        return seat >= 1;
    }

    // A quiet tank seat: the healer took a seat from a bound raid's layout
    // (seated), not the tank's first seat, the tank is there, alive and not on
    // a boss, and its health after the damage it is taking is at or above 90%.
    // A missing tank, focus target or focus list makes the caller pass
    // tankFound false: not quiet.
    inline bool TankSeatQuiet(bool seated, std::uint8_t seat, bool tankFound, bool tankAlive,
                              bool tankOnBoss, float tankEffectiveHealthPercent)
    {
        return seated && TankSeatMayAssist(seat) && tankFound && tankAlive && !tankOnBoss &&
            tankEffectiveHealthPercent >= kTankSeatQuietHealth;
    }

    // The raid member a quiet seat may heal: visibly inside heal range, below
    // 70%. No walk into sight and no two-times heal envelope.
    inline bool RaidAssistCandidate(bool quiet, bool inSight, bool inRange, float memberHealthPercent)
    {
        return quiet && inSight && inRange && memberHealthPercent < kRaidAssistBelowHealth;
    }
}
