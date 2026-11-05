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

    // SeekTankPullWalksToOutOfCombatSon: a Son 30 yd out and not fighting is walked to, not dropped after one target
    // set (the tank stayed near the spot and the rotation returned to the boss, so 75 of 83 siphons had no Son in
    // combat). Inside the swing's reach it is hit; once it fights, or follows the tank, the take/drag branch owns it.
    CHECK(SeekTankPullStep(false, false, 30.0f) == SeekTankPullStep::Walk);
    CHECK(SeekTankPullStep(false, false, 60.0f) == SeekTankPullStep::Walk);
    CHECK(SeekTankPullStep(false, false, 5.0f) == SeekTankPullStep::Hit);
    CHECK(SeekTankPullStep(false, false, 4.0f) == SeekTankPullStep::Hit);
    CHECK(SeekTankPullStep(true, false, 30.0f) == SeekTankPullStep::Done);
    CHECK(SeekTankPullStep(false, true, 30.0f) == SeekTankPullStep::Done);

    // SeekTankPullHitNotHeld: the pulling tank's own hit on a Son at 100 % that is not fighting is exempt from the
    // seek hold, or the hold would stand the swing down and the Son would never aggro; once the Son fights, the hold
    // rule applies to it like to any other Son.
    CHECK(SeekPullHitExempt(true, false));
    CHECK(!SeekPullHitExempt(true, true));
    CHECK(!SeekPullHitExempt(false, false));
    CHECK(!SeekPullHitExempt(false, true));
    // The hold itself still answers true for a Son that is not the pick: the exemption is the caller's, on the puller's
    // own strike against a Son that has not joined the fight.
    CHECK(SeekHoldDecision(false, true, false, false, 100.0f, 30.0f, 30.0f, 5));

    // SeekPullFallsBackToHunter: with fewer than three tanks alive the third off-tank's pull is a hunter's shot, then
    // a ranged damage dealer's; nobody pulls without an off-tank (tanks[1]) to take the Son over.
    CHECK(SeekPullRoleOf(true, true, true, true) == SeekPullRole::ThirdTank);
    CHECK(SeekPullRoleOf(false, true, true, true) == SeekPullRole::Hunter);
    CHECK(SeekPullRoleOf(false, true, false, true) == SeekPullRole::Ranged);
    CHECK(SeekPullRoleOf(false, true, false, false) == SeekPullRole::None);
    CHECK(SeekPullRoleOf(false, false, true, true) == SeekPullRole::None);

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
    CHECK(SeekSourceSearchYd(true, false, false) == kSeekSpotYd);
    CHECK(SeekSourceSearchYd(false, false, false) == kSeekFallbackYd);
    CHECK(kSeekFallbackYd == 35.0f);

    // SeekForcedBurnReachesOffSpotSource: with three tanks the search used to stay at the spot's 8 yd, so a Son 11 yd
    // out could never become the gather source and was held for the whole fight. A forced burn widens the reach to
    // 35 yd, and the source the gather then stands around is not held once the gather is ready.
    CHECK(SeekSourceSearchYd(true, true, false) == kSeekFallbackYd);
    CHECK(11.0f <= SeekSourceSearchYd(true, true, false));
    CHECK(!SeekHoldDecision(true, true, true, false, 80.0f, 30.0f, 11.0f, 5));

    // SeekBelowHoldSourceStillWaits: a low Son only widens the search while it stands inside the seek's reach and
    // fights; it is picked and held to the hold percent like any other until the gather is ready.
    CHECK(SeekSourceFinishable(true, 14.0f, 30.0f, 5));
    CHECK(!SeekSourceFinishable(false, 14.0f, 30.0f, 5));
    CHECK(!SeekSourceFinishable(true, 31.0f, 30.0f, 5));
    CHECK(SeekSourceSearchYd(true, false, true) == kSeekFallbackYd);
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
    CHECK(SeekSourceSearchYd(true, false, false) == kSeekSpotYd);
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

    std::cout << "All seek-aura policy tests passed.\n";
    return 0;
}
