#pragma once
#include <ctime>

// Pure policy for the "one vendor journey at a time" rule (issue #399): a
// picked vendor trip suppresses new vendor requests for a while, so a bot
// whose walk never starts (move starved by loot/attacks, wander range) cannot
// re-pick the same vendor every ~20 s. Mirrors the trainer window
// (ShouldTravelNamedValue, "trainer trip since"): the stamp is set when a
// vendor pick lands (ChooseTravelTargetAction::setNewTarget) and cleared when
// a sale lands (SellAction) or the bot dings (XpGainAction,
// AutoLearnSpellAction) - the two events that change what the stock is worth.
// Deliberately request-side only (RequestTravelTargetAction::isUseful): the
// travel trigger doubles as the in-flight trip's stored condition, so gating
// the trigger would drop the trip itself on the next travel check.

namespace ai
{
    // How long a picked vendor trip suppresses new vendor requests. Matches
    // the trainer window and the fruitless-errand parks (10 * MINUTE).
    constexpr time_t VENDOR_TRIP_REPICK_WINDOW = 10 * 60;

    // True while `tripSince` (manual time "vendor trip since", 0 = no trip
    // picked yet) is inside the window at `now`.
    inline bool VendorTripSuppressedByRecentTrip(time_t tripSince, time_t now)
    {
        return tripSince != 0 && now - tripSince < VENDOR_TRIP_REPICK_WINDOW;
    }
}
