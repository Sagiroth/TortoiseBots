#include "../ai/playerbot/TravelLegChainPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    std::cout << "Starting TortoiseBots travel leg chain policy tests...\n";

    // No live spline: the end-of-leg stop needs no chain.
    CHECK(TravelLegChainDecide(true, 0, false, false) == TravelLegChainVerdict::WaitFinalized);
    CHECK(TravelLegChainDecide(true, 0, true, true) == TravelLegChainVerdict::WaitFinalized);
    std::cout << "  [PASS] no live spline waits\n";

    // Another movement owns the spline: not our leg.
    CHECK(TravelLegChainDecide(false, 0, true, false) == TravelLegChainVerdict::WaitOtherOrigin);
    CHECK(TravelLegChainDecide(false, 100, true, false) == TravelLegChainVerdict::WaitOtherOrigin);
    std::cout << "  [PASS] foreign spline waits\n";

    // Our leg with far to go: let it walk.
    CHECK(TravelLegChainDecide(true, TRAVEL_LEG_CHAIN_REMAIN_MS + 1, true, false) == TravelLegChainVerdict::WaitSpline);
    std::cout << "  [PASS] mid-leg spline waits\n";

    // Our leg nearly finished: chain the next leg in the same turn.
    CHECK(TravelLegChainDecide(true, TRAVEL_LEG_CHAIN_REMAIN_MS, true, false) == TravelLegChainVerdict::Chain);
    CHECK(TravelLegChainDecide(true, 0, true, false) == TravelLegChainVerdict::Chain);
    CHECK(TravelLegChainDecide(true, -100, true, false) == TravelLegChainVerdict::Chain);
    std::cout << "  [PASS] tail spline chains\n";

    std::cout << "All TortoiseBots travel leg chain policy tests passed.\n";
    return 0;
}
