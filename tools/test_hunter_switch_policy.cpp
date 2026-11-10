#include "../ai/playerbot/HunterSwitchPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::kHunterMeleeGlueYd;
using ai::kHunterRangedReturnYd;
using ai::ShouldSwitchToMelee;
using ai::ShouldSwitchToRanged;
using ai::HunterKitState;
using ai::HunterDeadZoneStep;
using ai::DecideHunterDeadZoneStep;
using ai::HunterKitDwellElapsed;
using ai::NoteHunterKitSwitch;
using ai::BeginHunterStepOut;
using ai::HunterMeleeRowIgnoresThreatBackoff;
using ai::HunterStepOutLength;
using ai::HunterTrapTargetInReach;
using ai::kHunterStepOutMaxAttempts;
using ai::kHunterStepOutMaxYd;
using ai::kHunterStepOutTargetYd;
using ai::kHunterTrapReachYd;
using ai::kHunterKitMinDwellMs;
using ai::kHunterStepOutWindowMs;
using ai::kHunterStepOutRetryMs;
using ai::IsShootingSpotSafeFromUnpulled;
using ai::IsShootingSpotSafeFromEnemy;
using ai::ScoreShootingSpot;

int main()
{
    std::cout << "Starting TortoiseBots hunter-switch tests...\n";

    // Edges: the donor's own 8 yd melee edge (melee <= 8; ranged returns on
    // victim-off, not distance); the 10 yd constant is kept for future use.
    CHECK(kHunterMeleeGlueYd == 8.0f);
    CHECK(kHunterRangedReturnYd == 10.0f);

    // Melee side: the donor AND (victim on bot, dist <= 8) plus mobile.
    {
        // Glued victim at the edge: trade (fast or slow - the speed OR is
        // gone, a slow mob glued on the bot is still a dead-zone mob).
        CHECK(ShouldSwitchToMelee(true, true, false, true, 8.0f, true));
        CHECK(ShouldSwitchToMelee(true, true, false, false, 8.0f, true));
        // Just outside the edge: hold ranged even with the victim on bot.
        CHECK(!ShouldSwitchToMelee(true, true, false, true, 8.1f, true));
        // Victim off the bot or rooted: hold (rooted is shot, not charged).
        CHECK(!ShouldSwitchToMelee(true, false, false, true, 3.0f, true));
        CHECK(!ShouldSwitchToMelee(true, true, true, true, 3.0f, true));
        // No ranged kit to trade from: hold.
        CHECK(!ShouldSwitchToMelee(false, true, false, true, 3.0f, true));
        // Out of ammo always trades: nothing to shoot with.
        CHECK(ShouldSwitchToMelee(true, false, true, false, 30.0f, false));
        std::cout << "  [PASS] melee trades only when the victim is glued on the bot\n";
    }

    // Ranged side: off the bot (pet/tank holds it) at any distance, or rooted.
    {
        // Off the bot past the edge: back to ranged.
        CHECK(ShouldSwitchToRanged(true, true, false, false, 8.1f, true));
        // Off the bot inside the dead zone: back to ranged too (the close
        // kit's reach melee would otherwise keep the hunter meleeing a
        // pet-held mob for the whole fight); the ranged kit steps out.
        CHECK(ShouldSwitchToRanged(true, true, false, false, 8.0f, true));
        CHECK(ShouldSwitchToRanged(true, true, false, false, 2.0f, true));
        // Victim still on the bot: hold even far out (melee finishes it).
        CHECK(!ShouldSwitchToRanged(true, false, false, false, 30.0f, true));
        // Rooted: ranged at any distance, even glued on the bot.
        CHECK(ShouldSwitchToRanged(true, false, true, false, 3.0f, true));
        // Merely slow no longer answers at melee distance (the flap source:
        // 83% of melee->ranged pairs landed at dist < 5 yd on this branch).
        CHECK(!ShouldSwitchToRanged(true, false, false, true, 3.0f, true));
        // No close kit to trade back from, or no ammo: hold.
        CHECK(!ShouldSwitchToRanged(false, true, false, false, 30.0f, true));
        CHECK(!ShouldSwitchToRanged(true, true, false, false, 30.0f, false));
        std::cout << "  [PASS] ranged returns once the mob is off the bot, or rooted\n";
    }

    // Kit dwell (REVIEW finding 4): victim on/off alone flips the kit on
    // every aggro trade inside 8 yd; a kit is now held 4 s first.
    {
        CHECK(kHunterKitMinDwellMs == 4000u);
        HunterKitState state;
        // Never switched: free to switch.
        CHECK(HunterKitDwellElapsed(state, 500u));
        NoteHunterKitSwitch(state, 100000u);
        CHECK(!HunterKitDwellElapsed(state, 100000u));
        CHECK(!HunterKitDwellElapsed(state, 103999u));
        CHECK(HunterKitDwellElapsed(state, 104000u));
        // Wrap-safe: a switch just before the ms timer wraps.
        NoteHunterKitSwitch(state, 0xFFFFFF00u);
        CHECK(!HunterKitDwellElapsed(state, 0x00000100u));
        CHECK(HunterKitDwellElapsed(state, 0x00000F00u + 4000u));

        bool const dwellNotElapsed = false;
        // Aggro ping-pong inside 8 yd: the pet Growls the mob off a hunter
        // that just traded into melee -> hold melee until the dwell is up.
        CHECK(!ShouldSwitchToRanged(true, true, false, false, 3.0f, true, false, dwellNotElapsed));
        CHECK(ShouldSwitchToRanged(true, true, false, false, 3.0f, true, false, true));
        // ...and a mob running back onto a hunter that just went ranged.
        CHECK(!ShouldSwitchToMelee(true, true, false, true, 3.0f, true, false, dwellNotElapsed));
        CHECK(ShouldSwitchToMelee(true, true, false, true, 3.0f, true, false, true));
        // Rooted waits for the dwell too; out of ammo never waits.
        CHECK(!ShouldSwitchToRanged(true, false, true, false, 3.0f, true, false, dwellNotElapsed));
        CHECK(ShouldSwitchToMelee(true, false, false, false, 30.0f, false, false, dwellNotElapsed));
        std::cout << "  [PASS] kit switches wait for the 4 s dwell (out of ammo excepted)\n";
    }

    // Dead-zone step-out: a step per mob, hold while it runs, retry farther
    // when it fails - never the old flee every tick, never a melee trade for
    // a mob that is not on the hunter (#1018).
    {
        uint64_t const mob = 0xF130001234000042ull;
        uint64_t const otherMob = 0xF130001234000043ull;
        HunterKitState state;
        uint32_t const t0 = 200000u;
        CHECK(kHunterStepOutMaxAttempts == 3);
        // Out of the dead zone, or the mob is on the hunter: not this row.
        CHECK(DecideHunterDeadZoneStep(state, mob, t0, false, false, false) == HunterDeadZoneStep::None);
        CHECK(DecideHunterDeadZoneStep(state, mob, t0, true, true, false) == HunterDeadZoneStep::None);
        // Tank-held mob in the dead zone, no episode yet: step out.
        CHECK(DecideHunterDeadZoneStep(state, mob, t0, true, false, false) == HunterDeadZoneStep::StepOut);
        BeginHunterStepOut(state, mob, t0);
        CHECK(state.stepOutAttempt == 1);
        // Step under way: hold, do not re-dispatch.
        CHECK(DecideHunterDeadZoneStep(state, mob, t0 + 500u, true, false, true) == HunterDeadZoneStep::Hold);
        // Stopped but still inside (no spot, or the mob came along): retry.
        CHECK(DecideHunterDeadZoneStep(state, mob, t0 + 500u, true, false, false) == HunterDeadZoneStep::StepOut);
        // Still moving after the window: retry too.
        CHECK(DecideHunterDeadZoneStep(state, mob, t0 + kHunterStepOutWindowMs, true, false, true) == HunterDeadZoneStep::StepOut);
        // Got out: nothing to do.
        CHECK(DecideHunterDeadZoneStep(state, mob, t0 + 1000u, false, false, false) == HunterDeadZoneStep::None);
        // Second attempt inside the episode: same episode, attempt 2, its
        // own window.
        BeginHunterStepOut(state, mob, t0 + 3500u);
        CHECK(state.stepOutAttempt == 2);
        CHECK(state.stepOutStartMs == t0);
        CHECK(DecideHunterDeadZoneStep(state, mob, t0 + 4000u, true, false, true) == HunterDeadZoneStep::Hold);
        CHECK(DecideHunterDeadZoneStep(state, mob, t0 + 4000u, true, false, false) == HunterDeadZoneStep::StepOut);
        BeginHunterStepOut(state, mob, t0 + 4000u);
        CHECK(state.stepOutAttempt == 3);
        // Every attempt failed: Stuck (hold the ranged kit), not another step.
        CHECK(DecideHunterDeadZoneStep(state, mob, t0 + 6000u, true, false, false) == HunterDeadZoneStep::Stuck);
        // A different mob, or the episode ran out: a fresh episode, attempt 1.
        CHECK(DecideHunterDeadZoneStep(state, otherMob, t0 + 6000u, true, false, false) == HunterDeadZoneStep::StepOut);
        CHECK(DecideHunterDeadZoneStep(state, mob, t0 + kHunterStepOutRetryMs, true, false, false) == HunterDeadZoneStep::StepOut);
        BeginHunterStepOut(state, mob, t0 + kHunterStepOutRetryMs);
        CHECK(state.stepOutAttempt == 1);
        CHECK(state.stepOutStartMs == t0 + kHunterStepOutRetryMs);
        BeginHunterStepOut(state, otherMob, t0 + kHunterStepOutRetryMs + 100u);
        CHECK(state.stepOutAttempt == 1);
        CHECK(state.stepOutTarget == otherMob);

        // Stuck no longer trades into melee for a mob off the hunter, inside
        // or after the dwell (the old rule did; Zul'Farrak: melee kit 13-20 %
        // of fight time, the mob on the tank in 47-73 % of those frames).
        CHECK(!ShouldSwitchToMelee(true, false, false, false, 3.0f, true, true, false));
        CHECK(!ShouldSwitchToMelee(true, false, false, false, 3.0f, true, true, true));
        CHECK(!ShouldSwitchToMelee(false, false, false, false, 3.0f, true, true, true));
        // A glued mob on the hunter still trades after the dwell, stuck or not.
        CHECK(ShouldSwitchToMelee(true, true, false, false, 3.0f, true, true, true));
        CHECK(!ShouldSwitchToMelee(true, true, false, false, 3.0f, true, true, false));
        // The close kit no longer holds after a failed step-out: a mob off the
        // hunter hands ranged back after the dwell (the step-out moves it).
        CHECK(ShouldSwitchToRanged(true, true, false, false, 3.0f, true, true, true));
        CHECK(ShouldSwitchToRanged(true, true, false, false, 3.0f, true, false, true));
        // A rooted mob is shot from where the hunter stands regardless.
        CHECK(ShouldSwitchToRanged(true, true, true, false, 3.0f, true, true, true));

        // Step length: aims 15 yd from the target, +5 yd per retry, clamped
        // to 4..20 yd; never aims inside the dead zone + 3 yd.
        CHECK(kHunterStepOutTargetYd == 15.0f);
        CHECK(kHunterStepOutMaxYd == 20.0f);
        CHECK(HunterStepOutLength(2.0f, 8.0f) == 13.0f);
        CHECK(HunterStepOutLength(6.0f, 8.0f, 1) == 9.0f);
        CHECK(HunterStepOutLength(6.0f, 8.0f, 2) == 14.0f);
        CHECK(HunterStepOutLength(6.0f, 8.0f, 3) == 19.0f);
        CHECK(HunterStepOutLength(0.0f, 8.0f, 3) == 20.0f);
        CHECK(HunterStepOutLength(13.0f, 8.0f) == 4.0f);
        // A longer dead zone raises the aim to its edge + 3 yd.
        CHECK(HunterStepOutLength(0.0f, 14.0f) == 17.0f);
        // Attempt 0 (never stamped) aims like the first attempt.
        CHECK(HunterStepOutLength(2.0f, 8.0f, 0) == 13.0f);
        std::cout << "  [PASS] dead zone: step out to 15 yd, retry farther, never melee a mob off the hunter\n";
    }

    // Traps on the target: laid only when the mob is already in melee reach
    // of the hunter, never after a walk into melee (#1018: 22-29 walks per
    // Zul'Farrak run, 0 traps cast).
    {
        CHECK(kHunterTrapReachYd == 5.0f);
        CHECK(HunterTrapTargetInReach(0.0f));
        CHECK(HunterTrapTargetInReach(5.0f));
        CHECK(!HunterTrapTargetInReach(5.1f));
        // The tank's mob ~12 yd away: no trap, no walk.
        CHECK(!HunterTrapTargetInReach(12.0f));
        std::cout << "  [PASS] traps: only for a mob already in melee reach\n";
    }

    {
        // Melee row vs the party threat back-off: a mob on the hunter is
        // exempt, a mob someone else holds is not.
        CHECK(HunterMeleeRowIgnoresThreatBackoff(true));
        CHECK(!HunterMeleeRowIgnoresThreatBackoff(false));
        std::cout << "  [PASS] melee row: exempt from the threat back-off only for a mob on the hunter\n";
    }

    {
        // Shooting spot safety and scoring: unpulled hostile avoidance,
        // active enemy dead-zone and approaching enemy clearance.
        float const aggroDist = 20.0f;
        CHECK(IsShootingSpotSafeFromUnpulled(20.0f, aggroDist));
        CHECK(IsShootingSpotSafeFromUnpulled(25.0f, aggroDist));
        CHECK(!IsShootingSpotSafeFromUnpulled(19.9f, aggroDist));
        CHECK(!IsShootingSpotSafeFromUnpulled(5.0f, aggroDist));

        float const minRange = 8.0f;
        // Outside dead zone + 1 yd: safe
        CHECK(IsShootingSpotSafeFromEnemy(9.0f, minRange, 12.0f, false));
        CHECK(IsShootingSpotSafeFromEnemy(15.0f, minRange, 12.0f, false));
        // Inside dead zone: refused
        CHECK(!IsShootingSpotSafeFromEnemy(8.9f, minRange, 12.0f, false));
        CHECK(!IsShootingSpotSafeFromEnemy(4.0f, minRange, 12.0f, false));
        // Target itself stepping out: moving further than current dist is fine
        CHECK(IsShootingSpotSafeFromEnemy(15.0f, minRange, 5.0f, false));
        // Approaching enemy on the bot: stepping closer than current distance is refused
        CHECK(!IsShootingSpotSafeFromEnemy(10.0f, minRange, 12.0f, true));
        CHECK(IsShootingSpotSafeFromEnemy(14.0f, minRange, 12.0f, true));

        // Score: higher clearance wins; tie-break prefers smaller deflection angle
        CHECK(ScoreShootingSpot(15.0f, 0.0f) > ScoreShootingSpot(15.0f, 0.5f));
        CHECK(ScoreShootingSpot(15.0f, 0.5f) > ScoreShootingSpot(10.0f, 0.0f));
        std::cout << "  [PASS] shooting spot safety and scoring\n";
    }

    std::cout << "All hunter-switch checks PASSED!\n";
    return 0;
}
