// Standalone regression test for the hired-companion master-departure rule
// (issue #378): a hire is dismissed when its master leaves the party, and
// NOTHING else is. Guards the rules that keep pool bots, alts, hires still
// being provisioned and logged-out masters (grace period) untouched.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_hire_departure_policy.cpp -o /tmp/test_hire_departure
//   /tmp/test_hire_departure

#include "../runtime/HireDeparturePolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

// The reported bug: a hire that was grouped, whose master is still online but
// is no longer in its group (the master left or was kicked from a party larger
// than two, so the group did not disband).
static void TestMasterLeftWhileOnline()
{
    CHECK(ShouldDismissHireOnGroupDeparture(true, true, true, false) == true);
}

// Still together: nothing to do.
static void TestStillGrouped()
{
    CHECK(ShouldDismissHireOnGroupDeparture(true, true, true, true) == false);
}

// Logout (or crash) of the master: the disconnect grace period owns the hire.
static void TestMasterOffline()
{
    CHECK(ShouldDismissHireOnGroupDeparture(true, false, true, false) == false);
    CHECK(ShouldDismissHireOnGroupDeparture(true, false, true, true) == false);
}

// A just-provisioned hire: Claim() runs before the invitation is accepted, so
// it must survive any group state until it was seen grouped at least once.
static void TestNotYetGrouped()
{
    CHECK(ShouldDismissHireOnGroupDeparture(true, true, false, false) == false);
    CHECK(ShouldDismissHireOnGroupDeparture(true, true, false, true) == false);
    CHECK(ShouldDismissHireOnGroupDeparture(true, false, false, false) == false);
}

// A pool bot or a player's alt is not a hire: never dismissed, whatever the
// other facts say.
static void TestNotAHire()
{
    CHECK(ShouldDismissHireOnGroupDeparture(false, true, true, false) == false);
    CHECK(ShouldDismissHireOnGroupDeparture(false, true, true, true) == false);
    CHECK(ShouldDismissHireOnGroupDeparture(false, false, true, false) == false);
    CHECK(ShouldDismissHireOnGroupDeparture(false, true, false, false) == false);
}

// Every combination, to pin the precedence: only (hire, online, grouped,
// not-same-group) dismisses.
static void TestAllCombinations()
{
    for (int isHire = 0; isHire <= 1; ++isHire)
        for (int online = 0; online <= 1; ++online)
            for (int grouped = 0; grouped <= 1; ++grouped)
                for (int same = 0; same <= 1; ++same)
                {
                    bool expected = isHire && online && grouped && !same;
                    CHECK(ShouldDismissHireOnGroupDeparture(isHire, online, grouped, same) == expected);
                }
}

int main()
{
    TestMasterLeftWhileOnline();
    TestStillGrouped();
    TestMasterOffline();
    TestNotYetGrouped();
    TestNotAHire();
    TestAllCombinations();
    std::printf("hire departure policy: %d checks passed\n", checks);
    return 0;
}
