#pragma once
#include <cstdint>
#include <vector>

// Pure policy for the idle near-service rule (issue #379): a bot that is
// waiting must use the class trainer or vendor next to it instead of asking for
// a new journey it may never finish. The world-facing part (which NPCs are in
// range, which need is real) lives in NearbyServiceTarget(); the decisions
// below are pure so they can be tested on their own.

namespace ai
{
    // The nearest is walked to, so "nearby" is the same camp or town square, not
    // a journey: 50 yd is under ten seconds at run speed and sits inside the
    // bot's sight range.
    const float NEARBY_SERVICE_RANGE = 50.0f;

    // Bag pressure valve: bags at or above this fill are serviced at the next
    // vendor whatever the batch counters say, so a full bag cannot lock a bot
    // out of looting.
    const uint8_t NEARBY_SERVICE_BAG_PRESSURE = 85;

    inline float NearbyServiceRangeSq()
    {
        return NEARBY_SERVICE_RANGE * NEARBY_SERVICE_RANGE;
    }

    inline bool NearbyServiceBagPressure(uint8_t bagSpacePercentFull)
    {
        return bagSpacePercentFull >= NEARBY_SERVICE_BAG_PRESSURE;
    }

    // One NPC the rule could walk to. rank: 0 = vendor, 1 = class trainer,
    // negative = not a service target at all. Vendors outrank trainers even when
    // the trainer is nearer: the sale is what funds the next class rank, and the
    // trainer is picked up on a later tick once the purse can pay.
    struct NearbyServiceCandidate
    {
        int rank;
        float sqDistance;
    };

    // Index of the candidate to service, -1 when there is none. Lowest rank
    // wins; ties go to the nearest.
    inline int BestNearbyServiceCandidate(std::vector<NearbyServiceCandidate> const& candidates)
    {
        int best = -1;

        for (size_t i = 0; i < candidates.size(); ++i)
        {
            NearbyServiceCandidate const& candidate = candidates[i];

            if (candidate.rank < 0)
                continue;

            if (best < 0 || candidate.rank < candidates[best].rank ||
                (candidate.rank == candidates[best].rank && candidate.sqDistance < candidates[best].sqDistance))
            {
                best = static_cast<int>(i);
            }
        }

        return best;
    }
}
