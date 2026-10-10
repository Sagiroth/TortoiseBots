#pragma once

#include <cstdint>

namespace ai
{
    // Equip-upgrade score threshold (equipthreshold, AG-1).
    //
    // Without it a bot swaps gear on any epsilon stat-weight gain: two
    // near-identical greens trade back and forth across audits, burning
    // durability/attention for nothing. Donor mod-playerbots upgrades only
    // when the new score beats the old by a configurable factor
    // (EquipUpgradeThreshold = 1.1): itemScore > oldScore * threshold.
    //
    // Pure decision rule, no core includes: the caller (ItemUsageValue,
    // which owns the layered compare) passes the two weights and the
    // configured factor; the rule only renders the better verdict. Exact
    // ties stay false here - they fall through to the caller's sheet /
    // quality / item-level tiebreaks, same as the donor shape where a zero
    // or tied score never satisfies the strict multiply compare.

    // Better verdict: strictly above old * threshold. A zero old weight
    // takes any positive new weight (first real stats always win); an
    // exact tie or an epsilon gain does not.
    inline bool EquipUpgradeBetter(std::uint32_t newWeight, std::uint32_t oldWeight,
        float threshold = 1.1f)
    {
        return float(newWeight) > float(oldWeight) * threshold;
    }
}
