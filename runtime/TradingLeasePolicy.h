#pragma once

#include <cstdint>

// Pure policy for the AH seller Trading lease (issue #404): how long a
// Trading lease must live so a teleported seller survives until its post.
//
// A teleported bot posts on the NEXT seller tick, i.e. one full market
// interval after the teleport. The old flat 120 s lease therefore expired in
// a race with the post at the default 120 s interval (29 expiries vs ~1 post
// in the 2026-10-02 measurement), and each expiry cleared the attempt
// cooldown, so the trip restarted every cycle. The lease must cover two ticks
// plus teleport/load margin, bounded so a stuck bot cannot squat Trading
// (which blocks its LFT/BG acquisition) for hours on huge intervals.

namespace TortoiseBots
{
    // Floor: teleport + one full default-interval trip with margin.
    inline constexpr uint32_t kTradingTripLeaseMinMs = 180000;
    // Cap: a stuck trip is evicted by ten minutes at the latest.
    inline constexpr uint32_t kTradingTripLeaseMaxMs = 600000;
    // Extra margin over two full seller ticks.
    inline constexpr uint32_t kTradingTripLeaseMarginMs = 60000;

    // marketIntervalMs is the clamped seller tick (AhMarketService::Update).
    inline uint32_t TradingTripLeaseMs(uint32_t marketIntervalMs)
    {
        uint64_t lease = uint64_t(marketIntervalMs) * 2 + kTradingTripLeaseMarginMs;
        if (lease < kTradingTripLeaseMinMs)
            lease = kTradingTripLeaseMinMs;
        if (lease > kTradingTripLeaseMaxMs)
            lease = kTradingTripLeaseMaxMs;
        return uint32_t(lease);
    }

} // namespace TortoiseBots
