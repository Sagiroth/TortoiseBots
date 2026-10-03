#include "../ai/playerbot/strategy/values/NearbyServicePolicy.h"

#include <cstdlib>
#include <ctime>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::BestNearbyServiceCandidate;
using ai::NEARBY_SERVICE_FAIL_PARK_SECONDS;
using ai::NEARBY_SERVICE_FAIL_PARK_TRIPS;
using ai::NEARBY_SERVICE_FAIL_SLOTS;
using ai::NearbyServiceBagPressure;
using ai::NearbyServiceCandidate;
using ai::NearbyServiceFailParks;
using ai::NearbyServiceKind;
using ai::NearbyServiceRangeSq;
using ai::NearbyServiceRankOf;
using ai::NearbyServiceShouldPark;
using ai::NearbyServiceTargetParked;
using ai::NearbyServiceVerbMadeProgress;

static NearbyServiceCandidate Candidate(NearbyServiceKind kind, float sqDistance)
{
    return NearbyServiceCandidate{ NearbyServiceRankOf(kind), sqDistance };
}

int main()
{
    std::cout << "Starting TortoiseBots idle near-service policy tests...\n";

    // Bag pressure valve (issue #379 proposal 2): bags at/above the line are
    // sold whatever the batch counters say; below it the batch/spell rules
    // still decide.
    CHECK(NearbyServiceBagPressure(0) == false);
    CHECK(NearbyServiceBagPressure(84) == false);
    CHECK(NearbyServiceBagPressure(85) == true);
    CHECK(NearbyServiceBagPressure(100) == true);
    std::cout << "  [PASS] bag pressure valve behaves at the boundary\n";

    // The service order is hand-in, accept, sell, train: a finished quest is the
    // reward item and its XP, an accept is free, and the town errands come after
    // both. "None" is never a target.
    CHECK(NearbyServiceRankOf(NearbyServiceKind::TurnIn) < NearbyServiceRankOf(NearbyServiceKind::Accept));
    CHECK(NearbyServiceRankOf(NearbyServiceKind::Accept) < NearbyServiceRankOf(NearbyServiceKind::Vendor));
    CHECK(NearbyServiceRankOf(NearbyServiceKind::Vendor) < NearbyServiceRankOf(NearbyServiceKind::Trainer));
    CHECK(NearbyServiceRankOf(NearbyServiceKind::None) < 0);
    std::cout << "  [PASS] quest work outranks the town errands\n";

    // A quest taker outranks a nearer quest giver: the hand-in frees the log slot
    // the accept needs, and only the hand-in pays out.
    {
        std::vector<NearbyServiceCandidate> candidates = {
            Candidate(NearbyServiceKind::Accept, 4.0f),    // giver, 2 yd
            Candidate(NearbyServiceKind::TurnIn, 900.0f),  // taker, 30 yd
        };
        CHECK(BestNearbyServiceCandidate(candidates) == 1);
    }
    std::cout << "  [PASS] turn-in outranks a nearer accept\n";

    // An available quest outranks a nearer vendor: accepting costs nothing and
    // feeds the loop, the sale waits for a later tick.
    {
        std::vector<NearbyServiceCandidate> candidates = {
            Candidate(NearbyServiceKind::Vendor, 1.0f),
            Candidate(NearbyServiceKind::Accept, 1600.0f),
        };
        CHECK(BestNearbyServiceCandidate(candidates) == 1);
    }
    std::cout << "  [PASS] accept outranks a nearer vendor\n";

    // A vendor always outranks a trainer, even a nearer one: the sale funds the
    // next class rank, and the trainer is serviced on a later tick.
    {
        std::vector<NearbyServiceCandidate> candidates = {
            Candidate(NearbyServiceKind::Trainer, 4.0f),   // trainer, 2 yd
            Candidate(NearbyServiceKind::Vendor, 900.0f),  // vendor, 30 yd
        };
        CHECK(BestNearbyServiceCandidate(candidates) == 1);
    }
    std::cout << "  [PASS] vendor outranks a nearer trainer\n";

    // Within one rank the nearest wins, whatever the order they were found in.
    {
        std::vector<NearbyServiceCandidate> candidates = {
            Candidate(NearbyServiceKind::Trainer, 900.0f),
            Candidate(NearbyServiceKind::Trainer, 16.0f),
            Candidate(NearbyServiceKind::Trainer, 400.0f),
        };
        CHECK(BestNearbyServiceCandidate(candidates) == 1);
    }
    std::cout << "  [PASS] nearest wins inside one rank\n";

    // Non-service NPCs (the 200 yd rpg target list is full of them) are ignored.
    {
        std::vector<NearbyServiceCandidate> candidates = {
            Candidate(NearbyServiceKind::None, 1.0f),
            Candidate(NearbyServiceKind::None, 4.0f),
        };
        CHECK(BestNearbyServiceCandidate(candidates) == -1);
    }
    std::cout << "  [PASS] no service target when nothing qualifies\n";

    {
        CHECK(BestNearbyServiceCandidate({}) == -1);
    }
    std::cout << "  [PASS] empty candidate list has no target\n";

    // NPCs that are not service targets (the 200 yd rpg target list is full of
    // them) never shadow a qualifying one, however near they are.
    {
        std::vector<NearbyServiceCandidate> candidates = {
            Candidate(NearbyServiceKind::None, 4.0f),
            Candidate(NearbyServiceKind::Trainer, 900.0f),
            Candidate(NearbyServiceKind::Vendor, 2500.0f),
        };
        CHECK(BestNearbyServiceCandidate(candidates) == 2);
    }
    std::cout << "  [PASS] ranking ignores non-service NPCs\n";

    // The 50 yd radius is enforced with squared distances upstream; the policy
    // only supplies the bound.
    CHECK(NearbyServiceRangeSq() == 2500.0f);
    std::cout << "  [PASS] nearby radius is 50 yd\n";

    // Only a journey in flight owns the bot. A live pool produced 0 SellAction
    // rows in 90 minutes because the rule treated every active travel target -
    // including the Grind target a bot sits on for its whole work phase - as
    // "busy", so a bot parked next to a vendor never serviced it. The travel
    // request/multiplier ask the same helper for the bag-pressure vendor errand.
    CHECK(ai::JourneyInFlightOwnsBot(ai::NEARBY_SERVICE_TRAVEL_STATUS_PREPARE) == true);   // preparing a journey
    CHECK(ai::JourneyInFlightOwnsBot(ai::NEARBY_SERVICE_TRAVEL_STATUS_TRAVEL) == true);    // walking it
    CHECK(ai::JourneyInFlightOwnsBot(0) == false);                                         // no target
    CHECK(ai::JourneyInFlightOwnsBot(2) == false);                                         // arrived (READY)
    CHECK(ai::JourneyInFlightOwnsBot(4) == false);                                         // working at it
    CHECK(ai::JourneyInFlightOwnsBot(5) == false);                                         // cooldown
    CHECK(ai::JourneyInFlightOwnsBot(6) == false);                                         // expired
    std::cout << "  [PASS] only a journey in flight blocks the rule\n";

    // A verb that keeps failing on one NPC+kind earns a brief park (issue
    // #407): 3 consecutive fails trip it, the park lasts 90 s, and it must not
    // approach the 10-min trainer/travel parks - a hand-in or affordable rank
    // is only delayed, never missed.
    CHECK(NEARBY_SERVICE_FAIL_PARK_TRIPS == 3);
    CHECK(NEARBY_SERVICE_FAIL_PARK_SECONDS == 90);
    CHECK(!NearbyServiceShouldPark(0));
    CHECK(!NearbyServiceShouldPark(NEARBY_SERVICE_FAIL_PARK_TRIPS - 1));
    CHECK(NearbyServiceShouldPark(NEARBY_SERVICE_FAIL_PARK_TRIPS));
    CHECK(NearbyServiceShouldPark(NEARBY_SERVICE_FAIL_PARK_TRIPS + 5));
    std::cout << "  [PASS] three consecutive fails earn a park\n";

    {
        std::time_t const now = 1'000'000;
        CHECK(!NearbyServiceTargetParked(0, now));
        CHECK(NearbyServiceTargetParked(now + NEARBY_SERVICE_FAIL_PARK_SECONDS, now));
        CHECK(NearbyServiceTargetParked(now + NEARBY_SERVICE_FAIL_PARK_SECONDS - 1, now));
        CHECK(!NearbyServiceTargetParked(now + NEARBY_SERVICE_FAIL_PARK_SECONDS, now + NEARBY_SERVICE_FAIL_PARK_SECONDS));
        CHECK(!NearbyServiceTargetParked(now - 1, now));
    }
    std::cout << "  [PASS] fail park holds 90 s then expires\n";

    // The fixed-size tracker behind the real code (issue #407 review): 3
    // consecutive fails park one NPC+verb for 90 s, other verbs on the same
    // NPC stay live, success clears, expiry re-arms, and the table never
    // grows past its slots.
    {
        std::time_t const now = 2'000'000;
        NearbyServiceFailParks parks;
        uint64_t const npc = 12345;
        int const turnIn = NearbyServiceRankOf(NearbyServiceKind::TurnIn);
        int const vendor = NearbyServiceRankOf(NearbyServiceKind::Vendor);

        CHECK(!parks.Parked(npc, turnIn, now));
        parks.RecordFail(npc, turnIn, now);
        parks.RecordFail(npc, turnIn, now);
        CHECK(!parks.Parked(npc, turnIn, now));
        parks.RecordFail(npc, turnIn, now);
        CHECK(parks.Parked(npc, turnIn, now));
        // Same NPC, other verb: unaffected.
        CHECK(!parks.Parked(npc, vendor, now));
        // Expiry re-arms.
        CHECK(!parks.Parked(npc, turnIn, now + NEARBY_SERVICE_FAIL_PARK_SECONDS));
        parks.RecordFail(npc, turnIn, now + NEARBY_SERVICE_FAIL_PARK_SECONDS);
        CHECK(!parks.Parked(npc, turnIn, now + NEARBY_SERVICE_FAIL_PARK_SECONDS));

        // Success clears the pair.
        parks.RecordFail(npc, vendor, now);
        parks.RecordFail(npc, vendor, now);
        parks.RecordFail(npc, vendor, now);
        CHECK(parks.Parked(npc, vendor, now));
        parks.Clear(npc, vendor);
        CHECK(!parks.Parked(npc, vendor, now));
    }
    std::cout << "  [PASS] tracker parks one verb, spares the others\n";

    {
        std::time_t const now = 3'000'000;
        NearbyServiceFailParks parks;
        // Fill every slot with a live park, then force a fifth pair in: the
        // table must still hold at most NEARBY_SERVICE_FAIL_SLOTS entries.
        for (std::size_t s = 0; s < NEARBY_SERVICE_FAIL_SLOTS; ++s)
        {
            uint64_t const npc = 1000 + s;
            int const verb = NearbyServiceRankOf(NearbyServiceKind::Vendor);
            parks.RecordFail(npc, verb, now);
            parks.RecordFail(npc, verb, now);
            parks.RecordFail(npc, verb, now);
            CHECK(parks.Parked(npc, verb, now));
        }
        parks.RecordFail(9999, NearbyServiceRankOf(NearbyServiceKind::Trainer), now);
        parks.RecordFail(9999, NearbyServiceRankOf(NearbyServiceKind::Trainer), now);
        parks.RecordFail(9999, NearbyServiceRankOf(NearbyServiceKind::Trainer), now);
        CHECK(parks.Parked(9999, NearbyServiceRankOf(NearbyServiceKind::Trainer), now));
        int live = 0;
        for (std::size_t s = 0; s < NEARBY_SERVICE_FAIL_SLOTS; ++s)
            if (parks.slots[s].npcGuid != 0)
                ++live;
        CHECK(live <= (int)NEARBY_SERVICE_FAIL_SLOTS);
    }
    std::cout << "  [PASS] tracker never grows past its slots\n";

    // A verb that ran but changed nothing counts as a failure, not a success
    // (issue #407 follow-up: a trainer that taught nothing logged 6
    // NearbyService successes in 1 s and re-queued every tick). The caller
    // re-evaluates the verb's own applicability after a done==true run; an
    // unchanged answer means nothing moved.
    CHECK(NearbyServiceVerbMadeProgress(NearbyServiceKind::TurnIn, false) == true);
    CHECK(NearbyServiceVerbMadeProgress(NearbyServiceKind::TurnIn, true) == false);
    CHECK(NearbyServiceVerbMadeProgress(NearbyServiceKind::Accept, false) == true);
    CHECK(NearbyServiceVerbMadeProgress(NearbyServiceKind::Accept, true) == false);
    CHECK(NearbyServiceVerbMadeProgress(NearbyServiceKind::Trainer, false) == true);
    CHECK(NearbyServiceVerbMadeProgress(NearbyServiceKind::Trainer, true) == false);
    std::cout << "  [PASS] unchanged verb state after a run counts as a fail\n";

    // Vendor needs no re-check: SellAction returns false when it sold nothing,
    // so done==true always means stock moved.
    CHECK(NearbyServiceVerbMadeProgress(NearbyServiceKind::Vendor, false) == true);
    CHECK(NearbyServiceVerbMadeProgress(NearbyServiceKind::Vendor, true) == true);
    std::cout << "  [PASS] vendor success always counts as progress\n";

    // And the three-strike park still applies to no-progress runs: the sixth
    // 1-second trainer success in the live log would have been the third
    // consecutive fail and parked the pair for 90 s instead.
    {
        std::time_t const now = 4'000'000;
        NearbyServiceFailParks parks;
        uint64_t const npc = 777;
        int const trainer = NearbyServiceRankOf(NearbyServiceKind::Trainer);

        CHECK(NearbyServiceVerbMadeProgress(NearbyServiceKind::Trainer, true) == false);
        parks.RecordFail(npc, trainer, now);
        parks.RecordFail(npc, trainer, now);
        CHECK(!parks.Parked(npc, trainer, now));
        parks.RecordFail(npc, trainer, now);
        CHECK(parks.Parked(npc, trainer, now));
    }
    std::cout << "  [PASS] no-progress fails trip the 90 s park\n";

    std::cout << "All idle near-service policy checks PASSED!\n";
    return 0;
}
