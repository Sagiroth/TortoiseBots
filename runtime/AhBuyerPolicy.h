#pragma once

#include <cstdint>

// Pure policy for the organic AH buyer trip (issue #405 rework, review 2).
//
// NO buyer teleport: the market buyer bids only with a pool bot ALREADY
// standing at an auctioneer of the auction's house (arrived on its own feet,
// e.g. on a sell trip or an AH travel errand). Teleport stays only the
// pre-existing stuck rescue on the way there - nothing new. To make buying
// happen organically, a masterless pool bot holding spendable gold gets the
// normal AH travel purpose and walks/flies there (same destination the seller
// uses); the arrival bid stays house-matched and budget-gated.
//
// Safety (review-y-405b finding 3): shopping trips are for established bots
// only (level 10+, past the beginner death belt), cost a real purse (several
// gold above the trainer reserve, not 1 silver), run in a narrow RPG-phase
// slice (few bots at once), and never leave the bot's own continent - the
// route to its own capital stays inside zones the level gates already allow.
// The point-danger / grind-cap gates keep applying on the way: this policy
// only opens the door, it never overrides a forbidden area.
//
// One money rule everywhere (finding 4): the trip trigger and the arrival
// bid use the same "free money for <usage>" purse the arrival path reads.
// The trigger demands spendable money for the weakest arrival usage so a bot
// never walks miles just to fail every budget gate on arrival.

namespace ai
{
    // Buyer scan bound (review finding 6): examine == probe, one pass, no
    // blind spot. Every pool entry the scan touches is fully probed, and the
    // rotating start (m_buyerScanIndex) cycles the whole pool over passes.
    // 8 keeps one Buy candidate at the pre-rework AI-context cost.
    inline constexpr uint32_t kBuyerProbeCap = 8;
    inline constexpr uint32_t kBuyerExamineCap = 8;

    // Shopping trips are for established bots: level 10+ has mounts paths,
    // flight access and survivable roads to its own capital; below that the
    // walk crosses mobs that farm the bot into a death spiral (IsPossible
    // already blocks every non-vendor RPG errand below 5; this extends the
    // buyer leg to 10 for the capital walk specifically).
    inline constexpr uint32_t kBuyerTripMinLevel = 10;

    // A shopping trip must be able to pay for *something* once there. The
    // floor is several gold above the trainer reserve: 1 silver only buys a
    // walk to the capital and back with nothing affordable on arrival.
    inline constexpr uint32_t kBuyerTripMinSpareCopper = 50000;

    inline bool BuyerTripAffordable(uint32_t moneyCopper, uint32_t spellReserveCopper)
    {
        if (moneyCopper <= spellReserveCopper)
            return false;
        return (moneyCopper - spellReserveCopper) >= kBuyerTripMinSpareCopper;
    }

    // Organic buyer travel share: only this slice of the hourly RPG phase
    // walks to the AH to shop (same GetFixedBotNumber(., 60, 1) clock the
    // GenericRpg/Grind stagger uses). Narrow slice: a handful of bots per
    // hour, not a quarter of the pool at once. Note GetFixedBotNumber mods
    // by (maxNum + 1), so kBuyerTripPhaseMax = 60 counts 61 slots 0..60.
    inline constexpr uint32_t kBuyerTripPhaseMax = 60;
    inline constexpr uint32_t kBuyerTripPhaseBelow = 3;

    inline bool BuyerTripPhaseOpen(uint32_t rpgPhase)
    {
        return rpgPhase < kBuyerTripPhaseBelow;
    }

    // Same-continent reachability (finding 3): the bot only shops when its
    // own map holds an auction house it can walk/fly to. Cross-map trips
    // (e.g. Kalimdor bot to an Eastern Kingdoms house) are never opened -
    // the destination search has no cross-continent path for them.
    inline bool BuyerTripSameContinent(uint32_t botMapId, bool sameMapHouseExists)
    {
        (void)botMapId;
        return sameMapHouseExists;
    }

} // namespace ai
