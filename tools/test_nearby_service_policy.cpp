#include "../ai/playerbot/strategy/values/NearbyServicePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::BestNearbyServiceCandidate;
using ai::NearbyServiceBagPressure;
using ai::NearbyServiceCandidate;
using ai::NearbyServiceKind;
using ai::NearbyServiceRangeSq;
using ai::NearbyServiceRankOf;

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
    // "busy", so a bot parked next to a vendor never serviced it.
    CHECK(ai::NearbyServiceTravelOwnsBot(ai::NEARBY_SERVICE_TRAVEL_STATUS_PREPARE) == true);   // preparing a journey
    CHECK(ai::NearbyServiceTravelOwnsBot(ai::NEARBY_SERVICE_TRAVEL_STATUS_TRAVEL) == true);    // walking it
    CHECK(ai::NearbyServiceTravelOwnsBot(0) == false);                                         // no target
    CHECK(ai::NearbyServiceTravelOwnsBot(2) == false);                                         // arrived (READY)
    CHECK(ai::NearbyServiceTravelOwnsBot(4) == false);                                         // working at it
    CHECK(ai::NearbyServiceTravelOwnsBot(5) == false);                                         // cooldown
    CHECK(ai::NearbyServiceTravelOwnsBot(6) == false);                                         // expired
    std::cout << "  [PASS] only a journey in flight blocks the rule\n";

    std::cout << "All idle near-service policy checks PASSED!\n";
    return 0;
}
