#pragma once
#include <cstdint>
#include <vector>

// Pure policy for the idle near-service rule (issue #379): a bot that is
// waiting must use the quest giver, class trainer or vendor next to it instead
// of asking for a new journey it may never finish. The world-facing part (which
// NPCs are in range, which need is real) lives in NearbyServiceTarget(); the
// decisions below are pure so they can be tested on their own.

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

    // What the rule can do with a nearby NPC, in the order it prefers them.
    // Handing in a finished quest is the strongest reason to stop: the reward
    // item and its XP are the only organic gear a low-level bot gets, and the
    // hand-in frees the quest log slot. Accepting a quest the bot can actually
    // take costs nothing and feeds that loop, so it comes next. The town errands
    // come after both, and there the sale still outranks the class rank it
    // funds. None means "not a service target".
    enum class NearbyServiceKind : int
    {
        None = -1,
        TurnIn = 0,
        Accept = 1,
        Vendor = 2,
        Trainer = 3,
    };

    inline int NearbyServiceRankOf(NearbyServiceKind kind)
    {
        return static_cast<int>(kind);
    }

    // One NPC the rule could walk to. rank: lower is serviced first - see
    // NearbyServiceRankOf() - negative = not a service target at all. Ties go to
    // the nearest NPC.
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
