// Standalone regression test for the follow-time transport snap decision
// (ai/playerbot/TransportSnapPolicy.h): a raw transport pointer mismatch
// must not stop the bot once nobody is geometrically aboard.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_transport_snap_policy.cpp -o /tmp/test_transport_snap
//   /tmp/test_transport_snap

#include "../ai/playerbot/TransportSnapPolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace ai;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

// Legit boarding: master aboard, bot on solid ground standing on the
// master's transport. Legit alighting: the mirror image.
static void TestBoardAndAlight()
{
    CHECK(DecideTransportSnapStep(true, false, false, true) == TransportSnapStep::Board);
    CHECK(DecideTransportSnapStep(false, true, true, false) == TransportSnapStep::Alight);
}

// Master aboard but the bot nowhere near the transport: no boarding, and no
// stop either (the bot waits on the dock while the master sails off).
static void TestMasterAboardBotAshored()
{
    CHECK(DecideTransportSnapStep(true, false, false, false) == TransportSnapStep::None);
}

// Both aboard the same transport (or both on solid ground with equal
// pointers, which never reaches the branch): nothing to do.
static void TestBothAboard()
{
    CHECK(DecideTransportSnapStep(true, true, true, false) == TransportSnapStep::None);
    CHECK(DecideTransportSnapStep(true, false, true, false) == TransportSnapStep::None);
}

// The reported bug: a stale master pointer with the bot on solid ground.
// No boarding target, no alighting, no stale bot pointer: leave the follow
// alone instead of stopping and snapping the bot every tick.
static void TestStaleMasterPointer()
{
    CHECK(DecideTransportSnapStep(false, false, false, false) == TransportSnapStep::None);
}

// A stale bot pointer (transport set, bot not standing on it, master on
// solid ground): drop the pointer without a teleport. Silently clearing
// matters: the core skips the state clear when the bot is no longer in the
// passenger set, so teleporting here would loop forever.
static void TestStaleBotPointer()
{
    CHECK(DecideTransportSnapStep(false, true, false, false) == TransportSnapStep::DetachStale);
}

// Bot geometrically aboard its own transport while the master pointer is
// stale: still a legit alighting, not a stale detach.
static void TestBotAboardDespiteStaleMaster()
{
    CHECK(DecideTransportSnapStep(false, true, true, false) == TransportSnapStep::Alight);
}

int main()
{
    TestBoardAndAlight();
    TestMasterAboardBotAshored();
    TestBothAboard();
    TestStaleMasterPointer();
    TestStaleBotPointer();
    TestBotAboardDespiteStaleMaster();
    std::printf("transport_snap_policy: %d checks passed\n", checks);
    return 0;
}
