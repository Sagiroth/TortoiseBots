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
using ai::NearbyServiceRangeSq;

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

    // A vendor always outranks a trainer, even a nearer one: the sale funds the
    // next class rank, and the trainer is serviced on a later tick.
    {
        std::vector<NearbyServiceCandidate> candidates = {
            {1, 4.0f},    // trainer, 2 yd
            {0, 900.0f},  // vendor, 30 yd
        };
        CHECK(BestNearbyServiceCandidate(candidates) == 1);
    }
    std::cout << "  [PASS] vendor outranks a nearer trainer\n";

    // Within one rank the nearest wins, whatever the order they were found in.
    {
        std::vector<NearbyServiceCandidate> candidates = {
            {1, 900.0f},
            {1, 16.0f},
            {1, 400.0f},
        };
        CHECK(BestNearbyServiceCandidate(candidates) == 1);
    }
    std::cout << "  [PASS] nearest wins inside one rank\n";

    // Non-service NPCs (the 200 yd rpg target list is full of them) are ignored.
    {
        std::vector<NearbyServiceCandidate> candidates = {
            {-1, 1.0f},
            {-1, 4.0f},
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
            {-1, 4.0f},
            {1, 900.0f},
            {0, 2500.0f},
        };
        CHECK(BestNearbyServiceCandidate(candidates) == 2);
    }
    std::cout << "  [PASS] ranking ignores non-service NPCs\n";

    // The 50 yd radius is enforced with squared distances upstream; the policy
    // only supplies the bound.
    CHECK(NearbyServiceRangeSq() == 2500.0f);
    std::cout << "  [PASS] nearby radius is 50 yd\n";

    std::cout << "All idle near-service policy checks PASSED!\n";
    return 0;
}
