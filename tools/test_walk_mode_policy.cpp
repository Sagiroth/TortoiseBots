#include "../runtime/policy/movement/WalkModePolicy.h"

#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::WalkModePolicy::DispatchWalks;

int main()
{
    std::cout << "Starting TortoiseBots walk-mode tests...\n";

    // A run dispatch with neither the master nor the bot walking. Either
    // walking source expands the dispatch, so a walk flag can never be
    // dropped once the bot walks: the core leaves a launch's pre-launch mode
    // in place on the wire for player units, so a run spline launched on walk
    // flags would pin every watching client in walk mode.
    CHECK(!DispatchWalks(false, false));
    std::cout << "  [PASS] no walk sources, movement runs\n";

    CHECK(DispatchWalks(true, false));
    CHECK(DispatchWalks(false, true));
    CHECK(DispatchWalks(true, true));
    std::cout << "  [PASS] master mimic and action walk flags both dispatch walk\n";

    std::cout << "Walk-mode tests passed.\n";
    return 0;
}
