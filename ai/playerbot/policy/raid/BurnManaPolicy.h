#pragma once

// Pure decision rules for the burn_mana boss tactic: Moam refills his mana from the raid's own drain casts, so a few
// bots keep burning him and the rest of the raid fights the adds (consumer: BossTacticsTriggers/Actions.cpp).
//
// No core includes: the caller translates the fight into plain numbers so the rules stay testable in
// tools/test_burn_mana_policy.cpp. `priority` is the most burners; 0 means every eligible bot. Every bot orders the
// eligible bots by guid and takes its own place, so the same few bots burn without a vote.

#include <cstdint>

namespace ai
{
    // Whether the bot at `burnerRank` burns the boss now: it must know one of the three drain spells, the boss's mana
    // must still be at or above the row's start % (below it his own Drain Mana casts fill him and the burners go back
    // to the raid), and the rank must be inside the cap.
    inline bool BurnManaWanted(float bossManaPct, float startPct, bool botCanBurn, int burnerRank, int maxBurners)
    {
        if (!botCanBurn || burnerRank < 0)
            return false;
        if (bossManaPct < startPct)
            return false;
        return maxBurners <= 0 || burnerRank < maxBurners;
    }
}
