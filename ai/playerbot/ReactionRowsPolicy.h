#pragma once

// The rows the generic `react` strategy contributes, and the stay gates the legacy raid spread reads beside them.
//
// `react` is named by every reaction engine (AiFactory::AddDefaultReactionStrategies) and had no creator, so the
// engine skipped the unknown name silently and none of these rows existed (issue #1589). AiPlayerbot.RaidReactions
// turns them on; with the key off the list is empty, so every engine's trigger list is what it was with no strategy.
//
// No core includes: tools/test_reaction_rows_policy.cpp reads this list without the server.

#include <vector>

namespace ai
{
    // ACTION_EMERGENCY (strategy/Strategy.h). The policy header cannot include Strategy.h (it pulls the server), so
    // ReactionStrategy.cpp static_asserts the two are equal and this base cannot drift.
    constexpr int kReactionRaidEmergency = 90;

    struct ReactionRaidRow
    {
        const char* trigger;
        const char* action;
        int priority;
    };

    // The rows `react` adds, in the order they are registered: the universal raid survival answers, at the
    // priorities they carried before the strategy had a creator. `enabled` off: no row at all.
    inline std::vector<ReactionRaidRow> ReactionRaidRows(bool enabled)
    {
        if (!enabled)
            return {};

        return {
            // Environmental-damage traps (e.g. braziers) regardless of combat state.
            { "environmental hazard nearby", "move away from hazard", kReactionRaidEmergency + 5 },
            // Universal raid survival (any raid map): bomb carriers run 30 yd clear,
            // non-tanks flank dragons, stacked ranged split, Four Horsemen mark
            // carriers (3+ stacks) rotate to the next horseman's corner.
            { "raid bomb debuff", "raid bomb runout", kReactionRaidEmergency + 6 },
            { "four horsemen mark", "four horsemen rotate", kReactionRaidEmergency + 6 },
            { "dragon breath risk", "dragon flank", kReactionRaidEmergency + 4 },
            { "raid spread needed", "raid spread", kReactionRaidEmergency + 3 },
        };
    }
}
