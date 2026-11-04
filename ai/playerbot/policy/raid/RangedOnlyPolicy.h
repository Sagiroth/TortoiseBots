#pragma once

// Pure decision rule for the ranged_only boss tactic: Ayamiss climbs 20 yd in the air 5 s into the fight, where a
// melee bot cannot reach her, so the melee bots take the adds until she lands (consumer: DpsTargetValue.cpp,
// TankTargetValue.cpp and BossTacticsActions.cpp).
//
// No core includes: the caller passes the bot's role and the two heights, so the rule stays testable in
// tools/test_ranged_only_policy.cpp. The comparison is the raw z difference, never the navmesh height: the boss stands
// in the air, not on a floor.

namespace ai
{
    // Whether a melee bot standing `botFloorZ` under a subject at `subjectZ` is held off it: the subject stands
    // `gapYd` yd or more above the bot. The threshold is inclusive (exactly gapYd holds); a ranged bot is never held.
    inline bool RangedOnlyHolds(bool isMeleeBot, float subjectZ, float botFloorZ, float gapYd)
    {
        if (!isMeleeBot || gapYd <= 0.0f)
            return false;
        return subjectZ - botFloorZ >= gapYd;
    }
}
