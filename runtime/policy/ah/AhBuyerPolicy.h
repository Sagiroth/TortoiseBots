#pragma once

#include <algorithm>
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

    // Organic AH buyer pricing & bid decision rules (moved from removed synthetic policy):
    // Quality price multiplier in percent of the base price (POOR..ARTIFACT).
    inline uint32_t AhBuyerQualityMultiplier(uint32_t quality)
    {
        switch (quality)
        {
            case 0: return 50;
            case 1: return 100;
            case 2: return 200;
            case 3: return 400;
            case 4: return 800;
            case 5: return 1600;
            case 6: return 3200;
            default: return 100;
        }
    }

    inline uint32_t AhBuyerBasePrice(uint32_t buyPrice, uint32_t sellPrice, uint32_t quality)
    {
        uint32_t qualityMultiplier = AhBuyerQualityMultiplier(quality);
        uint64_t base = buyPrice;
        if (!base || (sellPrice && buyPrice / sellPrice > 5))
            base = uint64_t(sellPrice) * (quality <= 1 ? 4 : 5);
        if (!base)
            base = 1;
        return uint32_t(std::min<uint64_t>(base * qualityMultiplier / 100, 0x7fffffffULL));
    }

    inline uint32_t AhBuyerVariedPrice(uint32_t price, int32_t deltaPercent)
    {
        int64_t varied = int64_t(price) + (int64_t(price) * deltaPercent / 100);
        if (varied <= 0)
            return 1;
        return static_cast<uint32_t>(std::min<int64_t>(varied, 0x7fffffffLL));
    }

    // A bot never bids on its own listing, a same-account listing, or one it
    // already holds the high bid on.
    inline bool AhBuyerExcluded(uint32_t auctionOwner, uint32_t auctionOwnerAccount,
        uint32_t currentBidder, uint32_t buyerGuidLow, uint32_t buyerAccountId)
    {
        return auctionOwner == buyerGuidLow ||
               auctionOwnerAccount == buyerAccountId ||
               currentBidder == buyerGuidLow;
    }

    inline uint64_t AhBuyerWillingness(uint32_t baseUnitPrice, uint32_t medianUnitPrice,
        uint32_t count, uint32_t buyValuePercent)
    {
        uint32_t fairUnit = baseUnitPrice;
        if (medianUnitPrice > 0)
            fairUnit = (baseUnitPrice + medianUnitPrice) / 2;
        return (uint64_t(fairUnit) * count * buyValuePercent) / 100;
    }

    inline bool AhBuyerTargetPrice(uint64_t willingness, uint32_t buyout, uint32_t currentBid,
        uint32_t outbid, uint32_t startbid, uint32_t& targetPrice)
    {
        uint32_t nextBid = currentBid ? (currentBid + outbid) : startbid;
        bool canBuyout = (buyout > 0 && willingness >= buyout);
        bool canBid = (!canBuyout && willingness >= nextBid && (buyout == 0 || nextBid < buyout));
        if (!canBuyout && !canBid)
            return false;
        targetPrice = canBuyout ? buyout : nextBid;
        return true;
    }

} // namespace ai
