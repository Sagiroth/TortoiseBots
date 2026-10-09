#pragma once

#include <cstdint>

// Pet taunt situation toggle (PET-3): Growl/Torment autocast stays on while
// the pet must hold aggro (solo, or grouped with no real tank) and turns off
// when grouped with a real tank (player or bot tank role) so the pet stops
// stealing aggro in dungeons. The reverse side (PET-8b): with taunting off
// and the pet holding aggro anyway, order Cower once to drop threat.
//
// Pure decision logic; the live input gathering (group walk + IsTank) lives
// in the action layer. Unit-tested on its own
// (tools/test_pet_taunt_policy.cpp).

namespace TortoiseBots
{

// Growl ranks (hunter pets) + Torment ranks (voidwalker). Verified against
// tw_world.spell_template; matches runtime/PetSpellRankPolicy.h ladders.
inline bool IsPetTauntSpell(uint32_t spellId)
{
    switch (spellId)
    {
        case 2649:                                                  // Growl 1
        case 14916: case 14917: case 14918: case 14919: case 14920: // Growl 2-6
        case 14921:                                                 // Growl 7
        case 3716:                                                  // Torment 1
        case 7809: case 7810: case 7811: case 11774: case 11775:     // Torment 2-6
            return true;
        default:
            return false;
    }
}

// Cower ranks (hunter pets): the deliberate threat-drop ordered when the pet
// holds aggro while taunting is off. Autocast stays off (upkeep denylist);
// the bot orders it explicitly.
inline bool IsPetCowerSpell(uint32_t spellId)
{
    switch (spellId)
    {
        case 1742: case 1753: case 1754: case 1755: case 1756: case 16697:
            return true;
        default:
            return false;
    }
}

struct PetTauntInputs
{
    bool grouped = false;     // bot is in a group with others
    bool tankInGroup = false; // a group member (player or bot) fills the tank role
};

inline bool ShouldPetTaunt(PetTauntInputs const& inputs)
{
    // Solo: the pet is the tank. Grouped without a tank: somebody must hold
    // aggro, the pet is the fallback. Grouped with a real tank: pet taunts
    // off so Growl/Torment stop peeling off the tank.
    if (!inputs.grouped)
        return true;
    return !inputs.tankInGroup;
}

} // namespace TortoiseBots
