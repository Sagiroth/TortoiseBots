#pragma once

#include <cstdint>

// Pure policy for the organic AH buyer trip (issue #405 rework).
//
// NO buyer teleport: the market buyer bids only with a pool bot ALREADY
// standing at an auctioneer of the auction's house (arrived on its own feet,
// e.g. on a sell trip or an AH travel errand). Teleport stays only the
// pre-existing stuck rescue on the way there - nothing new. To make buying
// happen organically, a masterless pool bot holding spare gold above its
// trainer reserve gets the normal AH travel purpose and walks/flies there
// (same destination the seller uses); the arrival bid stays house-matched
// and budget-gated.
//
// Bounds keep one buyer scan cheap for a 500-bot pool: at most
// kBuyerExamineCap pool entries are examined (cheap guards only) and at most
// kBuyerProbeCap of them touch an AI context ("nearest npcs" + interact
// check) per auction candidate. The rotating start (m_buyerScanIndex in the
// service) cycles coverage across passes.

namespace ai
{
    // Max AI-context probes ("nearest npcs" + interact + house resolve) per
    // auction candidate. Past the cap the scan stops: no teleport fallback,
    // the candidate is simply skipped until a later pass rotates coverage.
    inline constexpr uint32_t kBuyerProbeCap = 8;
    // Max pool entries examined (cheap guards: world/alive/lease/owner) per
    // auction candidate.
    inline constexpr uint32_t kBuyerExamineCap = 24;

    // Organic buyer trip purse rule (mirrors AhBuyerTripNeeded in
    // MaintenanceValues.cpp): spare gold above the trainer reserve must cover
    // at least the cheapest realistic opening bid. Below one silver the AH
    // cannot clear anyway (deposit + minimum-bid granularity).
    inline constexpr uint32_t kBuyerTripMinSpareCopper = 100;

    inline bool BuyerTripAffordable(uint32_t moneyCopper, uint32_t spellReserveCopper)
    {
        if (moneyCopper <= spellReserveCopper)
            return false;
        return (moneyCopper - spellReserveCopper) >= kBuyerTripMinSpareCopper;
    }

    // Organic buyer travel share: only this slice of the hourly RPG phase
    // walks to the AH to shop (same GetFixedBotNumber(., 60, 1) clock the
    // GenericRpg/Grind stagger uses). Keeps shopping trips bounded: not
    // every rich bot walks at once.
    inline constexpr uint32_t kBuyerTripPhaseMax = 60;
    inline constexpr uint32_t kBuyerTripPhaseBelow = 15;

    inline bool BuyerTripPhaseOpen(uint32_t rpgPhase)
    {
        return rpgPhase < kBuyerTripPhaseBelow;
    }

} // namespace ai
