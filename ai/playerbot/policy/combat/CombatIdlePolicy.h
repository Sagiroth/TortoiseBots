#pragma once

#include <cstdint>

namespace ai
{
    // How long the engine may sit in combat state with no fight before it
    // leaves: long enough to bridge a real pull's quiet seconds (ranged
    // opener flight, CC landing, target switching, pet still walking in),
    // short enough that a wedged bot is back travelling within a minute.
    constexpr std::uint32_t COMBAT_IDLE_EXIT_SECONDS = 30;

    // Facts about one held order target, read off the world by the glue.
    struct CombatOrderFacts
    {
        // Target resolves, is alive, and is hostile to the bot.
        bool liveHostile = false;
        // Target is really fighting: victim/threat on the bot, its pet, or
        // a groupmate, or it is the bot's explicit attack order still being
        // walked to (in range, bot moving).
        bool engaged = false;
    };

    // An order holds combat only while it is a live hostile AND engaged. A
    // stale order on a far, idle, unreachable or evading mob stops vetoing
    // combat end; the idle clock below then releases the engine.
    inline bool CombatOrderHolds(bool liveHostile, bool engaged)
    {
        return liveHostile && engaged;
    }

    inline bool CombatIdleExitDue(bool coreInCombat, bool hasAttackers,
        bool orderHolds, std::uint32_t idleSeconds)
    {
        if (coreInCombat || hasAttackers || orderHolds)
            return false;
        return idleSeconds >= COMBAT_IDLE_EXIT_SECONDS;
    }
}
