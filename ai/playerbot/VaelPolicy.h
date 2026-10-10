#pragma once

#include <cstdint>

// Pure decision rules for the Vaelastrasz refinements (mod-playerbots
// parity, raid1 batch item 6).
// Donor: mod-playerbots @ 79bd4281, src/Ai/Raid/BWL/BWLActions.cpp:142-265
// (VaelastraszMoveAway: victim holds while boss lives; carriers flee by
// weighted repulsion from non-BA bots within 20y, BA carriers cluster;
// tail-sweep recovery past 30y walks back; ranged past 25y blends toward
// the boss to hold cast range + LOS) + BWLMultipliers.cpp:64-89 (BA
// carriers: only the BA move runs, charge blocked) + BWLStrategy.cpp:26-32
// (rear-flank positioning + BA runout wiring).
// What already exists here (NOT this PR): the universal `raid bomb debuff`
// trigger covers all BA spell ids (18173/23478/23620) with a 30y
// anchor-flee, and Vael 13020 joined the dragon-flank list in the BWL
// bundle PR. This PR adds the two Vael-specific refinements the universal
// behavior lacks: victim exemption and repulsion-from-non-BA flee.
// No core includes: callers translate game state into plain inputs so the
// rules stay testable in tools/test_vael_policy.cpp.

namespace ai
{
    // Donor geometry (yards).
    constexpr float kVaelRepulsionRange = 20.0f;
    constexpr float kVaelTailSweepRecoveryRange = 30.0f;
    constexpr float kVaelRangedBiasThreshold = 25.0f;

    // The boss's victim holds through Burning Adrenaline while the boss
    // lives (tank keeps the dragon; fleeing would wipe the raid).
    inline bool ShouldVaelVictimHold(bool botIsVictim, bool bossAlive)
    {
        return botIsVictim && bossAlive;
    }

    // BA carriers flee by repulsion from non-BA bots (BA victims cluster
    // together); the trigger that queues the move is the universal bomb
    // trigger — this predicate picks the flee SHAPE downstream.
    inline bool ShouldClusterWithBaCarriers(bool otherHasBurningAdrenaline, bool bossAlive)
    {
        // While the boss lives, other BA carriers are ignored as repulsion
        // sources so victims group together away from the clean raid.
        return bossAlive && otherHasBurningAdrenaline;
    }

    // Tail-sweep recovery: knocked past 30y with no repulsion push, walk
    // back toward the boss instead of standing out of range.
    inline bool ShouldRecoverToVaelBoss(bool hasRepulsionPush, bool bossAlive, bool beyondRecoveryRange)
    {
        return !hasRepulsionPush && bossAlive && beyondRecoveryRange;
    }

    // Ranged past 25y blends the flee toward the boss to hold cast range.
    inline bool ShouldBiasFleeToVaelBoss(bool botIsRanged, bool bossAlive, bool beyondBiasThreshold)
    {
        return botIsRanged && bossAlive && beyondBiasThreshold;
    }
}
