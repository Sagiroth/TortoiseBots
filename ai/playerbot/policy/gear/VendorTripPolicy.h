#pragma once
#include <cstdint>
#include <ctime>

// Pure policy for the "one vendor journey at a time" rule (issue #399): a
// picked vendor trip suppresses new vendor requests for a while, so a bot
// whose walk never starts (move starved by loot/attacks, wander range) cannot
// re-pick the same vendor every ~20 s. The trainer window
// (RequestNamedTravelTargetAction::isAllowed, "trainer trip since") uses the
// same helper. The stamp is set when a
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

     // A picked vendor trip is its own answer: the errand is already underway
     // (picked, walking, working it or cooling down), so a bag-pressure
     // re-request for the same purpose only re-picks the same vendor and
     // restarts the walk - live pool, vendor re-picks every 3-5 s while the
     // bot stood or drifted with no move between picks (issue travel churn).
     // Still true while the trip cools down after an inactive destination:
     // the cooldown ends on its own and the next search refuses the dead
     // destination, so it walks a different vendor instead of this one again.
     // Pure so the standalone test never links the server: the caller passes
     // the held target's already-read (hasDestination, purposeId) pair.
     // Vendor purpose id, mirrored from TravelDestinationPurpose::Vendor so
     // this header stays core-free (static_assert in the .cpp keeps it true).
     constexpr uint32_t VENDOR_TRIP_PURPOSE_ID = 512;
     inline bool VendorTripAlreadyUnderway(bool heldHasDestination, uint32_t heldPurposeId)
     {
         return heldHasDestination && heldPurposeId == VENDOR_TRIP_PURPOSE_ID;
     }

    // True while `tripSince` (manual time "vendor trip since", 0 = no trip
    // picked yet) is inside the window at `now`.
    inline bool VendorTripSuppressedByRecentTrip(time_t tripSince, time_t now)
    {
        return tripSince != 0 && now - tripSince < VENDOR_TRIP_REPICK_WINDOW;
    }

    // A bag-pressure vendor journey for a bot whose bags are mostly held order
    // stock: worth the walk only when the sale frees a bag row or gets the bags
    // under the pressure line; otherwise each looted grey sends it to town again.
    constexpr uint32_t ORDER_BAG_PRESSURE_MIN_SELL_SLOTS = 4;
    inline bool BagPressureSaleRelievesBags(uint32_t usedSlots, uint32_t totalSlots, uint32_t sellableSlots,
        uint8_t pressurePercent)
    {
        if (!totalSlots) return true;
        // Completely full bags loot nothing more, so a smaller sale can never
        // grow into the threshold: any sellable slot is worth the trip then.
        if (usedSlots >= totalSlots && sellableSlots > 0) return true;
        if (sellableSlots >= ORDER_BAG_PRESSURE_MIN_SELL_SLOTS) return true;
        uint32_t const freed = sellableSlots < usedSlots ? sellableSlots : usedSlots;
        return (usedSlots - freed) * 100u < uint32_t(pressurePercent) * totalSlots;
    }
}
