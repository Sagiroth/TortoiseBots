// Standalone check for ai/playerbot/SeekAuraPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_seek_aura_policy.cpp -o /tmp/test_seek_aura_policy
//   /tmp/test_seek_aura_policy
#include "../ai/playerbot/SeekAuraPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    std::cout << "Starting TortoiseBots seek-aura policy tests...\n";

    // A cloud is the row's when the dynamic object's spell is the aura itself, or when the aura is the trigger of the
    // object's effect. The Son of Hakkar's death object is 24320 and its effect 27 triggers 24321, so matching the
    // dynamic object's id alone would miss the (24320, 24321) pair and matching the trigger alone would miss a plain
    // aura cloud such as 24321 cast on the ground.
    CHECK(IsSoughtCloud(24320, 24321, 24321));
    CHECK(!IsSoughtCloud(24320, 0, 24321));
    CHECK(IsSoughtCloud(24321, 0, 24321));
    CHECK(!IsSoughtCloud(24320, 24322, 24321));

    // The dose window is [due - lead, due - 3 s]: the first 10 s of the cycle are the previous siphon's tail and the
    // last 3 s are too late for the dose to be on at the siphon.
    {
        const std::uint32_t due = 90000;
        CHECK(!SeekWindowOpen(9999, due, 80000));
        CHECK(SeekWindowOpen(10000, due, 80000));
        CHECK(SeekWindowOpen(87000, due, 80000));
        CHECK(!SeekWindowOpen(87001, due, 80000));
    }

    // SeekWindowStaysOpenWhenOverdue: the raw window does not shut at due - 3 s when the 24324 is not seen (walk 40
    // missed two, leaving 184-185 s gaps); it is open past the due time, and the caller's overdue read
    // (SeekRolledDueMs) decides how long. The last three seconds before due stay shut.
    {
        const std::uint32_t due = 90000;
        CHECK(!SeekWindowOpen(87001, due, 80000));
        CHECK(SeekWindowOpen(90000, due, 80000));   // due with no sighting: open
        CHECK(SeekWindowOpen(184000, due, 80000));  // still open on the raw due: the caller rolls it
        CHECK(SeekForceBurn(184000, due));
        // The sighting moves the due time a cycle ahead: the window shuts and waits for the next one.
        CHECK(!SeekWindowOpen(184000, 274000, 80000));
        CHECK(SeekWindowOpen(194000, 274000, 80000));
        CHECK(!SeekWindowOpen(1000, 0, 80000));     // no due time yet
    }

    // SeekWindowShutsAfterTheOverdueGrace: a due time nobody saw the siphon for is stale, and the window must not stay
    // open for the rest of the fight (the quiet group stood at the spot and the Son was held there). Hakkar's row is a
    // 90 s cycle with an 80 s window, so the grace has to stay under the 10 s between the roll and the rolled window:
    // the window stays open up to due + 8 s, the estimate then rolls one cycle (due += 90 s) and the window is shut
    // from due + 8 s until due_new - 80 s = due + 10 s, where the rolled cycle's own window opens.
    {
        const std::uint32_t due = 90000;
        const std::uint32_t cycle = 90000;
        const std::uint32_t lead = 80000;
        CHECK(kSeekOverdueGraceMs == 8000);
        CHECK(SeekRolledDueMs(due + 7999, due, cycle) == due);   // still the overdue grace
        CHECK(SeekRolledDueMs(due + 8000, due, cycle) == due);   // due + 8 s, inclusive
        CHECK(SeekRolledDueMs(due + 8001, due, cycle) == due + cycle); // rolled one cycle (due += 90 s)
        const std::uint32_t rolled = SeekRolledDueMs(due + 8001, due, cycle);
        CHECK(rolled != due);                                   // not the stale due
        CHECK(!SeekWindowOpen(due + 8001, rolled, lead));       // shut right after the roll
        CHECK(!SeekWindowOpen(rolled - lead - 1, rolled, lead)); // due + 9.999 s: still shut
        CHECK(SeekWindowOpen(rolled - lead, rolled, lead));     // due + 10 s: the rolled window opens
        CHECK(!SeekForceBurn(due + 8001, rolled));              // and the burn waits for it
        CHECK(SeekForceBurn(rolled - 15000, rolled));
        // A clock that is never sighted keeps rolling: a 94 s overdue gap is due + one cycle, a 184 s one two cycles.
        CHECK(SeekRolledDueMs(184000, due, 90000) == 180000);
        CHECK(SeekRolledDueMs(184000, due, 90000) == due + 90000);
        CHECK(SeekRolledDueMs(274000, due, 90000) == 270000);
        // No due time (no combat start yet) stays 0; a zero cycle cannot roll.
        CHECK(SeekRolledDueMs(184000, 0, 90000) == 0);
        CHECK(SeekRolledDueMs(184000, due, 0) == due);
    }

    // SeekFirstSiphonIsCountedFromThePull: before any siphon is seen the due time is the fight's combat start plus one
    // cycle, since the core's 90 s timer only runs in combat, so cycle 1 gets the same 80 s window and 15 s forced
    // burn as every later cycle. An observed siphon resyncs the cadence to itself, and a fight with no combat start
    // yet has no due time.
    {
        const std::uint32_t start = 1000;
        const std::uint32_t first = SeekCycleDueMs(start, 0, 90000);
        CHECK(first == 91000);
        CHECK(!SeekWindowOpen(10999, first, 80000)); // opens at start + 10 s
        CHECK(SeekWindowOpen(11000, first, 80000));
        CHECK(SeekWindowOpen(88000, first, 80000));
        CHECK(!SeekWindowOpen(88001, first, 80000));
        CHECK(!SeekForceBurn(75999, first)); // forced 15 s before due
        CHECK(SeekForceBurn(76000, first));
        CHECK(SeekCycleDueMs(start, 92000, 90000) == 182000);
        CHECK(SeekCycleDueMs(0, 0, 90000) == 0);
    }

    // A sighting starts a new cycle only half a cycle after the last accepted one: every bot sees the same siphon
    // within a few hundred ms, so the second sighting must not push the due time again.
    {
        CHECK(SeekCycleAccepts(0, 91000, 90000));
        CHECK(SeekCycleDueMs(1000, 91000, 90000) == 181000);
        CHECK(!SeekCycleAccepts(91000, 95000, 90000));
        CHECK(SeekCycleDueMs(1000, 91000, 90000) == 181000);
        CHECK(SeekCycleAccepts(91000, 136000, 90000));
        CHECK(SeekCycleDueMs(1000, 136000, 90000) == 226000);
    }

    // The walk aims kSeekCloudInsetYd inside the cloud's edge and pays the decision latency first: at 14 yd from an
    // 8 yd cloud the bot has 7.5 yd to run, so 1.5 s (10.5 yd of budget minus 0.5 s latency) is not enough and 2 s is.
    CHECK(!SeekCloudReachable(14.0f, 8.0f, 1500));
    CHECK(SeekCloudReachable(14.0f, 8.0f, 2000));
    CHECK(SeekCloudReachable(6.0f, 8.0f, 500));
    CHECK(SeekCloudReachable(6.0f, 8.0f, 400));  // already inside: no run left to pay for
    CHECK(SeekCloudReachable(9.0f, 8.0f, 1000));

    // The gather is ready when nobody is missing, when it has waited kSeekGatherMaxMs, or when the burn is forced.
    CHECK(!SeekGatherReady(2, 0, 7999, false));
    CHECK(SeekGatherReady(2, 0, 8000, false));
    CHECK(SeekGatherReady(0, 0, 0, false));
    CHECK(SeekGatherReady(2, 0, 0, true));

    // SeekOnlyThePickedSourceIsDamageable: the low-health exception belongs to the one picked source. With two Sons
    // at or below the hold percent only the selected one may be damaged; every other Son stays held wherever it
    // stands, so a second low Son is not spent beside the pick.
    CHECK(!SeekHoldDecision(true, true, true, false, 20.0f, 30.0f, 5.0f, 5));
    CHECK(SeekHoldDecision(false, true, true, false, 20.0f, 30.0f, 5.0f, 5));

    // SeekPickedSourceStillHoldsAtPercent: inside the window the picked source still stops at the hold percent until
    // the gather is ready (the old bypass finished a 14 % Son before the raid stood around it).
    CHECK(SeekHoldDecision(true, true, false, false, 14.0f, 30.0f, 5.0f, 5));
    CHECK(!SeekHoldDecision(true, true, true, false, 14.0f, 30.0f, 5.0f, 5));
    CHECK(SeekHoldDecision(true, true, false, false, 29.0f, 30.0f, 5.0f, 5)); // 29 <= 30: still the hold
    CHECK(!SeekHoldDecision(true, true, false, false, 31.0f, 30.0f, 5.0f, 5)); // above the hold: burn it down

    // SeekKillPickOnlyInsideTheWindow: with the window shut (due - 3 s through the siphon and its tail) the picked
    // Son is held at whatever health it has, so it serves the next siphon; a 12 % Son is not a kill pick.
    CHECK(SeekHoldDecision(true, false, true, false, 12.0f, 30.0f, 5.0f, 5));
    CHECK(SeekHoldDecision(true, false, false, true, 12.0f, 30.0f, 5.0f, 5));

    // SeekForcedBurnReleasesTheHold: the forced burn (the window is about to close) releases the picked source even
    // before the gather is ready, and the gather row's readiness release is the same rule.
    CHECK(!SeekHoldDecision(true, true, false, true, 14.0f, 30.0f, 5.0f, 5));
    CHECK(!SeekHoldDecision(true, true, true, false, 80.0f, 30.0f, 5.0f, 5));

    // SeekLateEngagedSonForcesBurn: a picked Son that only joins the fight with the siphon due inside 30 s is burnt at
    // once, not at the forced burn's own 15 s, so its cloud still lands in time (walk 41 left three such Sons at 67-90 %
    // seconds before the siphon). A Son that is not fighting, a due time further out, and a fight with no due time do
    // not force it; an overdue pick is late by definition.
    CHECK(kSeekLateEngageMs == 30000);
    CHECK(SeekLateEngagedBurn(true, 91000, 25000));
    CHECK(SeekLateEngagedBurn(true, 91000, 30000));
    CHECK(!SeekLateEngagedBurn(true, 91000, 30001));
    CHECK(!SeekLateEngagedBurn(false, 91000, 25000));
    CHECK(!SeekLateEngagedBurn(true, 0, 0));
    CHECK(SeekLateEngagedBurn(true, 91000, 0));

    // seek_pull's kill lead (walk 51: back -> death median 27 s, p75 31 s): the same rule with the row's own lead.
    CHECK(SeekLateEngagedBurnWithin(true, 91000, 44000, 45000));
    CHECK(!SeekLateEngagedBurnWithin(true, 91000, 46000, 45000));
    CHECK(!SeekLateEngagedBurnWithin(false, 91000, 44000, 45000));
    CHECK(!SeekLateEngagedBurnWithin(true, 0, 0, 45000));
    // The old 30 s rule is the same function at its own lead.
    for (std::uint32_t dueIn : {0u, 25000u, 30000u, 30001u})
        CHECK(SeekLateEngagedBurn(true, 91000, dueIn) == SeekLateEngagedBurnWithin(true, 91000, dueIn, 30000));

    // SeekTakeFightingSonOnHealer: a Son that has sat on a healer (or any other non-tank) for 3 s is the first
    // off-tank's own take, whatever the third off-tank does: the third off-tank had those 3 s (walk 45: with three
    // tanks alive the old gate never let the first off-tank take one, and 87 of 99 deaths from Sons were non-tanks).
    CHECK(kSeekHealerTakeMs == 3000);
    CHECK(SeekTakeFightingSonOnHealer(3000));
    CHECK(!SeekTakeFightingSonOnHealer(2999));
    // An off-tank that is Hakkar's victim (Cause Insanity, then the threat successor) takes no Son and walks no drag:
    // it would bring Hakkar onto the gather. The other off-tank takes it.
    CHECK(SeekTankTakeAllowed(false));
    CHECK(!SeekTankTakeAllowed(true));

    // Who pulls an unengaged Son: exactly one actor. The third off-tank while it lives and is not the boss's victim;
    // with it the victim (the guard above stands its whole task down), the ranged fallback takes the pull that cycle.
    {
        const SeekPullActors normal = SeekPullActorsFor(3, false, false);
        CHECK(normal.thirdTank && !normal.fallback);
        const SeekPullActors victim = SeekPullActorsFor(3, true, false);
        CHECK(!victim.thirdTank && victim.fallback);
        const SeekPullActors two = SeekPullActorsFor(2, false, false);
        CHECK(!two.thirdTank && two.fallback);
        // The anchored shot opens the fallback beside an idle third off-tank (the tanks have no Son to walk to).
        const SeekPullActors anchored = SeekPullActorsFor(3, false, true);
        CHECK(!anchored.thirdTank && anchored.fallback);
        const SeekPullActors none = SeekPullActorsFor(1, false, false);
        CHECK(!none.thirdTank && !none.fallback);
    }

    // SeekHoldDecision with the Son's victim: a Son on a non-tank for 5 s is no longer held, the damage dealers kill it
    // (the cycle's cloud is lost, which beats losing the raid). Below that, and on a tank (0), the hold is today's.
    CHECK(kSeekNonTankBurnMs == 5000);
    CHECK(SeekHoldDecision(true, true, false, false, 20.0f, 30.0f, 5.0f, 5, 4999));
    CHECK(!SeekHoldDecision(true, true, false, false, 20.0f, 30.0f, 5.0f, 5, 5000));
    // Not the pick, the window shut, far from the spot, the raid dosed: released all the same.
    CHECK(!SeekHoldDecision(false, false, false, false, 98.0f, 30.0f, 40.0f, 0, 5000));
    CHECK(SeekHoldDecision(false, false, false, false, 98.0f, 30.0f, 40.0f, 0, 0));
    // On a tank (no non-tank time): held exactly as before.
    CHECK(SeekHoldDecision(true, true, false, false, 20.0f, 30.0f, 5.0f, 5, 0) ==
          SeekHoldDecision(true, true, false, false, 20.0f, 30.0f, 5.0f, 5));

    // SeekHoldReleasesAfter20s: a Son the hold keeps longer than 20 s is released to the damage dealers, whatever the
    // window and the gather say. Walk 48: the held Sons lasted a median 26.5 s (up to 126 s) and 12 of the 23 losses
    // were Son wipes.
    CHECK(kSeekHoldMaxMs == 20000);
    CHECK(SeekHoldDecision(true, true, false, false, 20.0f, 30.0f, 5.0f, 5, 0, 19999));
    CHECK(!SeekHoldDecision(true, true, false, false, 20.0f, 30.0f, 5.0f, 5, 0, 20000));
    // Not the pick (otherwise held all fight), the window shut and the gather still running: released too.
    CHECK(!SeekHoldDecision(false, true, true, false, 80.0f, 30.0f, 5.0f, 5, 0, 20000));
    CHECK(!SeekHoldDecision(true, false, false, false, 20.0f, 30.0f, 5.0f, 5, 0, 20000));

    // SeekHoldWhyOf: the hold rule's one body answers why, and SeekHoldDecision reads held-ness through it. The cap
    // wins over the healer's burn and every other reason; the dosed path (no seeker left) still holds the Son, since a
    // kill buys nothing then.
    CHECK(SeekHoldWhyOf(true, true, false, false, 20.0f, 30.0f, 5.0f, 5, 0, 20000) == SeekHoldWhy::Cap20);
    CHECK(SeekHoldWhyOf(true, true, false, false, 20.0f, 30.0f, 5.0f, 5, 5000) == SeekHoldWhy::NonTank);
    CHECK(SeekHoldWhyOf(true, true, true, false, 14.0f, 30.0f, 5.0f, 5) == SeekHoldWhy::SelectedBurn);
    CHECK(SeekHoldWhyOf(false, true, true, false, 20.0f, 30.0f, 5.0f, 5) == SeekHoldWhy::Held);
    CHECK(SeekHoldWhyOf(true, true, true, false, 20.0f, 30.0f, 5.0f, 0) == SeekHoldWhy::Dosed);
    CHECK(SeekHoldWhyHolds(SeekHoldWhy::Held) && SeekHoldWhyHolds(SeekHoldWhy::Dosed));
    CHECK(!SeekHoldWhyHolds(SeekHoldWhy::Cap20) && !SeekHoldWhyHolds(SeekHoldWhy::NonTank) &&
          !SeekHoldWhyHolds(SeekHoldWhy::SelectedBurn));
    // The decision and its reason cannot drift: over the decision's cases (the dosed one aside) SeekHoldDecision is
    // exactly (SeekHoldWhyOf == Held).
    {
        const auto agrees = [](bool selected, bool windowOpen, bool gatherReady, bool forceBurn, float hpPct,
                               float holdPct, float dist, int seekers, std::uint32_t nonTankMs = 0,
                               std::uint32_t heldMs = 0)
        {
            return SeekHoldDecision(selected, windowOpen, gatherReady, forceBurn, hpPct, holdPct, dist, seekers,
                                    nonTankMs, heldMs) ==
                (SeekHoldWhyOf(selected, windowOpen, gatherReady, forceBurn, hpPct, holdPct, dist, seekers, nonTankMs,
                               heldMs) == SeekHoldWhy::Held);
        };
        CHECK(agrees(true, true, true, false, 20.0f, 30.0f, 5.0f, 5));    // held at the percent
        CHECK(agrees(true, true, false, false, 14.0f, 30.0f, 5.0f, 5));   // held until the gather is ready
        CHECK(agrees(true, true, true, false, 14.0f, 30.0f, 5.0f, 5));    // the burn
        CHECK(agrees(false, true, true, false, 20.0f, 30.0f, 5.0f, 5));   // not the pick
        CHECK(agrees(true, false, false, false, 20.0f, 30.0f, 5.0f, 5));  // the window shut
        CHECK(agrees(true, true, false, false, 20.0f, 30.0f, 40.0f, 5));  // out of reach
        CHECK(agrees(true, true, false, false, 20.0f, 30.0f, 5.0f, 5, 4999)); // below the non-tank burn
        CHECK(agrees(true, true, false, false, 20.0f, 30.0f, 5.0f, 5, 5000)); // the non-tank burn
        CHECK(agrees(true, true, false, false, 20.0f, 30.0f, 5.0f, 5, 0, 19999)); // below the cap
        CHECK(agrees(true, true, false, false, 20.0f, 30.0f, 5.0f, 5, 0, 20000)); // the cap
    }

    // SeekSonTicks: what one read feeds into the shared hold clock. Only a Son this read saw alive, that the clock
    // counts and that the hold rule still holds is fed; a read of a Son seen dead erases its record, and a
    // non-ticking read of a live Son changes nothing (another bot may be feeding it). An idle spare or a Son the rule
    // already releases never gets a record, so the 20 s cap cannot empty the reservoir the area veto protects.
    CHECK(!SeekSonTicks(true, false, true));   // idle spare, held by the rule: no clock
    CHECK(SeekSonTicks(true, true, true));     // engaged and held: fed
    CHECK(!SeekSonTicks(true, true, false));   // engaged and released by the rule: not fed
    CHECK(!SeekSonTicks(false, true, true));   // dead or gone: erased, never fed

    // Who seeks: every living bot without the aura, except the boss's victim, the two exempt tanks and a charmed bot.
    CHECK(!SeekWanted(true, true, false, true, false, false, true));
    CHECK(!SeekWanted(true, true, false, false, true, false, true));
    CHECK(!SeekWanted(true, true, false, false, false, true, true));
    CHECK(!SeekWanted(true, true, true, false, false, false, true));
    CHECK(!SeekWanted(true, true, false, false, false, false, false));
    CHECK(!SeekWanted(false, true, false, false, false, false, true));
    CHECK(!SeekWanted(true, false, false, false, false, false, true));
    CHECK(SeekWanted(true, true, false, false, false, false, true));

    // The second off-tank pulls a source only inside the window, while enough seekers still miss the aura and a
    // source stands within kSeekPickupYd of the spot; a source already in the fight is picked up, not pulled.
    CHECK(SeekTankPullDue(true, false, 3, 59.0f));
    CHECK(!SeekTankPullDue(true, false, 2, 59.0f));
    CHECK(!SeekTankPullDue(true, false, 3, 61.0f));
    CHECK(!SeekTankPullDue(true, true, 3, 59.0f));
    CHECK(!SeekTankPullDue(false, false, 3, 59.0f));

    // SeekTankPullWalksToOutOfCombatSon: a Son 30 yd out and not fighting is walked to (the tank moves itself), only a
    // Son at melee reach is hit, and once the Son fights or follows the tank the take/drag branch owns it. The old
    // branch set the target and gave the tick back on every read, so the tank never moved (walk 41: 27-54 yd between
    // tank and Son in every undosed window).
    CHECK(SeekTankPullStep(false, false, 30.0f) == SeekPullStep::Walk);
    CHECK(SeekTankPullStep(false, false, kSeekPullLatchYd) == SeekPullStep::Walk);
    CHECK(SeekTankPullStep(false, false, 4.0f) == SeekPullStep::Hit);
    CHECK(SeekTankPullStep(false, false, kSeekPullHitYd) == SeekPullStep::Hit);
    CHECK(SeekTankPullStep(true, false, 30.0f) == SeekPullStep::Done);
    CHECK(SeekTankPullStep(false, true, 30.0f) == SeekPullStep::Done);
    CHECK(SeekTankPullStep(true, true, 0.0f) == SeekPullStep::Done);

    // SeekPullLatchKeepsLeavingSon: a latched pull keeps after its Son while the Son is within 90 yd of B and the latch
    // is younger than 20 s — the pickup radius (60 yd) alone dropped a Son that walked out mid-pull, which is why the
    // tank never arrived (walk 41: the Son patrols 32 -> 112 yd from B).
    CHECK(kSeekPullLatchYd == 90.0f);
    CHECK(kSeekPullLatchMs == 20000);
    CHECK(SeekPullLatchKeeps(true, 75.0f, 8000));
    CHECK(SeekPullLatchKeeps(true, 30.0f, 0));
    CHECK(!SeekPullLatchKeeps(false, 30.0f, 8000));
    CHECK(!SeekPullLatchKeeps(true, 90.1f, 8000));
    CHECK(!SeekPullLatchKeeps(true, 30.0f, 20001));
    // The pickup rule still refuses a fresh pull past its own radius; the latch is what keeps a running one.
    CHECK(!SeekTankPullDue(true, false, 3, 75.0f));

    // SeekPullHitExemptOnLatchedTarget: the pulling tank's own hit on the Son its pull latched is exempt from the seek
    // hold — before it fights and after, or the hold would stand the swing down and the Son would never come to the
    // spot. Only that Son: a puller's hit on another Son stays held, so the spare source is not damaged, and a bot
    // that is not the puller never exempts.
    CHECK(SeekPullHitExemptOnLatch(true, true));
    CHECK(!SeekPullHitExemptOnLatch(true, false));
    CHECK(!SeekPullHitExemptOnLatch(false, true));
    CHECK(!SeekPullHitExemptOnLatch(false, false));
    // The hold itself still answers true for a Son that is not the pick: the exemption is the caller's, on the puller's
    // own strike against the Son its latch names.
    CHECK(SeekHoldDecision(false, true, false, false, 100.0f, 30.0f, 30.0f, 5));

    // SeekPullFallsBackToHunter: with fewer than three tanks alive the third off-tank's pull is a hunter's shot, then
    // a ranged damage dealer's; nobody pulls without an off-tank (tanks[1]) to take the Son over.
    CHECK(SeekPullRoleOf(true, true, true, true) == SeekPullRole::ThirdTank);
    CHECK(SeekPullRoleOf(false, true, true, true) == SeekPullRole::Hunter);
    CHECK(SeekPullRoleOf(false, true, false, true) == SeekPullRole::Ranged);
    CHECK(SeekPullRoleOf(false, true, false, false) == SeekPullRole::None);
    CHECK(SeekPullRoleOf(false, false, true, true) == SeekPullRole::None);

    // SeekFallbackPullerIsTwoTankOnly: the fallback shot belongs to exactly two living tanks — with three the third
    // off-tank walks the Son down itself, and with fewer than two nobody is left at the spot to take the Son over.
    CHECK(SeekFallbackPullBranch(2));
    CHECK(!SeekFallbackPullBranch(0));
    CHECK(!SeekFallbackPullBranch(1));
    CHECK(!SeekFallbackPullBranch(3));
    CHECK(!SeekFallbackPullBranch(4));

    // SeekFallbackPullerSkips: a charmed member is hostile and its AI is gone, the boss's current victim cannot turn
    // and shoot (and the pull would take the boss's target out of the tank's face), a dead one is gone, and the three
    // tanks the seek leaves out are never the fallback. Only the clean ranged member pulls.
    CHECK(SeekFallbackCandidate(true, false, false, false));   // a clean candidate
    CHECK(!SeekFallbackCandidate(false, false, false, false));  // dead
    CHECK(!SeekFallbackCandidate(true, true, false, false));    // charmed
    CHECK(!SeekFallbackCandidate(true, false, true, false));    // the boss's victim
    CHECK(!SeekFallbackCandidate(true, false, false, true));    // an exempt tank
    CHECK(!SeekFallbackCandidate(true, true, true, true));      // every skip at once

    // SeekFallbackPullsOnlyNearTheSpot: the fallback's shot reaches a Son within 35 yd of the spot (the third
    // off-tank's own pull still walks to one within 60), so a Son just inside the old 60 yd reach is not pulled; the
    // rule also needs the open window, no Son in the fight yet, and enough seekers still without the poison.
    CHECK(SeekFallbackPullDue(true, false, 3, 34.9f));
    CHECK(SeekFallbackPullDue(true, false, 3, kSeekFallbackYd));
    CHECK(!SeekFallbackPullDue(true, false, 3, kSeekFallbackYd + 0.1f));
    CHECK(!SeekFallbackPullDue(true, false, 3, 59.0f));
    CHECK(!SeekFallbackPullDue(true, false, 2, 30.0f));
    CHECK(!SeekFallbackPullDue(true, true, 3, 30.0f));
    CHECK(!SeekFallbackPullDue(false, false, 3, 30.0f));
    // The third off-tank's own pull keeps its 60 yd walk-in.
    CHECK(SeekTankPullDue(true, false, 3, 59.0f));
    CHECK(!SeekTankPullDue(true, false, 3, 61.0f));

    // SeekAreaHoldsASonBesideThePick: an area action's area is judged Son by Son, so the selected Son may be burnable
    // while a second, held Son standing beside it is not: the cast reaches both, so it is vetoed. When the area reaches
    // only the non-selected (held) Son the cast is vetoed too, and it stands when nothing inside it is held.
    {
        CHECK(SeekAreaVeto({{12.0f, false}, {8.0f, true}}, 10.0f));  // only the held Son is in the area
        CHECK(SeekAreaVeto({{6.0f, false}, {8.0f, true}}, 10.0f));   // the pick is in it too
        CHECK(!SeekAreaVeto({{6.0f, false}, {8.0f, true}}, 7.0f));   // the held Son is outside the radius
        CHECK(!SeekAreaVeto({{6.0f, false}, {12.0f, true}}, 10.0f)); // a held Son beyond the cast
        CHECK(!SeekAreaVeto({{0.0f, false}}, 10.0f));
        CHECK(!SeekAreaVeto({}, 10.0f));
    }

    // SeekDoTsAreNeverCastOnASon: a plain DoT on the boss is fine, but one that would land on any Son of the seek fight
    // is held: its ticks would run through the hold percent and the shut window. A spell that also lands direct damage
    // is a burst the rotation can stop, so its periodic tail is held only while that Son is held; a channelled
    // periodic cast (Mind Flay, Drain Life) cannot be stopped once it runs and stays held always.
    CHECK(SeekDotVeto(false, true, false, false));  // Corruption, Serpent Sting: no direct part to stop
    CHECK(SeekDotVeto(false, true, false, true));
    CHECK(SeekDotVeto(true, true, false, true));    // Fireball, Immolate on a held Son
    CHECK(!SeekDotVeto(true, true, false, false));  // ... and on the Son the hold lets burn
    CHECK(SeekDotVeto(true, true, true, false));    // Mind Flay, Drain Life: channelled ticks
    CHECK(SeekDotVeto(false, true, true, true));
    CHECK(!SeekDotVeto(true, false, false, false)); // a plain nuke
    CHECK(!SeekDotVeto(false, false, false, true));
    CHECK(!SeekDotVeto(true, false, true, false));  // a channel with no damage tick (Arcane Missiles)

    // SeekOffTankStopsSwingingATiredSon: with the window shut the holding off-tank stops auto-attacking a Son already
    // at or below the hold percent plus 10 (a 30 % hold: 40 % and below); inside the open window it swings as usual,
    // and a healthier Son stays with the rotation.
    CHECK(SeekTankSwingHeld(false, 40.0f, 30.0f));
    CHECK(SeekTankSwingHeld(false, 12.0f, 30.0f));
    CHECK(!SeekTankSwingHeld(false, 41.0f, 30.0f));
    CHECK(!SeekTankSwingHeld(true, 12.0f, 30.0f));

    // SeekOffTankHoldLeavesTheTickToTheRotation: with no swing on the held Son there is nothing to stop, and the tank
    // action must not take the tick: Shield Block, the shouts and the taunt only ran on the ticks it left (one
    // successful action a tick). It takes the tick only to stop the swing that is running on the Son.
    CHECK(SeekTankHoldConsumesTick(true));
    CHECK(!SeekTankHoldConsumesTick(false));

    // At the gather the seek action stops a walk once, then leaves the tick to the rotation, the heals and the burn:
    // a seek that kept consuming every tick at the spot froze the seekers (H1).
    CHECK(SeekActionConsumesTick(false, false)); // still walking
    CHECK(SeekActionConsumesTick(false, true));
    CHECK(SeekActionConsumesTick(true, true));   // the one stop
    CHECK(!SeekActionConsumesTick(true, false)); // standing at the spot: the engine's turn

    // The gather keeps its source while that unit stands at the spot; with two there the pick is deterministic (the
    // lowest health %, then the lowest guid), so no bot flips the shared latch (M2).
    {
        const std::vector<SeekSourceChoice> two = {{0x2000, 30.0f}, {0x1000, 30.0f}};
        CHECK(SeekKeepOrPickGatherSource(0, two) == 0x1000);
        CHECK(SeekKeepOrPickGatherSource(0x2000, two) == 0x2000);
        const std::vector<SeekSourceChoice> closer = {{0x2000, 28.0f}, {0x1000, 40.0f}};
        CHECK(SeekKeepOrPickGatherSource(0, closer) == 0x2000);
        CHECK(SeekKeepOrPickGatherSource(0x3000, closer) == 0x2000); // the old one left the spot
        CHECK(SeekKeepOrPickGatherSource(0, {}) == 0);
    }

    // SeekLatchSurvivesDamageSpread: two Sons near the spot, the raid's damage spreads between them, and the pick is
    // the latched one for the whole window — also through a forced burn or once the search shrinks back (the caller
    // checks the latch against the 35 yd reach, not the current radius). It repicks only when the Son is gone or out
    // of reach, and then the lowest health % (then lowest guid) wins.
    {
        const std::vector<SeekSourceChoice> spread = {{0x2000, 22.0f}, {0x1000, 18.0f}};
        CHECK(SeekLatchedGatherSource(true, 0x2000, spread) == 0x2000);
        const std::vector<SeekSourceChoice> flipped = {{0x2000, 12.0f}, {0x1000, 25.0f}};
        CHECK(SeekLatchedGatherSource(true, 0x2000, flipped) == 0x2000);
        const std::vector<SeekSourceChoice> dragged = {{0x2000, 40.0f}, {0x1000, 25.0f}};
        CHECK(SeekLatchedGatherSource(false, 0x2000, dragged) == 0x1000);
        CHECK(SeekLatchedGatherSource(true, 0x2000, dragged) == 0x2000);
        CHECK(SeekLatchedGatherSource(true, 0x2000, {}) == 0x2000); // alive in reach, outside the search radius
        CHECK(SeekLatchedGatherSource(false, 0x2000, {}) == 0);     // gone with nothing to repick
        CHECK(SeekLatchedGatherSource(false, 0, spread) == 0x1000); // no latch yet: the lowest health %
    }

    // Without the third tank the gather goes to the source wherever it stands within 35 yd of the spot (M3).
    CHECK(SeekSourceSearchYd(true, false, false, false) == kSeekSpotYd);
    CHECK(SeekSourceSearchYd(false, false, false, false) == kSeekFallbackYd);
    CHECK(kSeekFallbackYd == 35.0f);

    // SeekInCombatOffSpotSonIsPicked: with three tanks a Son already fighting 11-25 yd from the spot (on the healers)
    // used to stay outside the spot's 8 yd search and was never picked; an engaged Son inside the 35 yd reach now
    // widens the search, so the gather and the tank's drag take it.
    CHECK(SeekSourceSearchYd(true, false, false, true) == kSeekFallbackYd);
    CHECK(18.0f <= SeekSourceSearchYd(true, false, false, true));
    CHECK(SeekSourceSelectable(true, 5));
    CHECK(!SeekSourceSelectable(false, 5)); // an idle Son is still not a pick

    // SeekForcedBurnReachesOffSpotSource: with three tanks the search used to stay at the spot's 8 yd, so a Son 11 yd
    // out could never become the gather source and was held for the whole fight. A forced burn widens the reach to
    // 35 yd, and the source the gather then stands around is not held once the gather is ready.
    CHECK(SeekSourceSearchYd(true, true, false, false) == kSeekFallbackYd);
    CHECK(11.0f <= SeekSourceSearchYd(true, true, false, false));
    CHECK(!SeekHoldDecision(true, true, true, false, 80.0f, 30.0f, 11.0f, 5));

    // SeekBelowHoldSourceStillWaits: a low Son only widens the search while it stands inside the seek's reach and
    // fights; it is picked and held to the hold percent like any other until the gather is ready.
    CHECK(SeekSourceFinishable(true, 14.0f, 30.0f, 5));
    CHECK(!SeekSourceFinishable(false, 14.0f, 30.0f, 5));
    CHECK(!SeekSourceFinishable(true, 31.0f, 30.0f, 5));
    CHECK(SeekSourceSearchYd(true, false, true, false) == kSeekFallbackYd);
    CHECK(SeekHoldDecision(true, false, false, false, 14.0f, 30.0f, 5.0f, 5));
    CHECK(SeekHoldDecision(true, false, false, false, 40.0f, 30.0f, 5.0f, 5));
    CHECK(SeekHoldDecision(true, true, false, false, 14.0f, 30.0f, 5.0f, 5));

    // SeekFarSonNeverWidensTheSearch: a Son fighting at or below the hold percent widens the search only while it
    // stands inside the seek's 35 yd of the spot. One farther out is not the below-hold pick, so it cannot flip the
    // gather onto it or drop the latch; the search stays at the spot's radius with three tanks.
    CHECK(SeekFinishableInReach(true, 14.0f, 30.0f, 34.0f, 5));
    CHECK(!SeekFinishableInReach(true, 14.0f, 30.0f, 36.0f, 5));
    CHECK(!SeekFinishableInReach(true, 31.0f, 30.0f, 10.0f, 5));
    CHECK(!SeekFinishableInReach(false, 14.0f, 30.0f, 10.0f, 5));
    CHECK(SeekSourceSearchYd(true, false, false, false) == kSeekSpotYd);
    // A picked Son beyond the reach is held even inside the window with the gather ready: the gather cannot stand
    // around it inside the row's radius.
    CHECK(SeekHoldDecision(true, true, true, false, 80.0f, 30.0f, 36.0f, 5));

    // SeekDosedRaidPicksNothing: while every seeker already carries 24321 (seekers 0, after the cloud landed and before
    // the siphon strips it) no new Son is picked and the picked one is not killed, however the gather ran and even at a
    // forced burn; a Son that is not fighting is never picked either. The pick resumes with the next siphon. Tested on
    // the second Son in reach: low on health and inside the 35 yd reach, it is neither the below-hold pick nor a kill.
    CHECK(!SeekSourceSelectable(true, 0));
    CHECK(SeekSourceSelectable(true, 1));
    CHECK(!SeekSourceSelectable(false, 1));
    CHECK(!SeekSourceFinishable(true, 20.0f, 30.0f, 0));
    CHECK(SeekSourceFinishable(true, 20.0f, 30.0f, 1));
    CHECK(!SeekFinishableInReach(true, 20.0f, 30.0f, 34.0f, 0));
    CHECK(SeekFinishableInReach(true, 20.0f, 30.0f, 34.0f, 1));
    CHECK(SeekHoldDecision(true, true, true, false, 20.0f, 30.0f, 5.0f, 0));
    CHECK(SeekHoldDecision(true, true, true, true, 20.0f, 30.0f, 5.0f, 0));
    CHECK(!SeekHoldDecision(true, true, true, false, 20.0f, 30.0f, 5.0f, 2));

    // Pets leave the siphon: one pet inside the 100 yd hit is a full 8 000 heal because the core reads the poison on
    // every living unit. The window starts 5 s before the due time and lasts until 2 s after it; the dummy aura that
    // marks a siphon in progress holds it too, and a pet may be called again once the window is over.
    CHECK(!SeekPetMustLeave(6000, false));
    CHECK(SeekPetMustLeave(5000, false));
    CHECK(SeekPetMustLeave(1, false));
    CHECK(SeekPetMustLeave(0, false));
    CHECK(SeekPetMustLeave(-1000, false));
    CHECK(SeekPetMustLeave(-2000, false));
    CHECK(!SeekPetMustLeave(-2001, false));
    CHECK(SeekPetMustLeave(90000, true));
    CHECK(SeekPetMustLeave(-30000, true));

    // SeekTotemsAndGuardiansLeaveTheSiphon: a shaman's placements are named "<element> totem" (the damage ones
    // too), and the rotations' pet calls and demon summons are their own names. Each leaves one more undosed unit to
    // the siphon's hit, so the hold rule reads the spell/action name rather than the class alone.
    CHECK(SeekTotemSpell("stoneskin totem"));
    CHECK(SeekTotemSpell("fire nova totem"));
    CHECK(SeekTotemSpell("disease cleansing totem"));
    CHECK(!SeekTotemSpell("totemic recall"));
    CHECK(!SeekTotemSpell("lightning bolt"));
    CHECK(!SeekTotemSpell("totem"));
    CHECK(SeekPetCallAction("call pet"));
    CHECK(SeekPetCallAction("summon imp"));
    CHECK(SeekPetCallAction("summon inferno"));
    CHECK(SeekPetCallAction("inferno"));
    CHECK(!SeekPetCallAction("lightning bolt"));
    // The warlock's guardians are never summoned in the fight at all, not only around the window.
    CHECK(SeekGuardianSummonAction("summon inferno"));
    CHECK(SeekGuardianSummonAction("inferno"));
    CHECK(SeekGuardianSummonAction("summon doomguard"));
    CHECK(!SeekGuardianSummonAction("summon imp"));
    CHECK(!SeekGuardianSummonAction("call pet"));

    // SeekWarlockDemonLeavesOnce: a warlock's demon goes at the engage and does not come back — the class rule is
    // true at any time in the fight, so the dismiss runs once and the summon hold stays up all fight. A hunter keeps
    // the window cycle and its pet waits in the current slot for the call back.
    CHECK(SeekClassPetMustLeave(true, 90000, false));
    CHECK(SeekClassPetMustLeave(true, -30000, false));
    CHECK(!SeekClassPetMustLeave(false, 6000, false));
    CHECK(SeekClassPetMustLeave(false, 5000, false));
    CHECK(!SeekClassPetMustLeave(false, -2001, false));

    // The cycle clock starts over on a re-pull: the boss out of combat, or a read gap longer than a re-pull (L1).
    CHECK(SeekClockStartOver(false, 0, 5000));
    CHECK(!SeekClockStartOver(true, 1000, 1500));
    CHECK(SeekClockStartOver(true, 1000, 12000));
    CHECK(!SeekClockStartOver(true, 0, 12000));

    // SeekSonAtAnchorNorthEdgeInReach: with a live boss anchor the reach is measured from the anchor's centre (the
    // boss's spawn) as well as from B. A Son the walk-out brought to the anchor's north edge stands 25 yd from the
    // spawn but 37.7 yd from B (15 yd south of it): it is in reach, picked, and released for the forced burn.
    {
        const float fromB = 37.7f;
        const float fromAnchor = 25.0f;
        CHECK(fromB > kSeekFallbackYd);
        const float anchorYd = 30.0f;
        const float reach = SeekReachYd(fromB, fromAnchor, true, anchorYd);
        CHECK(reach <= kSeekFallbackYd);
        CHECK(!SeekHoldDecision(true, true, false, true, 60.0f, 30.0f, reach, 10));
        CHECK(SeekFinishableInReach(true, 25.0f, 30.0f, reach, 10));
        // Unanchored, the reach is still B's alone: the same Son is held.
        const float unanchored = SeekReachYd(fromB, fromAnchor, false, anchorYd);
        CHECK(unanchored > kSeekFallbackYd);
        CHECK(SeekHoldDecision(true, true, false, true, 60.0f, 30.0f, unanchored, 10));
        // The nearer of the two counts: a Son near B but far from the spawn stays in reach.
        CHECK(SeekReachYd(10.0f, 40.0f, true, anchorYd) <= kSeekFallbackYd);
        // The anchor's part of the reach ends at its own radius: a Son 33 yd from the spawn is outside the 30 yd
        // anchor, so only its 37.7 yd from B counts and it is held.
        CHECK(SeekReachYd(fromB, 33.0f, true, anchorYd) > kSeekFallbackYd);
        CHECK(SeekHoldDecision(true, true, false, true, 60.0f, 30.0f, SeekReachYd(fromB, 33.0f, true, anchorYd), 10));
        CHECK(SeekReachYd(fromB, 30.0f, true, anchorYd) <= kSeekFallbackYd);
        // A Son past both reaches is held.
        CHECK(SeekReachYd(50.0f, 36.0f, true, anchorYd) > kSeekFallbackYd);
    }

    // The take row's reason (walk 53): all eight inputs of the chase refusal, a taunt's presence and its cast.
    CHECK(std::string(SeekTakeReasonWord(false, false, false)) == "no_taunt");
    CHECK(std::string(SeekTakeReasonWord(false, false, true)) == "no_taunt");
    CHECK(std::string(SeekTakeReasonWord(false, true, false)) == "attack");
    CHECK(std::string(SeekTakeReasonWord(false, true, true)) == "taunt");
    CHECK(std::string(SeekTakeReasonWord(true, false, false)) == "chase_refused");
    CHECK(std::string(SeekTakeReasonWord(true, false, true)) == "no_taunt");
    CHECK(std::string(SeekTakeReasonWord(true, true, false)) == "chase_refused");
    CHECK(std::string(SeekTakeReasonWord(true, true, true)) == "taunt");
    // The bounded take edge's own word, only for a refusal that could not taunt.
    CHECK(std::string(SeekTakeReasonWord(true, true, false, true)) == "edge_far");
    CHECK(std::string(SeekTakeReasonWord(true, false, false, true)) == "edge_far");
    CHECK(std::string(SeekTakeReasonWord(true, true, true, true)) == "taunt");
    CHECK(std::string(SeekTakeReasonWord(false, true, false, true)) == "attack");
    CHECK(std::string(SeekTakeReasonWord(false, false, false, true)) == "no_taunt");

    // The open meet's Son stays held until a tank has it (walk 54: released 2-6 s after the shot at 98 % hp, because the
    // puller counted as a non-tank for 5 s and the late-engaged burn was forced).
    {
        // The meet Son on the puller (a non-tank) at 5 s non-tank time and with the forced burn: the hold rule alone
        // releases it (NonTank), the meet hold keeps it.
        CHECK(SeekHoldWhyOf(true, true, false, true, 98.0f, 30.0f, 34.0f, 3, 5000, 0) == SeekHoldWhy::NonTank);
        CHECK(!SeekHoldDecision(true, true, false, true, 98.0f, 30.0f, 34.0f, 3, 5000, 0));
        CHECK(SeekMeetHoldsSon(true, true, false));
        // Also the 6 s clock and a Son the selected burn would release (no non-tank time at all).
        CHECK(!SeekHoldDecision(true, true, true, true, 98.0f, 30.0f, 34.0f, 3, 6000, 0));
        CHECK(SeekMeetHoldsSon(true, true, false));
        // A Son on a tank is released as today.
        CHECK(!SeekMeetHoldsSon(true, true, true));
        // The meet closed (or the flag off): today's decision.
        CHECK(!SeekMeetHoldsSon(false, true, false));
        // Another Son is not the meet's.
        CHECK(!SeekMeetHoldsSon(true, false, false));
        CHECK(!SeekMeetHoldsSon(true, false, true));
    }

    std::cout << "All seek-aura policy tests passed.\n";
    return 0;
}
