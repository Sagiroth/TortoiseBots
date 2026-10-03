#pragma once

#include <cstdint>

// Pure policy for the AH buyer teleport-or-skip (issue #405).
//
// The buyer used to bid only while standing next to an auctioneer and never
// teleported, unlike the seller — so with pool bots rarely in cities it could
// never fire. It now shares the seller safety net: prefer a bot already at a
// matching-house auctioneer, else teleport one eligible bot and defer the bid
// until the next Buy pass revisits the listing.
//
// Bounds keep one buyer candidate cheap for a 500-bot pool: at most
// kBuyerExamineCap pool entries are examined (cheap guards only) and at most
// kBuyerProbeCap of them touch an AI context ("nearest npcs" + interact
// check) per auction candidate. Teleports are capped globally at one per
// market interval, and a teleported bot sits out re-teleport selection for
// one interval via the "ahMarketLastBuy" facade cooldown (it can still bid
// once it stands at the auctioneer — the cooldown gates teleport, not bids).

namespace TortoiseBots
{
    // Max AI-context probes ("nearest npcs" + interact + house resolve) per
    // auction candidate. Everything past the cap is teleport-fallback only.
    inline constexpr uint32_t kBuyerProbeCap = 8;
    // Max pool entries examined (cheap guards: world/alive/lease/owner) per
    // auction candidate. Random start rotates coverage across passes.
    inline constexpr uint32_t kBuyerExamineCap = 24;

    // Clamp range for buyer cadences, shared with the seller attempt cooldown
    // (AhMarketService::TryPostForBot clamps to the same 5..3600 window).
    inline constexpr uint32_t kBuyerCooldownMinSec = 5;
    inline constexpr uint32_t kBuyerCooldownMaxSec = 3600;

    // Per-bot trip cooldown after a buyer teleport: one market interval, so a
    // bot en route is not re-teleported for the next listing while it travels.
    inline uint32_t BuyerTripCooldownSec(uint32_t marketIntervalSec)
    {
        if (marketIntervalSec < kBuyerCooldownMinSec)
            return kBuyerCooldownMinSec;
        if (marketIntervalSec > kBuyerCooldownMaxSec)
            return kBuyerCooldownMaxSec;
        return marketIntervalSec;
    }

    // Global buyer teleport budget: at most one teleport per market interval.
    // Times are seconds (time(nullptr)); a zero lastTeleport allows the first.
    inline bool BuyerTeleportAllowed(int64_t nowSec, int64_t lastTeleportSec, uint32_t marketIntervalSec)
    {
        uint32_t interval = BuyerTripCooldownSec(marketIntervalSec);
        return (nowSec - lastTeleportSec) >= (int64_t)interval;
    }

} // namespace TortoiseBots
