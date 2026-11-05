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
        const std::uint32_t due = SeekDueMs(0, 0, 90000);
        CHECK(due == 90000);
        CHECK(!SeekWindowOpen(9999, due, 80000));
        CHECK(SeekWindowOpen(10000, due, 80000));
        CHECK(SeekWindowOpen(87000, due, 80000));
        CHECK(!SeekWindowOpen(87001, due, 80000));
    }

    // A sighting starts a new cycle only half a cycle after the last accepted one: every bot sees the same siphon
    // within a few hundred ms, so the second sighting must not push the due time again.
    {
        CHECK(SeekCycleAccepts(0, 91000, 90000));
        CHECK(SeekDueMs(0, 91000, 90000) == 181000);
        CHECK(!SeekCycleAccepts(91000, 95000, 90000));
        CHECK(SeekDueMs(0, 91000, 90000) == 181000);
        CHECK(SeekCycleAccepts(91000, 136000, 90000));
        CHECK(SeekDueMs(0, 136000, 90000) == 226000);
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
    CHECK(!SeekHoldDecision(true, true, true, false, 20.0f, 30.0f, 5.0f));
    CHECK(SeekHoldDecision(false, true, true, false, 20.0f, 30.0f, 5.0f));

    // SeekPickedSourceStillHoldsAtPercent: inside the window the picked source still stops at the hold percent until
    // the gather is ready (the old bypass finished a 14 % Son before the raid stood around it).
    CHECK(SeekHoldDecision(true, true, false, false, 14.0f, 30.0f, 5.0f));
    CHECK(!SeekHoldDecision(true, true, true, false, 14.0f, 30.0f, 5.0f));
    CHECK(SeekHoldDecision(true, true, false, false, 29.0f, 30.0f, 5.0f)); // 29 <= 30: still the hold
    CHECK(!SeekHoldDecision(true, true, false, false, 31.0f, 30.0f, 5.0f)); // above the hold: burn it down

    // SeekForcedBurnReleasesTheHold: the forced burn (the window is about to close) releases the picked source even
    // before the gather is ready, and the gather row's readiness release is the same rule.
    CHECK(!SeekHoldDecision(true, true, false, true, 14.0f, 30.0f, 5.0f));
    CHECK(!SeekHoldDecision(true, true, true, false, 80.0f, 30.0f, 5.0f));

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

    // Without the third tank the gather goes to the source wherever it stands within 35 yd of the spot (M3).
    CHECK(SeekSourceSearchYd(true, false, false) == kSeekSpotYd);
    CHECK(SeekSourceSearchYd(false, false, false) == kSeekFallbackYd);
    CHECK(kSeekFallbackYd == 35.0f);

    // SeekForcedBurnReachesOffSpotSource: with three tanks the search used to stay at the spot's 8 yd, so a Son 11 yd
    // out could never become the gather source and was held for the whole fight. A forced burn widens the reach to
    // 35 yd, and the source the gather then stands around is not held once the gather is ready.
    CHECK(SeekSourceSearchYd(true, true, false) == kSeekFallbackYd);
    CHECK(11.0f <= SeekSourceSearchYd(true, true, false));
    CHECK(!SeekHoldDecision(true, true, true, false, 80.0f, 30.0f, 11.0f));

    // SeekBelowHoldSourceStillWaits: a low Son only widens the search while it stands inside the seek's reach and
    // fights; it is picked and held to the hold percent like any other until the gather is ready.
    CHECK(SeekSourceFinishable(true, 14.0f, 30.0f));
    CHECK(!SeekSourceFinishable(false, 14.0f, 30.0f));
    CHECK(!SeekSourceFinishable(true, 31.0f, 30.0f));
    CHECK(SeekSourceSearchYd(true, false, true) == kSeekFallbackYd);
    CHECK(SeekHoldDecision(true, false, false, false, 14.0f, 30.0f, 5.0f));
    CHECK(SeekHoldDecision(true, false, false, false, 40.0f, 30.0f, 5.0f));
    CHECK(SeekHoldDecision(true, true, false, false, 14.0f, 30.0f, 5.0f));

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

    // The cycle clock starts over on a re-pull: the boss out of combat, or a read gap longer than a re-pull (L1).
    CHECK(SeekClockStartOver(false, 0, 5000));
    CHECK(!SeekClockStartOver(true, 1000, 1500));
    CHECK(SeekClockStartOver(true, 1000, 12000));
    CHECK(!SeekClockStartOver(true, 0, 12000));

    std::cout << "All seek-aura policy tests passed.\n";
    return 0;
}
