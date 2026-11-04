#pragma once

// Pure decision rules for the swap_on_aura boss tactic: the tanks of the row's rotation take the boss in turn at a
// stack count of the row's aura (Kurinnaxx's Mortal Wound, 5 stacks), and the tank that was swapped off holds its
// threat for a while so the new tank keeps the boss (consumer: BossTacticsActions.cpp).
//
// No core includes: the caller translates the fight into plain numbers so the rules stay testable in
// tools/test_tank_swap_policy.cpp. The rotation is the raid's tank order (GetRaidTankRoles), index 0 the main tank.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ai
{
    // How long the tank that was swapped off holds its threat after the swap, unless its own stacks are gone first.
    inline constexpr std::uint32_t kTankSwapStandDownMs = 20000;

    // How far from the boss a tank must stand to take it over (Taunt/Growl's own 30 yd range in 1.12).
    inline constexpr float kTankSwapCandidateYd = 30.0f;

    // The rotation tank that takes the boss next, or -1 for none. The arrays are indexed by rotation position and
    // include the victim. A tank qualifies when it is alive, within reach of the boss and carries fewer than half the
    // row's threshold: a tank that still carries a deep stack would only add another one. The search starts after the
    // victim and wraps, so the row's tanks alternate, and the victim itself is never picked. At least two rotation
    // tanks are needed, and the victim has to be one of them.
    inline int TankSwapWanted(int rotationIndexOfVictim, int victimStacks, int threshold,
                              const std::vector<int>& candidateStacks,
                              const std::vector<bool>& candidateAlive,
                              const std::vector<bool>& candidateInRange)
    {
        const std::size_t count = std::min(candidateStacks.size(),
                                           std::min(candidateAlive.size(), candidateInRange.size()));
        if (count < 2 || rotationIndexOfVictim < 0 ||
            static_cast<std::size_t>(rotationIndexOfVictim) >= count || threshold < 1 ||
            victimStacks < threshold)
            return -1;
        for (std::size_t step = 1; step < count; ++step)
        {
            const int index = static_cast<int>((static_cast<std::size_t>(rotationIndexOfVictim) + step) % count);
            if (index == rotationIndexOfVictim)
                continue;
            if (candidateAlive[index] && candidateInRange[index] && candidateStacks[index] * 2 < threshold)
                return index;
        }
        return -1;
    }

    // Whether the tank a swap moved off the boss holds its threat abilities: the fight record names it as swapped out
    // and it is still inside the window with its own stacks not yet gone. Either end ends the hold.
    inline bool TankSwapStandsDown(bool swappedOut, int ownStacks, std::uint32_t sinceMs)
    {
        return swappedOut && ownStacks > 0 && sinceMs < kTankSwapStandDownMs;
    }

    // Whether one action is held from the boss a stand-down tank was moved off: anything aimed at that boss itself
    // (a threat builder, a melee swing, a cast), any area damage (Consecration, a Swipe or a Cleave reaches the boss
    // from any pick) and any taunt. Nothing names a class, so a druid's Growl and Maul, a paladin's attack and a
    // warrior's rotation answer alike; a single-target action aimed elsewhere (an add, a heal, a buff) runs.
    inline bool TankSwapHoldsAction(bool standsDown, bool taunt, bool areaDamage, bool targetsBoss)
    {
        return standsDown && (taunt || areaDamage || targetsBoss);
    }
}
