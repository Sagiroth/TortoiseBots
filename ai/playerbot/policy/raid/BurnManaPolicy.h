#pragma once

// Pure decision rules for the burn_mana boss tactic: Moam refills his mana from the raid's own drain casts, so a few
// bots keep burning him and the rest of the raid fights the adds (consumer: BossTacticsTriggers/Actions.cpp).
//
// No core includes: the caller translates the fight into plain numbers so the rules stay testable in
// tools/test_burn_mana_policy.cpp. `priority` is the most burners; 0 means every eligible bot. Every bot orders the
// eligible bots by guid and takes its own place, so the same few bots burn without a vote.

#include <cstddef>
#include <cstdint>

namespace ai
{
    // Moam's stone phase (spell 25685): his Arcane Eruption is off and the mana bar does not threaten the raid, so the
    // burners leave him to the Mana Fiends.
    const std::uint32_t kStonePhaseAura = 25685;

    // Whether the bot at `burnerRank` burns the boss now: it must know one of the three drain spells, the boss's mana
    // must still be at or above the row's start % (below it his own Drain Mana casts fill him and the burners go back
    // to the raid), the rank must be inside the cap, and the boss must not be in the stone phase.
    inline bool BurnManaWanted(float bossManaPct, float startPct, bool botCanBurn, int burnerRank, int maxBurners,
                               bool bossStonePhase)
    {
        if (!botCanBurn || burnerRank < 0)
            return false;
        if (bossStonePhase)
            return false;
        if (bossManaPct < startPct)
            return false;
        return maxBurners <= 0 || burnerRank < maxBurners;
    }

    // Whether a member's drain spell reaches the boss and is paid for right now. `minRangeYd`/`maxRangeYd` come from
    // that member's own spell (warlock Drain Mana 20 yd, priest Mana Burn 30 yd, hunter Viper Sting 8-35 yd).
    inline bool BurnManaInRangeNow(bool alive, bool inCombat, int mana, int manaCost, float distanceYd,
                                   float minRangeYd, float maxRangeYd)
    {
        if (!alive || !inCombat)
            return false;
        if (manaCost > 0 && mana < manaCost)
            return false;
        return distanceYd >= minRangeYd && distanceYd <= maxRangeYd;
    }

    // One group member as the rank sees it: `runsBossTactics` is false for a bot that would never cast a burn row, and
    // `canBurnNow` folds in alive, in combat, the spell's own range and enough mana.
    struct BurnManaCandidate
    {
        std::uint64_t guid;
        bool runsBossTactics;
        bool canBurnNow;
    };

    // The 0-based burner rank of `selfGuid`, or -1 when the bot itself runs no boss tactics or cannot burn now. Only
    // lower-guid members that both run the tactics and can cast right now take a slot, so an out-of-range member never
    // pushes an in-range one past the cap.
    inline int BurnManaRank(BurnManaCandidate const* members, std::size_t count, std::uint64_t selfGuid,
                            bool selfRunsBossTactics, bool selfCanBurnNow)
    {
        if (!selfRunsBossTactics || !selfCanBurnNow)
            return -1;
        int rank = 0;
        for (std::size_t i = 0; i < count; ++i)
            if (members[i].guid < selfGuid && members[i].runsBossTactics && members[i].canBurnNow)
                ++rank;
        return rank;
    }
}
