#include "../ai/playerbot/AvoidAoePolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::AoeEscapeStep;
using ai::AoeFallbackHeadings;
using ai::AoeHazardCircle;
using ai::AoeLanding;
using ai::AoePoint;
using ai::ChooseClearAoeLanding;
using ai::DynObjIsHostileHazard;
using ai::HazardsHoldMove;
using ai::IsAvoidableAoeRadius;
using ai::kAoeAtSourceTargetA;
using ai::kAoeAtSourceTargetB;
using ai::kAoeFallbackHeadings;
using ai::kAoeFallbackStepYd;
using ai::kAoeFallbackWideStepYd;
using ai::kMaxAoeAvoidRadiusYd;
using ai::kTrapHazardMarginYd;
using ai::kTrapScanRangeYd;
using ai::kMaxTankDragStepYd;
using ai::LandingInsideHazard;
using ai::ReachEndpoint;
using ai::MeleeAoeCandidates;
using ai::MeleeAoeLandingInRange;
using ai::RangedAoeCandidates;
using ai::RangedAoeLandingInRange;
using ai::TrapHazardRadius;
using ai::TriggerSpellMarksSpinHazard;
using ai::TriggerSpellMarksGroundHazard;
using ai::TankDragFitsCap;
using ai::TankDragStep;
using ai::TankLandingDragsTargetOut;

int main()
{
    std::cout << "Starting TortoiseBots avoid-aoe policy tests...\n";

    // Radius gate: positive and within the 15yd cap.
    CHECK(IsAvoidableAoeRadius(5.0f));
    CHECK(IsAvoidableAoeRadius(15.0f));
    CHECK(!IsAvoidableAoeRadius(0.0f));
    CHECK(!IsAvoidableAoeRadius(-1.0f));
    CHECK(!IsAvoidableAoeRadius(15.1f));
    CHECK(kMaxAoeAvoidRadiusYd == 15.0f);
    std::cout << "  [PASS] radius cap gates all three sensor cases\n";

    // Melee order (donor): strafes first, toward-target third (strict);
    // tanking appends away-from-target and away-from-hazard fallbacks.
    {
        float out[5] = {};
        CHECK(MeleeAoeCandidates(false, out) == 3);
        CHECK(out[0] > 1.57f && out[0] < 1.58f);
        CHECK(out[1] < -1.57f && out[1] > -1.58f);
        CHECK(out[2] == 0.0f);
        CHECK(MeleeAoeCandidates(true, out) == 5);
        CHECK(out[3] > 3.14f && out[4] > 3.14f);
    }
    std::cout << "  [PASS] melee strafe-first order\n";

    // Ranged order: strafes, away, toward, away-from-hazard.
    {
        float out[5] = {};
        CHECK(RangedAoeCandidates(out) == 5);
        CHECK(out[0] > 0.0f && out[1] < 0.0f);
        CHECK(out[2] > 3.14f);
        CHECK(out[3] == 0.0f);
    }
    std::cout << "  [PASS] ranged strafe-first order\n";

    // Band tests: melee stays close, ranged stays in the cast band.
    CHECK(MeleeAoeLandingInRange(5.0f, 8.0f));
    CHECK(!MeleeAoeLandingInRange(8.1f, 8.0f));
    CHECK(RangedAoeLandingInRange(20.0f, 8.0f, 30.0f));
    CHECK(!RangedAoeLandingInRange(7.9f, 8.0f, 30.0f));
    CHECK(!RangedAoeLandingInRange(30.1f, 8.0f, 30.0f));
    std::cout << "  [PASS] stay-in-range band tests\n";

    // Spinning self-centred hazard (#971: Herod's Whirlwind 8989 -> 15578
    // is WEAPON_DAMAGE_NOSCHOOL around the caster, 8 yd — the old
    // school-damage-only gate never saw it). School damage keeps the old
    // rule (invisible emitters only: a selectable mob with a permanent
    // burning aura such as Fire Shield II 184 stays fightable); weapon
    // spins list anyone.
    CHECK(TriggerSpellMarksSpinHazard(2, 0, 0, false));    // school damage, emitter: old shape
    CHECK(TriggerSpellMarksSpinHazard(2, 22, 15, false));
    CHECK(!TriggerSpellMarksSpinHazard(2, 0, 0, true));    // school damage, selectable: stay and fight
    CHECK(!TriggerSpellMarksSpinHazard(2, 22, 15, true));  // Fire Shield II 184 shape: no step-out
    CHECK(TriggerSpellMarksSpinHazard(17, 22, 15, true));  // Herod 15578, selectable boss
    CHECK(TriggerSpellMarksSpinHazard(17, 22, 15, false)); // same spin on an emitter
    CHECK(TriggerSpellMarksSpinHazard(58, 22, 15, true));  // plain weapon damage spin
    CHECK(TriggerSpellMarksSpinHazard(31, 22, 15, true));  // weapon percent spin
    CHECK(!TriggerSpellMarksSpinHazard(17, 6, 0, true));   // single-target weapon hit (Cleave 15496): no spin
    CHECK(!TriggerSpellMarksSpinHazard(17, 6, 0, false));
    CHECK(!TriggerSpellMarksSpinHazard(17, 22, 0, true));  // weapon damage, wrong B target
    CHECK(!TriggerSpellMarksSpinHazard(17, 15, 22, true)); // weapon damage, swapped targets
    CHECK(!TriggerSpellMarksSpinHazard(10, 22, 15, true)); // heal around caster: not a hazard
    CHECK(!TriggerSpellMarksSpinHazard(0, 0, 0, false));   // empty effect
    std::cout << "  [PASS] whirlwind-type trigger marks a spin hazard\n";

    // Periodic trigger -> ground cloud (effect 27, persistent area aura).
    // Failing case before the fix: Sapphiron's Blizzard emitter 16474
    // (NOT_SELECTABLE, Chill 28534 -> Chill 28547 = effect 27, periodic
    // damage) was skipped by TriggerSpellMarksSpinHazard, which knows only
    // effects 2/17/58/31, so no bot ever stepped off the emitter.
    CHECK(!TriggerSpellMarksSpinHazard(27, 18, 28, false));  // the old gate: blind to effect 27
    CHECK(TriggerSpellMarksGroundHazard(27, false, false));  // Chill 28547 on emitter 16474: hazard
    CHECK(!TriggerSpellMarksGroundHazard(27, false, true));  // selectable mob's own cloud: stay and fight
    CHECK(!TriggerSpellMarksGroundHazard(27, true, false));  // positive cloud (a heal/buff field): no hazard
    CHECK(!TriggerSpellMarksGroundHazard(2, false, false));  // school damage is the spin gate's job
    CHECK(!TriggerSpellMarksGroundHazard(6, false, false));  // plain apply-aura (Locust Swarm 28786): no cloud
    CHECK(!TriggerSpellMarksGroundHazard(0, false, false));  // empty effect
    std::cout << "  [PASS] periodic trigger to a persistent area aura marks a ground hazard\n";

    // Inside a hostile dynamic object -> step out (the AvoidAuraWithDynamicObj
    // decision: DynObjIsHostileHazard gate, then the centre test). Failing
    // case before the sensor was real: the value was a stub, so this decision
    // never ran for Toxic Vapors 25786 (Kri, 8 yd) or Rain of Fire 28794
    // (Faerlina, 10 yd).
    {
        auto stepsOut = [](bool positive, bool playerCaster, bool friendly, AoeHazardCircle const& c,
                           float x, float y)
        {
            return DynObjIsHostileHazard(positive, playerCaster, friendly, c.radius) &&
                InsideAoeHazard(c, x, y, 0.0f);
        };
        AoeHazardCircle const vapors{0.0f, 0.0f, 0.0f, 8.0f};
        AoeHazardCircle const rain{0.0f, 0.0f, 0.0f, 10.0f};
        CHECK(stepsOut(false, false, false, vapors, 3.0f, 0.0f));   // bot in Kri's cloud: leave
        CHECK(stepsOut(false, false, true, rain, 9.0f, 0.0f));      // creature cloud whatever its faction
        CHECK(!stepsOut(false, false, false, vapors, 8.5f, 0.0f));  // outside the circle: stay
        CHECK(!stepsOut(false, true, true, rain, 1.0f, 0.0f));      // group mage's Blizzard: stay
        CHECK(!stepsOut(true, false, false, rain, 1.0f, 0.0f));     // positive effect: stay
        CHECK(!stepsOut(false, false, false, AoeHazardCircle{0.0f, 0.0f, 0.0f, 30.0f}, 1.0f, 0.0f)); // past the dodge cap
    }
    std::cout << "  [PASS] inside a hostile dynamic object steps out\n";

    // Escape step: just past the edge, capped at flee distance.
    CHECK(AoeEscapeStep(5.0f, 15.0f) == 6.0f);
    CHECK(AoeEscapeStep(20.0f, 15.0f) == 15.0f);
    std::cout << "  [PASS] escape step caps at flee distance\n";

    // Trap hazard radius: the spell's own circle (Flames 19628 is SpellRadius
    // index 8 = 5 yd) plus the margin, not the GO's 10 yd activation radius;
    // the activation radius is the fallback when the spell has none.
    CHECK(TrapHazardRadius(5.0f, 10.0f) == 5.0f + kTrapHazardMarginYd);
    CHECK(TrapHazardRadius(5.0f, 10.0f) < 10.0f);
    CHECK(TrapHazardRadius(0.0f, 10.0f) == 10.0f);
    CHECK(TrapHazardRadius(-1.0f, 10.0f) == 10.0f);
    CHECK(kAoeAtSourceTargetA == 22); // CASTER_SRC
    CHECK(kAoeAtSourceTargetB == 15); // ENEMY_AOE_AT_SRC_LOC
    std::cout << "  [PASS] trap hazard radius is the spell's circle\n";

    // A landing inside any damaging circle is rejected; at its edge and
    // outside all of them it is accepted (the core measures centre to centre
    // in 3D, so a ledge above a fire is out).
    {
        std::vector<AoeHazardCircle> hazards;
        hazards.push_back(AoeHazardCircle{0.0f, 0.0f, 0.0f, 7.0f});
        hazards.push_back(AoeHazardCircle{20.0f, 0.0f, 0.0f, 7.0f});
        CHECK(LandingInsideHazard(hazards, 0.0f, 0.0f, 0.0f));
        CHECK(LandingInsideHazard(hazards, 6.9f, 0.0f, 0.0f));
        CHECK(!LandingInsideHazard(hazards, 7.0f, 0.0f, 0.0f));
        CHECK(LandingInsideHazard(hazards, 20.0f, 6.0f, 0.0f));
        CHECK(!LandingInsideHazard(hazards, 10.0f, 0.0f, 0.0f));
        CHECK(!LandingInsideHazard(hazards, 0.0f, 0.0f, 7.0f));
        std::vector<AoeHazardCircle> none;
        CHECK(!LandingInsideHazard(none, 0.0f, 0.0f, 0.0f));

        // The hold, the CastHoldsMove shape: never the step-out, never a move
        // whose target is the bot, never without a destination.
        CHECK(HazardsHoldMove(false, false, true, 0.0f, 0.0f, 0.0f, hazards));
        CHECK(!HazardsHoldMove(false, false, true, 10.0f, 0.0f, 0.0f, hazards));
        CHECK(!HazardsHoldMove(true, false, true, 0.0f, 0.0f, 0.0f, hazards));
        CHECK(!HazardsHoldMove(false, true, true, 0.0f, 0.0f, 0.0f, hazards));
        CHECK(!HazardsHoldMove(false, false, false, 0.0f, 0.0f, 0.0f, hazards));
    }
    std::cout << "  [PASS] landing and hold on every damaging circle\n";

    // One centre-to-centre metric for the step-out trigger and the hold: the
    // circle a trap is stepped out of is the same circle a landing is refused
    // inside, so the trigger cannot fire at the radius the landing accepts.
    {
        AoeHazardCircle const fire{0.0f, 0.0f, 0.0f, TrapHazardRadius(5.0f, 10.0f)};
        CHECK(InsideAoeHazard(fire, 6.9f, 0.0f, 0.0f));
        CHECK(!InsideAoeHazard(fire, 7.0f, 0.0f, 0.0f));
    }
    std::cout << "  [PASS] one centre-to-centre metric for trigger and landing\n";

    // A reach is judged where it stops, not at the target's centre: the
    // target centre moved toward the bot by the action's stop range.
    {
        AoePoint p = ReachEndpoint(0.0f, 0.0f, 0.0f, 20.0f, 0.0f, 0.0f, 5.0f);
        CHECK(std::fabs(p.x - 5.0f) < 0.001f);
        CHECK(std::fabs(p.y) < 0.001f);
        CHECK(std::fabs(p.z) < 0.001f);
        // No stop range (a point move): the target's own centre.
        p = ReachEndpoint(0.0f, 0.0f, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f);
        CHECK(p.x == 0.0f && p.y == 0.0f);
        // A stop range longer than the gap clamps at the bot, never past it.
        p = ReachEndpoint(0.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f, 10.0f);
        CHECK(std::fabs(p.x - 3.0f) < 0.001f);
        // The same in 3D.
        p = ReachEndpoint(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 10.0f, 2.0f);
        CHECK(std::fabs(p.z - 2.0f) < 0.001f);
    }
    std::cout << "  [PASS] reach endpoint is the target moved toward the bot\n";

    // Widened last-resort search: sixteen headings all round the bot, nearest
    // step first, and only a landing clear of every damaging circle is taken.
    {
        float headings[kAoeFallbackHeadings] = {};
        AoeFallbackHeadings(headings);
        CHECK(kAoeFallbackHeadings == 16);
        CHECK(headings[0] == 0.0f);
        CHECK(headings[1] > 0.39f && headings[1] < 0.40f); // 22.5 degrees
        CHECK(headings[4] > 1.57f && headings[4] < 1.58f); // 90
        CHECK(headings[15] > 5.89f && headings[15] < 5.90f); // 2pi*15/16 (-22.5 degrees)

        // The fire being stepped out of: the bot starts inside it and a landing
        // in it is never an escape.
        AoeHazardCircle const leaving{ 0.0f, 0.0f, 0.0f, 7.0f };

        // One fire centred on the 12 yd ring at 202.5 degrees, wide enough to
        // cover every other heading on it: the fixed candidate directions
        // (0, +-90, 180) all land in fire, and only the 22.5 degree heading -
        // no fixed list has it - clears the circle.
        std::vector<AoeHazardCircle> fires;
        fires.push_back(AoeHazardCircle{
            12.0f * std::cos(3.5342917f), 12.0f * std::sin(3.5342917f), 0.0f, 23.8f});
        AoeLanding landings[kAoeFallbackHeadings] = {};
        for (int i = 0; i < kAoeFallbackHeadings; ++i)
            landings[i] = AoeLanding{ 12.0f * std::cos(headings[i]), 12.0f * std::sin(headings[i]), 0.0f };
        float const fixed[4] = { 0.0f, 1.5707963f, -1.5707963f, 3.1415927f };
        for (int i = 0; i < 4; ++i)
            CHECK(LandingInsideHazard(fires, 12.0f * std::cos(fixed[i]), 12.0f * std::sin(fixed[i]), 0.0f));
        CHECK(ChooseClearAoeLanding(fires, leaving, landings, kAoeFallbackHeadings) == 1);

        // Every candidate stands in fire: no move at all, not the least bad one.
        std::vector<AoeHazardCircle> everywhere;
        everywhere.push_back(AoeHazardCircle{ 0.0f, 0.0f, 0.0f, 20.0f });
        AoeLanding inFire[2] = { { 1.0f, 0.0f, 0.0f }, { 19.9f, 0.0f, 0.0f } };
        CHECK(ChooseClearAoeLanding(everywhere, leaving, inFire, 2) == -1);
        CHECK(ChooseClearAoeLanding(everywhere, leaving, inFire, 0) == -1);

        // The fire being left is a no-go on its own, without any listed hazard.
        std::vector<AoeHazardCircle> none;
        CHECK(ChooseClearAoeLanding(none, leaving, inFire, 2) == 1);
        AoeLanding inLeaving[2] = { { 1.0f, 0.0f, 0.0f }, { 3.0f, 0.0f, 0.0f } };
        CHECK(ChooseClearAoeLanding(none, leaving, inLeaving, 2) == -1);
        std::cout << "  [PASS] widened clear-landing search, no move when all stand in fire\n";
    }

    CHECK(kAoeFallbackStepYd == 12.0f);
    CHECK(kAoeFallbackWideStepYd == 20.0f);
    CHECK(kTrapScanRangeYd == 30.0f);

    // Tank drag (Venoxis P2: Poison Cloud 23861 is an 8 yd persistent-area
    // circle dropped under the boss): the step must take the boss, and the
    // melee around it, out of the cloud - the tank alone stepping to melee
    // reach only saves itself while the boss keeps standing in it. The step
    // is radius + both reaches + a margin, bounded by the hazard scan; the
    // landing must clear the same sum by the margin, and the cap is never
    // below that need (TankDragFitsCap).
    CHECK(TankDragStep(8.0f, 3.0f, 1.5f) == 14.0f);
    CHECK(TankDragStep(8.0f, 3.0f, 1.5f) <= kMaxTankDragStepYd);
    CHECK(!TankLandingDragsTargetOut(9.0f, 8.0f, 3.0f, 1.5f));
    CHECK(!TankLandingDragsTargetOut(12.0f, 8.0f, 3.0f, 1.5f));
    CHECK(TankLandingDragsTargetOut(13.0f, 8.0f, 3.0f, 1.5f));
    CHECK(TankLandingDragsTargetOut(14.0f, 8.0f, 3.0f, 1.5f));
    // The cap and the landing test must agree: a step the caller can take must
    // be a landing the drag test accepts, or the drag pass can never satisfy
    // it and the tank silently falls back to the bot-only escape. A tank at
    // the cloud centre finds a landing for a 15 yd cloud with a 10 yd target
    // reach and a 3 yd bot reach, and for the largest supported radius.
    CHECK(TankDragFitsCap(15.0f, 10.0f, 3.0f));
    CHECK(TankDragStep(15.0f, 10.0f, 3.0f) <= kMaxTankDragStepYd);
    CHECK(TankLandingDragsTargetOut(TankDragStep(15.0f, 10.0f, 3.0f), 15.0f, 10.0f, 3.0f));
    CHECK(TankDragFitsCap(kMaxAoeAvoidRadiusYd, 10.0f, 3.0f));
    CHECK(TankLandingDragsTargetOut(TankDragStep(kMaxAoeAvoidRadiusYd, 10.0f, 3.0f),
        kMaxAoeAvoidRadiusYd, 10.0f, 3.0f));
    // A need past the cap (a 15 yd cloud with a 30 yd reach) is refused, so the
    // caller drops the drag instead of the cap step that could never pass it.
    CHECK(!TankDragFitsCap(15.0f, 30.0f, 3.0f));
    CHECK(!TankDragFitsCap(15.0f, 10.0f, 15.0f));
    std::cout << "  [PASS] tank drag step takes the boss out of the cloud\n";

    // Only a hostile, non-positive circle is a hazard. The caster-friendly
    // exemption applies to a player-controlled caster only (a mage's Blizzard,
    // a pet's or a charmed unit's cloud): a trigger NPC's damaging cloud hurts
    // everyone, so a friendly-to-all creature still marks a hazard. A positive
    // ground effect is no danger, and the dodge cap still gates the circle (a
    // 30 yd pool is not stepped out of).
    CHECK(!DynObjIsHostileHazard(false, true, true, 8.0f));   // friendly player's cloud
    CHECK(DynObjIsHostileHazard(false, true, false, 8.0f));   // enemy player's cloud
    CHECK(DynObjIsHostileHazard(false, false, true, 8.0f));   // friendly creature's (trigger NPC) cloud
    CHECK(DynObjIsHostileHazard(false, false, false, 8.0f));  // hostile cloud
    CHECK(!DynObjIsHostileHazard(true, false, false, 8.0f));  // positive effect
    CHECK(!DynObjIsHostileHazard(true, true, false, 8.0f));   // positive player effect
    CHECK(!DynObjIsHostileHazard(false, false, false, 30.0f)); // past the dodge cap
    CHECK(!DynObjIsHostileHazard(false, false, false, 0.0f));  // no radius
    std::cout << "  [PASS] friendly or positive ground effects are no hazard\n";

    std::cout << "All avoid-aoe policy tests passed.\n";
    return 0;
}
