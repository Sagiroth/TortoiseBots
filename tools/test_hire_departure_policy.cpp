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

// Remote commander: a hire bound by its own hirer stays outside the hirer's
// party without ending, in every state but None.
static void TestBoundHireKeepsLiving()
{
    // The case that ends an ordinary hire: master online, once grouped, now apart.
    CHECK(ShouldDismissHireOnGroupDeparture(true, true, true, false, HireCommandState::None) == true);
    CHECK(ShouldDismissHireOnGroupDeparture(true, true, true, false, HireCommandState::Bound) == false);
    CHECK(ShouldDismissHireOnGroupDeparture(true, true, true, false, HireCommandState::Transition) == false);
    // Omitted state = None: the existing callers keep their behaviour.
    CHECK(ShouldDismissHireOnGroupDeparture(true, true, true, false) == true);
    // Every combination with a command state set: never dismissed by departure.
    for (int online = 0; online <= 1; ++online)
        for (int grouped = 0; grouped <= 1; ++grouped)
            for (int same = 0; same <= 1; ++same)
            {
                CHECK(!ShouldDismissHireOnGroupDeparture(true, online, grouped, same, HireCommandState::Bound));
                CHECK(!ShouldDismissHireOnGroupDeparture(true, online, grouped, same, HireCommandState::Transition));
            }
}

// What stays an end of hire while bound: a removal that no bind step made, and
// (through the binding's end) the hirer's logout grace; a disband is the
// binding's to return.
static void TestOtherEndsWhileBound()
{
    CHECK(ShouldDismissHireOnOwnRemoval(HireCommandState::None));
    CHECK(ShouldDismissHireOnOwnRemoval(HireCommandState::Bound)); // kicked from the bound party
    CHECK(!ShouldDismissHireOnOwnRemoval(HireCommandState::Transition)); // the bind/unbind step itself

    CHECK(ShouldDismissHireOnDisband(HireCommandState::None));
    CHECK(!ShouldDismissHireOnDisband(HireCommandState::Bound));
    CHECK(!ShouldDismissHireOnDisband(HireCommandState::Transition));
}

// A Bound state nothing backs any more is dropped, so the hire is not immune forever.
static void TestStaleBoundState()
{
    CHECK(CommandStateStillHolds(HireCommandState::Bound, true, true));
    CHECK(!CommandStateStillHolds(HireCommandState::Bound, true, false));
    CHECK(CommandStateStillHolds(HireCommandState::Bound, false, false)); // not live: left alone
    CHECK(!CommandStateStillHolds(HireCommandState::None, true, true));
    CHECK(CommandStateStillHolds(HireCommandState::Transition, true, false));
}

// Unbind: the hire goes back to its hirer's party when the hirer is online,
// waits for the hirer's return when it is not, and a hire that is not live is
// left to the other watchdogs.
static void TestBindingEnd()
{
    CHECK(DecideHireBindingEnd(true, true, false) == HireBindingEnd::RejoinHirer);
    CHECK(DecideHireBindingEnd(true, true, true) == HireBindingEnd::Keep);
    CHECK(DecideHireBindingEnd(true, false, false) == HireBindingEnd::ParkForLogin);
    CHECK(DecideHireBindingEnd(true, false, true) == HireBindingEnd::ParkForLogin);
    CHECK(DecideHireBindingEnd(false, true, false) == HireBindingEnd::Leave);
    CHECK(DecideHireBindingEnd(false, false, false) == HireBindingEnd::Leave);
    // After the unbind the hire is an ordinary one again: apart from an online
    // hirer it ends (the re-invite did not take), as before this feature.
    CHECK(ShouldDismissHireOnGroupDeparture(true, true, true, false, HireCommandState::None));
}

int main()
{
    TestMasterLeftWhileOnline();
    TestStillGrouped();
    TestMasterOffline();
    TestNotYetGrouped();
    TestNotAHire();
    TestAllCombinations();
    TestBoundHireKeepsLiving();
    TestOtherEndsWhileBound();
    TestStaleBoundState();
    TestBindingEnd();
    std::printf("hire departure policy: %d checks passed\n", checks);
    return 0;
}
