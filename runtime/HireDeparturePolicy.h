#pragma once

// Issue #378: when does a hired companion end because its master is no longer
// in its group? Like the deletion guard (HireDeletionPolicy.h), the rule is a
// pure function of the facts a caller can see, so it is unit-tested on its own
// (tools/test_hire_departure_policy.cpp).
//
// Facts:
//  - isHire: the guid is in the active-hire registry (HireLifecycle's m_hired).
//    The registry is written only by Hire(); a pool bot or a player's alt is
//    never in it, so neither can ever be dismissed by this rule.
//  - masterOnline: the master has a live, non-headless session AND no logout
//    grace is running. During a logout the master object can still be in world
//    for a moment, so callers pass `MasterOnline(record) && !record.masterOfflineSince`:
//    a logged-out master is offline here and the grace timer decides instead.
//  - everGrouped: the hire was observed sharing its master's group at least
//    once. Before that the hire is still being provisioned (or was never
//    grouped) and must not be dismissed.
//  - inSameGroupWithMaster: the hire still shares its master's group. A hire
//    that is not in world (mid-login, a stale session) also counts as
//    "same group": the runtime-record watchdog owns that case, not this rule.
//
// A hire whose master left the party while online has masterOnline true,
// everGrouped true and inSameGroupWithMaster false, so it is dismissed.

namespace TortoiseBots
{

inline bool ShouldDismissHireOnGroupDeparture(bool isHire, bool masterOnline, bool everGrouped,
    bool inSameGroupWithMaster)
{
    if (!isHire)
        return false; // a pool bot or an alt: nothing here may touch it
    if (!masterOnline)
        return false; // offline/logged-out master: the disconnect grace period owns the hire
    if (!everGrouped)
        return false; // not grouped yet: provisioning (or a stuck reunite) is in flight
    if (inSameGroupWithMaster)
        return false; // still together
    return true;
}

} // namespace TortoiseBots
