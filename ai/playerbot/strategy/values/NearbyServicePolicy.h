#pragma once
#include <cstddef>
#include <cstdint>
#include <ctime>
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

    // TravelStatus values the rule has to know about (0 = none, 1 = prepare,
    // 2 = ready, 3 = travel, 4 = work, 5 = cooldown, 6 = expired). The production
    // path static_asserts these against TravelStatus so they cannot drift.
    const int NEARBY_SERVICE_TRAVEL_STATUS_PREPARE = 1;
    const int NEARBY_SERVICE_TRAVEL_STATUS_TRAVEL = 3;

    // Only a journey in flight owns the bot: preparing one, or walking it. A
    // target that is merely set - the bot arrived and is waiting out its
    // work/cooldown at the destination - leaves the bot where it stands, so the
    // quest giver, trainer or vendor next to it must be usable then (the idle
    // near-service rule) and a bag-pressure vendor errand may still start (the
    // travel layer). Blocking on any active target meant the near-service rule
    // could only fire in the gaps between journeys, which is exactly when the
    // travel layer grabs the bot again: live, a bot parked next to a vendor with
    // a live Grind target never serviced it, 0 SellAction rows were produced in
    // 90 minutes, and the vendor travel request showed up as USELESS (`travel
    // target active`) in the old pool's decision trails while the bot sat at its
    // destination.
    inline bool JourneyInFlightOwnsBot(int travelStatus)
    {
        return travelStatus == NEARBY_SERVICE_TRAVEL_STATUS_PREPARE ||
            travelStatus == NEARBY_SERVICE_TRAVEL_STATUS_TRAVEL;
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

    // A verb that keeps failing on the same NPC+kind is never finished by
    // standing there (issue #407: 34 ACTION_LOOP rows on 30 bots). Park that
    // pair briefly instead of retrying every tick: the bot walks on, and the
    // park expires on its own. 90 s is long enough to break the tick loop but
    // far short of the 10-min trainer/travel parks, so a quest hand-in or a
    // rank that becomes affordable is only delayed, never missed - the target
    // selector keeps offering the NPC for its other verbs meanwhile.
    constexpr int NEARBY_SERVICE_FAIL_PARK_SECONDS = 90;
    constexpr int NEARBY_SERVICE_FAIL_PARK_TRIPS = 3;

    // True while `parkedUntil` (0 = never parked) still holds at `now`.
    inline bool NearbyServiceTargetParked(time_t parkedUntil, time_t now)
    {
        return parkedUntil != 0 && now < parkedUntil;
    }

    // True once `failures` consecutive fails on one NPC+verb earn a park.
    inline bool NearbyServiceShouldPark(int failures)
    {
        return failures >= NEARBY_SERVICE_FAIL_PARK_TRIPS;
    }

    // Fixed-size per-bot record of repeatedly failing NPC+verb pairs (issue
    // #407 review). A "manual time/int" value per NPC guid would grow the value
    // store without bound (every NPC the bot ever looks at leaves a heap
    // entry), so the park state lives here instead: at most MAX entries, never
    // allocated per NPC. Owned by the per-bot AI context (see
    // NearbyServiceFailParksValue), so it dies with the bot and needs no manual
    // cleanup. Slots hold raw NPC guids + the verb kind so one parked verb
    // never blocks the NPC's other verbs.
    constexpr std::size_t NEARBY_SERVICE_FAIL_SLOTS = 4;

    struct NearbyServiceFailSlot
    {
        uint64_t npcGuid = 0;
        int verb = static_cast<int>(NearbyServiceKind::None);
        int fails = 0;
        time_t parkedUntil = 0;
    };

    struct NearbyServiceFailParks
    {
        NearbyServiceFailSlot slots[NEARBY_SERVICE_FAIL_SLOTS]{};

        // Slot for this pair, or -1. Expired parks do not match: call with
        // now to ignore them, or 0 to match regardless of expiry.
        int Find(uint64_t npcGuid, int verb, time_t now) const
        {
            for (std::size_t i = 0; i < NEARBY_SERVICE_FAIL_SLOTS; ++i)
            {
                NearbyServiceFailSlot const& slot = slots[i];
                if (slot.npcGuid != npcGuid || slot.verb != verb)
                    continue;
                if (now != 0 && slot.parkedUntil != 0 && now >= slot.parkedUntil)
                    continue;
                return static_cast<int>(i);
            }
            return -1;
        }

        // True while this pair's park holds at now.
        bool Parked(uint64_t npcGuid, int verb, time_t now) const
        {
            int const i = Find(npcGuid, verb, now);
            return i >= 0 && NearbyServiceTargetParked(slots[i].parkedUntil, now);
        }

        // Record one failure; parks on the Nth consecutive fail. Reuses the
        // pair's slot, else the first expired/empty slot, else slot 0 (all
        // slots live - rare with 4 verbs max per NPC; one live park is
        // dropped early). Never grows.
        void RecordFail(uint64_t npcGuid, int verb, time_t now)
        {
            int i = Find(npcGuid, verb, 0);
            if (i < 0)
            {
                for (std::size_t s = 0; s < NEARBY_SERVICE_FAIL_SLOTS; ++s)
                {
                    if (slots[s].npcGuid == 0 ||
                        (slots[s].parkedUntil != 0 && now >= slots[s].parkedUntil))
                    {
                        i = static_cast<int>(s);
                        break;
                    }
                }
                if (i < 0)
                    i = 0;
                slots[i].npcGuid = npcGuid;
                slots[i].verb = verb;
                slots[i].fails = 0;
                slots[i].parkedUntil = 0;
            }
            NearbyServiceFailSlot& slot = slots[i];
            if (slot.parkedUntil != 0 && now < slot.parkedUntil)
                return;
            ++slot.fails;
            if (NearbyServiceShouldPark(slot.fails))
            {
                slot.parkedUntil = now + NEARBY_SERVICE_FAIL_PARK_SECONDS;
                slot.fails = 0;
            }
        }

        void Clear(uint64_t npcGuid, int verb)
        {
            int const i = Find(npcGuid, verb, 0);
            if (i >= 0)
                slots[i] = NearbyServiceFailSlot{};
        }
    };
}
