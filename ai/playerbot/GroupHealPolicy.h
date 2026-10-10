#pragma once

#include <algorithm>
#include <cstdint>

// Pure decision rules for the group-heal port (mod-playerbots parity).
// Donor: mod-playerbots @ 79bd4281, AoeInGroupTrigger
// (src/Ai/Base/Trigger/HealthTriggers.cpp:34-50). No core includes:
// callers in strategy/triggers translate game state into plain inputs, so
// the rules stay testable in tools/test_group_heal_policy.cpp.
//
// Donor rule: the trigger fires when enough near-group members are hurt,
// with the hurt threshold scaling with group size so a 5-man does not need
// a raid-sized clump and a raid does not fire on two scratches:
//   member <= 5  -> need 3 hurt
//   member <= 10 -> need min(member/2, 5)
//   member <= 25 -> need min(member/2, 10)
//   else         -> need min(member/2, 15)
// and the donor refuses outright below 5 near members (solo/duo/trio heals
// stay single-target). Adapted: the near count and the hurt count both use
// the 30y heal radius our AoeHealValue scans (the donor uses sight
// distance); the integer-halving is the donor's (member * 0.5 truncated).

namespace ai
{
    std::uint32_t const kGroupHealMinNearMembers = 5;

    inline std::uint32_t GroupHealHurtThreshold(std::uint32_t nearMembers)
    {
        if (nearMembers <= 5)
            return 3;
        std::uint32_t half = nearMembers / 2;
        if (nearMembers <= 10)
            return std::min(half, std::uint32_t(5));
        if (nearMembers <= 25)
            return std::min(half, std::uint32_t(10));
        return std::min(half, std::uint32_t(15));
    }

    inline bool ShouldGroupHeal(std::uint32_t nearMembers, std::uint32_t hurtCount)
    {
        if (nearMembers < kGroupHealMinNearMembers)
            return false;
        return hurtCount >= GroupHealHurtThreshold(nearMembers);
    }
}
