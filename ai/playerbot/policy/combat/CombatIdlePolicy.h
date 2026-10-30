#pragma once

#include <cstdint>

namespace ai
{
    // How long the engine may sit in combat state with no fight before it
    // leaves: long enough to bridge a real pull's quiet seconds (ranged
    // opener flight, CC landing, target switching, pet still walking in),
    // short enough that a wedged bot is back travelling within a minute.
    constexpr std::uint32_t COMBAT_IDLE_EXIT_SECONDS = 30;

    inline bool CombatIdleExitDue(bool coreInCombat, bool hasAttackers,
        bool hasLiveHostileOrder, std::uint32_t idleSeconds)
    {
        if (coreInCombat || hasAttackers || hasLiveHostileOrder)
            return false;
        return idleSeconds >= COMBAT_IDLE_EXIT_SECONDS;
    }
}
