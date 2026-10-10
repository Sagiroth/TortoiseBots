#pragma once

#include <cstdint>

namespace ai
{
    // Dual-wield main-hand -> off-hand cascade (dwcascade, AG-2).
    //
    // The equip audit scores each bag weapon against its resolved slot, so
    // after a main-hand upgrade lands, the displaced old main hand sits in
    // the bags: the next audit resolves it back to the (now stronger) main
    // hand and never tries the off hand. Donor mod-playerbots demotes it -
    // new weapon beats MH -> equip MH, move old MH to OH when it fits and
    // beats the current OH; new weapon not beating MH but beating OH ->
    // equip OH (the latter already works here via secondary-slot
    // resolution, so only the demotion is ported; Titan Grip branches are
    // WotLK-only and dropped).
    //
    // Pure decision rule, no core includes: the caller (EquipUpgradesAction)
    // passes the fit/legality verdicts it already owns plus the two weights.

    // Demotion verdict: the old main hand fits the off hand (1H weapon, not
    // a 2H), the spec wants a weapon in the off hand, and the off hand is
    // empty or holds a lighter weapon. Strictly-greater keeps a tied off
    // hand (no churn); a 2H old hand never demotes.
    inline bool DualWieldCascade(bool oldMHFitsOffHand, bool specWantsWeaponOffHand,
        bool offHandEmpty, std::uint32_t oldMHWeight, std::uint32_t offHandWeight)
    {
        if (!oldMHFitsOffHand || !specWantsWeaponOffHand)
            return false;
        if (offHandEmpty)
            return true;
        return oldMHWeight > offHandWeight;
    }
}
