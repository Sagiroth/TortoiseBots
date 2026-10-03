#pragma once
#include <cstdint>

// Pure policy for the vendor "buy useful" loop (#427, ported from
// mod-playerbots `src/Ai/Base/Actions/BuyAction.cpp:69-179`): vendor stock is
// ordered by live stat-weight score with an item-level fallback, and gear
// usages (EQUIP/BAD/BROKEN) share the gear budget. Core-free so the
// standalone g++ policy test can include it.
//
// Donor enum note: donor `ItemUsage` has REPLACE=2 where ours has
// BAD_EQUIP=2 (`ai/playerbot/strategy/values/ItemUsageValue.h`). Our
// classifier has no REPLACE — EQUIP already covers both the empty slot and
// the better-than-equipped answers the donor splits across REPLACE/EQUIP —
// so there is no fourth usage to map; EQUIP/BAD_EQUIP/BROKEN_EQUIP is the
// complete gear set on our side.

namespace ai
{
    // Our ItemUsage values (ItemUsageValue.h). Kept literal so this header
    // stays core-free for the standalone g++ policy test.
    constexpr uint32_t VENDOR_BUY_USAGE_EQUIP = 1;
    constexpr uint32_t VENDOR_BUY_USAGE_BAD_EQUIP = 2;
    constexpr uint32_t VENDOR_BUY_USAGE_BROKEN_EQUIP = 3;

    // True when a vendor-stock usage is paid from the gear budget
    // (NeedMoneyFor::gear). Donor maps REPLACE/EQUIP/BAD/BROKEN to gear;
    // BROKEN_AH is intentionally excluded — like the AH loop it is never
    // bought, only kept until repaired.
    inline bool VendorBuyUsesGearBudget(uint32_t usage)
    {
        return usage == VENDOR_BUY_USAGE_EQUIP ||
               usage == VENDOR_BUY_USAGE_BAD_EQUIP ||
               usage == VENDOR_BUY_USAGE_BROKEN_EQUIP;
    }

    // Donor sort rule (BuyAction.cpp:78-86): rank by weighted score, falling
    // back to item level when either side scores 0 (unweighted or
    // not-yet-usable stock). Strictly greater only, so ties keep vendor order.
    inline bool VendorBuyRanksFirst(uint32_t weightA, uint32_t levelA,
                                    uint32_t weightB, uint32_t levelB)
    {
        if (weightA == 0 || weightB == 0)
            return levelA > levelB;
        return weightA > weightB;
    }
}
