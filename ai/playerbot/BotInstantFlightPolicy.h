#pragma once

#include <cstdint>
#include <vector>

namespace ai
{
    // Test shortcut (AiPlayerbot.BotInstantFlight): where a bot that boarded
    // a taxi arrives at once instead of flying the whole route.
    //
    // The destination is the last node of the started route - the same spot
    // a flown flight ends at (Player::TaxiStepFinished teleports there on
    // arrival). DBC reads stay at the call site; this decision is pure
    // (flag/route in, node out) so it is unit-testable and costs nothing on
    // the world thread.

    // Node the bot should appear at, or 0 when there is no instant arrival:
    // the switch is off, the flight failed to start, or the route is empty.
    inline std::uint32_t BotInstantFlightDestNode(bool enabled, bool taxiStarted, std::vector<std::uint32_t> const& nodes)
    {
        if (!enabled || !taxiStarted || nodes.empty())
            return 0;
        return nodes.back();
    }
}
