#include "../ai/playerbot/policy/raid/UseObjectPolicy.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

static UseObjectCandidate Cand(std::uint64_t guid, float near, bool eligible = true)
{
    UseObjectCandidate c;
    c.guid = guid;
    c.near = near;
    c.eligible = eligible;
    return c;
}

int main()
{
    std::cout << "Starting TortoiseBots use_object policy tests...\n";

    // Due: an aura-only row fires on the aura alone, never on its own.
    CHECK(UseObjectDue(true, true, 0.0f, false, 0, 0));
    CHECK(!UseObjectDue(true, false, 0.0f, false, 0, 0));
    CHECK(!UseObjectDue(true, false, 0.0f, true, 1000000, 1000000));
    CHECK(!UseObjectDue(false, true, 0.0f, false, 0, 0));  // no trigger listed: the aura is nobody's
    std::cout << "  [PASS] aura-only rows stay quiet without the aura\n";

    // Due: a timer row runs from first sight, never due at once, due at the window.
    CHECK(!UseObjectDue(false, false, 38.0f, false, 0, 0));
    CHECK(!UseObjectDue(false, false, 38.0f, false, 0, 37999));
    CHECK(UseObjectDue(false, false, 38.0f, false, 0, 38000));
    CHECK(UseObjectDue(false, false, 38.0f, false, 0, 1000000));
    std::cout << "  [PASS] timer rows run from first sight\n";

    // Due: after a click the timer restarts; the sighting no longer matters.
    CHECK(!UseObjectDue(false, false, 38.0f, true, 1000, 1000000));
    CHECK(!UseObjectDue(false, false, 38.0f, true, 37999, 1000000));
    CHECK(UseObjectDue(false, false, 38.0f, true, 38000, 1000000));
    std::cout << "  [PASS] the timer restarts on every click\n";

    // Due across a getMSTime wrap (last 0xfffffff0, now 10): 26 ms elapsed, not 4 billion.
    {
        const std::uint32_t last = 0xfffffff0;
        const std::uint32_t now = 10;
        CHECK(now - last == 26);
        CHECK(last - now > 4000000000u);  // the trap a signed subtraction walks into
        CHECK(!UseObjectDue(false, false, 38.0f, true, now - last, 0));
        CHECK(UseObjectMsToDue(false, false, 38.0f, true, now - last, 0) == 38000 - 26);
    }
    std::cout << "  [PASS] unsigned wrap reads as milliseconds, not billions\n";

    // MsToDue: 0 when due now, UINT32_MAX when only an absent aura could fire it.
    CHECK(UseObjectMsToDue(true, true, 0.0f, false, 0, 0) == 0);
    CHECK(UseObjectMsToDue(true, false, 0.0f, false, 0, 0) == std::numeric_limits<std::uint32_t>::max());
    CHECK(UseObjectMsToDue(false, false, 38.0f, false, 0, 1000) == 37000);
    CHECK(UseObjectMsToDue(false, false, 38.0f, false, 0, 38000) == 0);
    std::cout << "  [PASS] ms-to-due answers the pre-position walk\n";

    // Pick: nearest eligible wins, lowest guid on a tie, -1 when nobody may go.
    {
        std::vector<UseObjectCandidate> none;
        CHECK(PickUseObjectClicker(none) == -1);
        std::vector<UseObjectCandidate> allOut = { Cand(7, 5.0f, false), Cand(9, 3.0f, false) };
        CHECK(PickUseObjectClicker(allOut) == -1);
        std::vector<UseObjectCandidate> nearerBeatsGuid = { Cand(7, 5.0f), Cand(9, 3.0f) };
        CHECK(PickUseObjectClicker(nearerBeatsGuid) == 1);
        std::vector<UseObjectCandidate> tie = { Cand(9, 3.0f), Cand(7, 3.0f) };
        CHECK(PickUseObjectClicker(tie) == 1);
        std::vector<UseObjectCandidate> skipIneligible = { Cand(7, 1.0f, false), Cand(9, 3.0f) };
        CHECK(PickUseObjectClicker(skipIneligible) == 1);
    }
    std::cout << "  [PASS] nearest eligible bot clicks, guid only breaks ties\n";

    // Attempts: at most two per object, at least 8000 ms apart across the record, clicker only.
    {
        const std::uint32_t t0 = 1000000;
        UseObjectAttempts fresh;
        CHECK(fresh.Ready(11, t0));
        CHECK(fresh.Take(11, 5, 5, t0));
        CHECK(!fresh.Ready(11, t0 + 7999));   // still arming
        CHECK(!fresh.Ready(22, t0 + 7999));   // the spacing is record-wide, not per object
        CHECK(fresh.Ready(11, t0 + 8000));
        CHECK(fresh.Take(11, 5, 5, t0 + 8000));
        CHECK(!fresh.Take(11, 5, 5, t0 + 16000));  // burned: two per object
        CHECK(!fresh.Ready(11, t0 + 1000000));
        CHECK(fresh.Take(22, 5, 5, t0 + 16000));   // the other crystal still takes one
        CHECK(fresh.Take(22, 5, 5, t0 + 24000));
        CHECK(!fresh.Take(33, 5, 5, t0 + 32000));  // no third object: two crystals, two tries each
        UseObjectAttempts stranger;
        CHECK(!stranger.Take(11, 6, 5, t0));       // a non-clicker takes nothing
        CHECK(!stranger.Take(11, 0, 0, t0));       // nobody takes without a claim
        CHECK(!stranger.attempted);
    }
    std::cout << "  [PASS] two tries per object, eight seconds apart, clicker only\n";

    // Click gate: due, in range, still, and the window open. 25.1 yd, a NaN range and a moving boss all refuse.
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        CHECK(UseObjectClickReady(true, 25.0f, 25.0f, false, true));
        CHECK(UseObjectClickReady(true, 0.0f, 25.0f, false, true));
        CHECK(!UseObjectClickReady(true, 25.1f, 25.0f, false, true));
        CHECK(!UseObjectClickReady(true, nan, 25.0f, false, true));
        CHECK(!UseObjectClickReady(true, 10.0f, 25.0f, true, true));
        CHECK(!UseObjectClickReady(false, 10.0f, 25.0f, false, true));
        CHECK(!UseObjectClickReady(true, 10.0f, 25.0f, false, false));
    }
    std::cout << "  [PASS] the click needs a due row, a still boss in range, an open window\n";

    // Anchor: only the tank the boss is on walks, only inside the 15 s lead, only when too far to click.
    CHECK(TankAnchorWanted(true, 15000, 20.0f, 25.0f));
    CHECK(TankAnchorWanted(true, 0, 20.0f, 25.0f));
    CHECK(!TankAnchorWanted(true, 15001, 20.0f, 25.0f));   // outside the lead
    CHECK(!TankAnchorWanted(true, 15000, 12.5f, 25.0f));  // already in click range
    CHECK(!TankAnchorWanted(true, 15000, 12.4f, 25.0f));
    CHECK(!TankAnchorWanted(false, 0, 80.0f, 25.0f));     // not this tank's boss
    std::cout << "  [PASS] the anchor walks only on its tank's boss, inside the lead\n";

    // Anchor point: 0.4 * maxYd from the object towards the tank; the object itself when on it.
    {
        float x = 0.0f, y = 0.0f;
        TankAnchorPoint(0.0f, 0.0f, 30.0f, 40.0f, 10.0f, x, y);
        CHECK(std::fabs(x - 6.0f) < 0.001f && std::fabs(y - 8.0f) < 0.001f);
        CHECK(std::fabs(std::sqrt(x * x + y * y) - 10.0f) < 0.001f);
        TankAnchorPoint(5.0f, 5.0f, 5.0f, 5.0f, 10.0f, x, y);  // degenerate: no direction to walk
        CHECK(x == 5.0f && y == 5.0f);
    }
    std::cout << "  [PASS] the anchor stops short of the object on its own side\n";

    std::cout << "All use_object policy tests passed.\n";
    return 0;
}
