#pragma once

#include <cstdint>

// Pure policy for chaining travel legs without a stop (MovementAction::MoveTo2,
// MoveToTravelTargetAction::isUseful).
//
// Problem: each travel turn launches at most one ~150 yd leg as a single core
// spline (ClipPath cuts the route at reactDistance, DispatchMovement launches
// it, WaitForReach asks to think again after the leg time capped at
// MaxWaitForMove). A turn that runs while the current leg is still being
// walked is a no-op (isUseful is false while moving), so when the spline ends
// the bot stands still until a fresh turn dispatches the next leg - about 3 s
// on a busy realm.
//
// Rule: a turn that finds the current leg nearly finished may dispatch the
// next leg in the same Execute instead of waiting for the leg to end. The
// next leg is still cut at the same clipped route (ClipPath caps the leg),
// only earlier: the old spline is almost done when the new one launches, so
// the bot never rubber-bands or turns back.
//
// What the chain does NOT touch: failure handling. A leg that failed to
// dispatch, or a path with fewer than two points, never chains; the caller
// keeps counting the failure and cooling the target down exactly as before.

namespace ai
{
    // How close to the end of the current leg a new leg may start (ms left on
    // the spline). A walking bot covers ~7 yd/s, so this re-dispatches about
    // 10 yd before the leg ends: the clipped tail of the route behind the bot
    // is far shorter than that, and the new leg's head matches the old spline
    // because both run on the same mesh.
    constexpr int32_t TRAVEL_LEG_CHAIN_REMAIN_MS = 1500;

    // What the tail check decided (TravelLegChainDecide).
    enum class TravelLegChainVerdict : uint8_t
    {
        Chain = 0,       // leg nearly finished: dispatch the next leg now
        WaitSpline,      // spline still has far to go: let it walk
        WaitFinalized,   // no live spline: the end-of-leg stop needs no chain
        WaitOtherOrigin, // another movement owns the spline: not our leg
    };

    // ownLeg: the live spline was launched by this module's leg dispatch
    // (origin "DispatchMovement"); remainMs: time left on the spline
    // (timeElapsed()). Only our own nearly finished leg chains; everything
    // else waits exactly as before.
    inline TravelLegChainVerdict TravelLegChainDecide(
        bool ownLeg, int32_t remainMs, bool initialized, bool finalized)
    {
        if (!initialized || finalized)
            return TravelLegChainVerdict::WaitFinalized;

        if (!ownLeg)
            return TravelLegChainVerdict::WaitOtherOrigin;

        return remainMs <= TRAVEL_LEG_CHAIN_REMAIN_MS
            ? TravelLegChainVerdict::Chain
            : TravelLegChainVerdict::WaitSpline;
    }
}
