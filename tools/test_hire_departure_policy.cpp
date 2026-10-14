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

// The hire sweep judges a Bound hire only after the hirer's binding was
// settled. The lead logging off or leaving makes "the hire is not in the
// commanded group" true for the whole party for up to a second, before the
// commander's own sweep releases the binding: that must return the hires, never
// read as a departure that deletes them.
static void TestBoundSweepOrdering()
{
    // Lead gone / group changed, binding not yet released: the settle step
    // releases it, then no binding stands and the hires are returned.
    CHECK(DecideBoundHireSweep(false, true, false) == BoundHireVerdict::ReturnToHirer);
    CHECK(DecideBoundHireSweep(false, true, true) == BoundHireVerdict::ReturnToHirer);
    CHECK(DecideBoundHireSweep(false, false, false) == BoundHireVerdict::ReturnToHirer);
    // The binding stands and the hire is in its group, or not live: nothing.
    CHECK(DecideBoundHireSweep(true, true, true) == BoundHireVerdict::Keep);
    CHECK(DecideBoundHireSweep(true, false, false) == BoundHireVerdict::Keep);
    // The binding stands but a live hire is outside its group: ordinary rules.
    CHECK(DecideBoundHireSweep(true, true, false) == BoundHireVerdict::DropState);
    // Never DropState without a standing binding, whatever the hire's group.
    for (int live = 0; live <= 1; ++live)
        for (int inGroup = 0; inGroup <= 1; ++inGroup)
            CHECK(DecideBoundHireSweep(false, live, inGroup) != BoundHireVerdict::DropState);
    // A returned hire is never dismissed by the departure rule on the way: the
    // binding's end puts it in transition, which the rule leaves alone.
    CHECK(!ShouldDismissHireOnGroupDeparture(true, true, true, false, HireCommandState::Transition));
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
    CHECK(DecideHireBindingEnd(false, true, false, false) == HireBindingEnd::Leave); // no character at all
    CHECK(DecideHireBindingEnd(false, false, false, false) == HireBindingEnd::Leave);
    // After the unbind the hire is an ordinary one again: apart from an online
    // hirer it ends (the re-invite did not take), as before this feature.
    CHECK(ShouldDismissHireOnGroupDeparture(true, true, true, false, HireCommandState::None));
}

// A hire out of world when the binding ends (a far `cmd summon`, an instance
// entry) is a hire mid-teleport, not a lost one: it is held in transition and
// re-joined once back, never released to the ordinary rules (which would delete
// it for being outside its online hirer's party).
static void TestHireOutOfWorldAtBindingEnd()
{
    CHECK(DecideHireBindingEnd(false, true, false, true) == HireBindingEnd::AwaitWorld);
    CHECK(DecideHireBindingEnd(false, true, true, true) == HireBindingEnd::AwaitWorld);
    // No character object at all: nothing to hold.
    CHECK(DecideHireBindingEnd(false, true, false, false) == HireBindingEnd::Leave);
    // The hirer is offline: the logout grace owns it, in world or not.
    CHECK(DecideHireBindingEnd(false, false, false, true) == HireBindingEnd::ParkForLogin);
    // Never a plain release for an existing hire with an online hirer outside its party.
    for (int live = 0; live <= 1; ++live)
        for (int inGroup = 0; inGroup <= 1; ++inGroup)
            CHECK(DecideHireBindingEnd(live, true, inGroup, true) != HireBindingEnd::Leave);

    // The sweep: wait while out of world, re-join when back, park if the hirer
    // left meanwhile, and give up (ordinary rules) only past the bound.
    CHECK(DecideAwaitedRegroup(true, false, false) == AwaitedRegroup::Wait);
    CHECK(DecideAwaitedRegroup(true, true, false) == AwaitedRegroup::Retry);
    CHECK(DecideAwaitedRegroup(false, true, false) == AwaitedRegroup::Park);
    CHECK(DecideAwaitedRegroup(false, false, false) == AwaitedRegroup::Park);
    CHECK(DecideAwaitedRegroup(true, false, true) == AwaitedRegroup::GiveUp);
    CHECK(DecideAwaitedRegroup(true, true, true) == AwaitedRegroup::GiveUp);
    // While held (transition) the departure rule leaves the hire alone.
    CHECK(!ShouldDismissHireOnGroupDeparture(true, true, true, false, HireCommandState::Transition));
    // After the give-up the ordinary rule applies again, as before this feature.
    CHECK(ShouldDismissHireOnGroupDeparture(true, true, true, false, HireCommandState::None));
}

// zf2-hires-2: a re-invite that did not land keeps the hire held until the bound.
static void TestFailedReinviteIsRetried()
{
    CHECK(KeepAwaitingRegroup(false, false));
    CHECK(!KeepAwaitingRegroup(true, false));
    CHECK(!KeepAwaitingRegroup(false, true)); // bound passed: the ordinary rules
    CHECK(!KeepAwaitingRegroup(true, true));
}

// zf2-hires-2: a party move inside a dungeon carries the instance over.
static void TestPartyMoveKeepsInstance()
{
    // Outside a dungeon: nothing (no homebind timer there).
    CHECK(DecidePartyMoveInstance(false, false, false, false) == PartyMoveInstance::Nothing);
    // A fresh party (the bind's re-formed party, the hirer's new party): bound to the bot's instance.
    CHECK(DecidePartyMoveInstance(true, false, false, false) == PartyMoveInstance::BindParty);
    // The party already holds this instance: the bot is kept inside.
    CHECK(DecidePartyMoveInstance(true, false, true, true) == PartyMoveInstance::KeepValid);
    // The party holds another instance of the map: the core's homebind stands.
    CHECK(DecidePartyMoveInstance(true, false, true, false) == PartyMoveInstance::Nothing);
    // The instance is already waiting for its reset: never bound again.
    CHECK(DecidePartyMoveInstance(true, true, false, false) == PartyMoveInstance::Nothing);
    CHECK(DecidePartyMoveInstance(true, true, true, true) == PartyMoveInstance::Nothing);
    // The bot's solo bind to the same instance goes once the party holds it.
    CHECK(DropSoloBindAfterPartyMove(PartyMoveInstance::BindParty, true, false, true));
    CHECK(DropSoloBindAfterPartyMove(PartyMoveInstance::KeepValid, true, false, true));
    CHECK(!DropSoloBindAfterPartyMove(PartyMoveInstance::Nothing, true, false, true));
    CHECK(!DropSoloBindAfterPartyMove(PartyMoveInstance::KeepValid, true, true, true));  // permanent stays
    CHECK(!DropSoloBindAfterPartyMove(PartyMoveInstance::KeepValid, true, false, false)); // other instance
    CHECK(!DropSoloBindAfterPartyMove(PartyMoveInstance::KeepValid, false, false, false));
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
    TestBoundSweepOrdering();
    TestBindingEnd();
    TestHireOutOfWorldAtBindingEnd();
    TestFailedReinviteIsRetried();
    TestPartyMoveKeepsInstance();
    std::printf("hire departure policy: %d checks passed\n", checks);
    return 0;
}
