#include "../ai/playerbot/policy/combat/RangedApproachPolicy.h"
#include "../ai/playerbot/CombatSpreadPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::ApproachIdlePack;
using ai::ApproachPoint;
using ai::ApproachPointClearOfPack;
using ai::RangedApproachChoice;
using ai::RangedApproachInPosition;
using ai::RangedApproachPointSafe;
using ai::RangedApproachStopDistance;
using ai::SelectRangedApproachPoint;
using ai::StepBackToPackSafePoint;
using ai::UseRangedApproach;

static ApproachPoint P(float x, float y, float z = 0.0f)
{
    ApproachPoint point;
    point.x = x;
    point.y = y;
    point.z = z;
    return point;
}

static float Dist(ApproachPoint const& a, ApproachPoint const& b)
{
    return std::sqrt(ai::ApproachDistanceSq(a, b));
}

static bool Near(ApproachPoint const& a, ApproachPoint const& b)
{
    return Dist(a, b) < 0.01f;
}

int main()
{
    std::cout << "Starting TortoiseBots ranged approach policy tests...\n";

    auto const seesAll = [](ApproachPoint const&) { return true; };

    // Only where the core chase fails: inside a dungeon, beyond melee reach.
    // Open world and melee keep the chase (the caller checks sight after this).
    {
        CHECK(UseRangedApproach(true, 30.0f, 5.0f));
        CHECK(!UseRangedApproach(false, 30.0f, 5.0f));
        CHECK(!UseRangedApproach(true, 5.0f, 5.0f));
        CHECK(!UseRangedApproach(true, 0.0f, 5.0f));
        std::cout << "  [PASS] gate: dungeon + beyond melee only\n";
    }

    // Uldaman shape: healer 38 yd from the tank down a straight corridor,
    // heal reach 27 yd. The point is on the path exactly 27 yd from the tank,
    // so the healer walks 11 yd and stops.
    {
        std::vector<ApproachPoint> path = { P(38, 0), P(30, 0), P(20, 0), P(10, 0), P(0, 0) };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0), 27.0f, seesAll);
        CHECK(choice.valid);
        CHECK(Near(choice.point, P(27, 0)));
        std::cout << "  [PASS] straight corridor: stops on the path at 27 yd\n";
    }

    // Path around a corner: the straight line would be inside a wall, the
    // chosen point is on the path segment and exactly `distance` from the target.
    {
        std::vector<ApproachPoint> path = { P(40, 30), P(40, 0), P(0, 0) };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0), 25.0f, seesAll);
        CHECK(choice.valid);
        CHECK(std::fabs(choice.point.y) < 0.01f);
        CHECK(std::fabs(Dist(choice.point, P(0, 0)) - 25.0f) < 0.01f);
        std::cout << "  [PASS] corner: point on the last leg at range\n";
    }

    // The path re-enters range late; the walk back from the end stops where
    // the target goes out of sight (a pillar between point and target).
    {
        std::vector<ApproachPoint> path = { P(50, 0), P(20, 5), P(15, 0), P(10, 0), P(0, 0) };
        auto const pillar = [](ApproachPoint const& point) { return point.x < 18.0f; };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0), 30.0f, pillar);
        CHECK(choice.valid);
        CHECK(Near(choice.point, P(15, 0)));
        std::cout << "  [PASS] line of sight: stops at the last point that sees the target\n";
    }

    // The exact-range point on the crossing segment is itself out of sight:
    // fall back to the in-range vertex that sees the target.
    {
        std::vector<ApproachPoint> path = { P(40, 0), P(10, 0), P(0, 0) };
        auto const near = [](ApproachPoint const& point) { return point.x <= 10.0f; };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0), 25.0f, near);
        CHECK(choice.valid);
        CHECK(Near(choice.point, P(10, 0)));
        std::cout << "  [PASS] edge point out of sight: keeps the vertex in sight\n";
    }

    // Range is 3D: a target on a dais 10 yd up counts its height.
    {
        std::vector<ApproachPoint> path = { P(40, 0, 0), P(0, 0, 0) };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0, 10), 26.0f, seesAll);
        CHECK(choice.valid);
        CHECK(std::fabs(Dist(choice.point, P(0, 0, 10)) - 26.0f) < 0.01f);
        CHECK(std::fabs(choice.point.x - 24.0f) < 0.01f);
        std::cout << "  [PASS] height counts towards the range\n";
    }

    // Partial path (navmesh island or seam) that ends out of range: walk to
    // its end when that closes the gap, never when it leads away.
    {
        std::vector<ApproachPoint> closer = { P(60, 0), P(45, 0) };
        RangedApproachChoice const choice = SelectRangedApproachPoint(closer, P(0, 0), 27.0f, seesAll);
        CHECK(choice.valid);
        CHECK(Near(choice.point, P(45, 0)));

        std::vector<ApproachPoint> away = { P(40, 0), P(50, 10) };
        CHECK(!SelectRangedApproachPoint(away, P(0, 0), 27.0f, seesAll).valid);
        std::cout << "  [PASS] partial path: walks only towards the target\n";
    }

    // Nothing to do: already in range, too short a path, no distance.
    {
        std::vector<ApproachPoint> alreadyThere = { P(20, 0), P(0, 0) };
        CHECK(!SelectRangedApproachPoint(alreadyThere, P(0, 0), 27.0f, seesAll).valid);

        std::vector<ApproachPoint> single = { P(40, 0) };
        CHECK(!SelectRangedApproachPoint(single, P(0, 0), 27.0f, seesAll).valid);

        std::vector<ApproachPoint> path = { P(40, 0), P(0, 0) };
        CHECK(!SelectRangedApproachPoint(path, P(0, 0), 0.0f, seesAll).valid);
        std::cout << "  [PASS] degenerate inputs give no point\n";
    }

    // Partial path whose end is in range but hidden (the target on a dais,
    // the path ending under its lip): never park the bot out of sight. The
    // anchor is the last point that still sees the target, then the usual
    // walk back and exact-range point from there. The bot itself sees the
    // target (the caller's gate); only the end of the path does not.
    {
        std::vector<ApproachPoint> path = { P(40, 0), P(30, 0), P(20, 0), P(5, 0) };
        auto const hiddenEnd = [](ApproachPoint const& point) { return point.x > 10.0f; };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0), 27.0f, hiddenEnd);
        CHECK(choice.valid);
        CHECK(Near(choice.point, P(27, 0)));
        CHECK(hiddenEnd(choice.point));
        std::cout << "  [PASS] partial path, end in range but hidden: last stretch in sight\n";
    }

    // Same, with only the hidden end inside range: the bot walks to the
    // closest point that still sees the target, even though that is short of
    // range, instead of parking under the dais.
    {
        std::vector<ApproachPoint> path = { P(40, 0), P(32, 0), P(5, 0) };
        auto const hiddenEnd = [](ApproachPoint const& point) { return point.x > 10.0f; };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0), 27.0f, hiddenEnd);
        CHECK(choice.valid);
        CHECK(Near(choice.point, P(32, 0)));
        std::cout << "  [PASS] partial path, only the hidden end in range: closest point in sight\n";
    }

    // Partial path whose end is out of range and hidden: a closer point in
    // sight if there is one; none closer than the bot gives no choice, and the
    // caller falls back to the chase.
    {
        std::vector<ApproachPoint> path = { P(60, 0), P(50, 0), P(45, 0) };
        auto const hiddenEnd = [](ApproachPoint const& point) { return point.x > 47.0f; };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0), 27.0f, hiddenEnd);
        CHECK(choice.valid);
        CHECK(Near(choice.point, P(50, 0)));

        std::vector<ApproachPoint> onlyEnd = { P(60, 0), P(45, 0) };
        auto const botOnly = [](ApproachPoint const& point) { return point.x > 55.0f; };
        CHECK(!SelectRangedApproachPoint(onlyEnd, P(0, 0), 27.0f, botOnly).valid);
        std::cout << "  [PASS] partial path, end out of range and hidden: closer point in sight or none\n";
    }

    // Near-tangent crossing: the segment grazes the range sphere, so the
    // discriminant is tiny next to b*b and float error can push it below
    // zero. Clamp, don't bail: the point lands on the sphere, not the vertex.
    {
        std::vector<ApproachPoint> path = { P(80, 30), P(60, 25), P(0, 25) };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0), 25.0f, seesAll);
        CHECK(choice.valid);
        CHECK(choice.point.x > -1.0f && choice.point.x < 1.0f);
        std::cout << "  [PASS] near-tangent crossing: clamps, keeps the touch point\n";
    }

    // A bot that does not see its target gets the first point in sight and
    // range, never the target's own spot; in range but hidden still walks.
    {
        auto const pastWall = [](ApproachPoint const& point) { return point.x <= 15.0f; };
        std::vector<ApproachPoint> corner = { P(20, 10), P(15, 0), P(0, 0) };
        RangedApproachChoice choice = SelectRangedApproachPoint(corner, P(0, 0), 23.0f, pastWall, false);
        CHECK(choice.valid);
        CHECK(choice.point.x == 15.0f && choice.point.y == 0.0f);
        std::vector<ApproachPoint> straight = { P(21, 0), P(0, 0) };
        choice = SelectRangedApproachPoint(straight, P(0, 0), 23.0f, pastWall, false);
        CHECK(choice.valid);
        CHECK(choice.point.x >= 13.0f && choice.point.x <= 15.0f);
        CHECK(!SelectRangedApproachPoint(straight, P(0, 0), 23.0f, pastWall, true).valid);
        std::cout << "  [PASS] out of sight: first point in sight and range, not the target's feet\n";
    }

    // The stop band sits just inside the range rim: a bot there
    // already casts, so the walk ends before the tank instead of on it.
    {
        CHECK(RangedApproachStopDistance(26.0f) == 23.0f);
        CHECK(RangedApproachStopDistance(8.0f) == 6.0f);
        CHECK(ai::RangedReachWaitsForTarget(true, true));
        CHECK(!ai::RangedReachWaitsForTarget(false, true));
        CHECK(!ai::RangedReachWaitsForTarget(true, false));
        CHECK(RangedApproachStopDistance(0.0f) == 0.0f);
        CHECK(RangedApproachInPosition(23.0f, 26.0f, true));
        CHECK(RangedApproachInPosition(10.0f, 26.0f, true));
        CHECK(!RangedApproachInPosition(23.1f, 26.0f, true));
        CHECK(!RangedApproachInPosition(10.0f, 26.0f, false));
        std::cout << "  [PASS] stop band: in position just inside range, in sight\n";
    }

    // A destination inside an idle pack's aggro range is refused: the walk
    // stops at a closer-to-self point that still casts, or keeps the plan
    // when no such point exists.
    {
        ApproachIdlePack packs[] = { { 19.5f, 12.0f, 10.0f } };
        ApproachPoint const bot = P(40, 0);
        ApproachPoint const target = P(0, 0);
        CHECK(!ApproachPointClearOfPack(15.0f, 5.0f, bot.x, bot.y, packs[0]));
        CHECK(ApproachPointClearOfPack(19.5f, 0.0f, bot.x, bot.y, packs[0]));
        CHECK(RangedApproachPointSafe(P(19.5f, 0.0f), target, bot, 19.5f, seesAll, packs, 1));
        CHECK(!RangedApproachPointSafe(P(15.0f, 5.0f), target, bot, 19.5f, seesAll, packs, 1));
        CHECK(!RangedApproachPointSafe(P(27.0f, 0.0f), target, bot, 19.5f, seesAll, packs, 1));
        std::vector<ApproachPoint> path = { P(40, 0), P(30, 0), P(19.5f, 0.0f), P(15, 5), P(10, 0) };
        RangedApproachChoice const back = StepBackToPackSafePoint(path, P(10, 0), target, 19.5f,
            seesAll, packs, 1);
        CHECK(back.valid);
        CHECK(Near(back.point, P(19.5f, 0.0f)));
        RangedApproachChoice const none = StepBackToPackSafePoint(path, P(10, 0), target, 19.5f,
            [](ApproachPoint const&) { return false; }, packs, 1);
        CHECK(!none.valid);
        std::cout << "  [PASS] idle pack: step back to the nearest safe point in range\n";
    }

    // Onyxia in P2: the floor path ends under the hover point, the body 21 yd up.
    // The trigger allows the centre distance plus both combat reaches (19 + 22 + 1.5),
    // so the walk stops 42.5 yd centre to centre, about 36.9 yd 2D from the point.
    {
        std::vector<ApproachPoint> path = { P(-30, -212, -86), P(-10, -214, -86), P(10, -216, -86), P(22.9, -217.2, -86) };
        ApproachPoint const target = P(22.9, -217.2, -65);
        RangedApproachChoice const choice =
            ai::SelectHoverApproachPoint(path, target, 19.0f, 22.0f, 1.5f, true, seesAll);
        CHECK(choice.valid);
        CHECK(std::fabs(Dist(choice.point, target) - 42.5f) <= 0.05f);
        float const dx = choice.point.x - target.x;
        float const dy = choice.point.y - target.y;
        CHECK(std::fabs(std::sqrt(dx * dx + dy * dy) - 36.9f) <= 0.1f);
        std::cout << "  [PASS] hover: stops at the trigger's own combat-reach distance\n";
    }

    // Today's shape with the plain distance: the walk runs to the floor point
    // right under her, 21 yd below the body.
    {
        std::vector<ApproachPoint> path = { P(-30, -212, -86), P(-10, -214, -86), P(10, -216, -86), P(22.9, -217.2, -86) };
        ApproachPoint const target = P(22.9, -217.2, -65);
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, target, 19.0f, seesAll);
        CHECK(choice.valid);
        float const dx = choice.point.x - target.x;
        float const dy = choice.point.y - target.y;
        CHECK(std::sqrt(dx * dx + dy * dy) < 1.0f);
        std::cout << "  [PASS] plain distance: walks under the hover point\n";
    }

    // Only a hostile 8 yd or more above the floor it stands over, inside a dungeon,
    // beyond melee reach, with a known floor, needs the hover approach.
    {
        CHECK(ai::HoverReachApplies(true, true, true, 40.0f, 5.0f, true, -65.0f, -86.0f));
        CHECK(!ai::HoverReachApplies(true, true, true, 40.0f, 5.0f, true, -65.0f, -70.0f));  // 5 yd
        CHECK(!ai::HoverReachApplies(false, true, true, 40.0f, 5.0f, true, -65.0f, -86.0f)); // key off
        CHECK(!ai::HoverReachApplies(true, false, true, 40.0f, 5.0f, true, -65.0f, -86.0f)); // open world
        CHECK(!ai::HoverReachApplies(true, true, false, 40.0f, 5.0f, true, -65.0f, -86.0f)); // friendly
        CHECK(!ai::HoverReachApplies(true, true, true, 5.0f, 5.0f, true, -65.0f, -86.0f));   // melee reach
        CHECK(!ai::HoverReachApplies(true, true, true, 40.0f, 5.0f, false, -65.0f, -86.0f)); // no floor
        std::cout << "  [PASS] hover gate: height, key, side, distance, floor\n";
    }

    // The threshold itself counts ("at least this high").
    {
        CHECK(ai::HoverReachApplies(true, true, true, 40.0f, 5.0f, true, -65.0f, -73.0f)); // exactly 8 yd
        std::cout << "  [PASS] hover gate: exactly 8 yd counts\n";
    }

    // Only a hostile body is a hover destination, and an attack reach also needs
    // one the core lets the bot attack. A neutral unit is the case !IsFriendlyTo
    // let through; a hostile airborne one still walks.
    {
        CHECK(!ai::HoverReachTargetEligible(false, false, false)); // neutral, spell reach
        CHECK(!ai::HoverReachTargetEligible(false, true, true));   // neutral, attack reach
        CHECK(!ai::HoverReachTargetEligible(true, true, false));   // hostile, not attackable
        CHECK(ai::HoverReachTargetEligible(true, false, false));   // hostile spell reach
        CHECK(ai::HoverReachTargetEligible(true, true, true));     // hostile attack reach
        CHECK(ai::HoverReachTargetEligible(true, false, false) &&
              ai::HoverReachApplies(true, true, true, 40.0f, 5.0f, true, -65.0f, -86.0f)); // airborne hostile
        std::cout << "  [PASS] hover target: hostile only, attackable for attack reaches\n";
    }

    // The walk is for ranged damage dealers only: a healer, a tank and a melee damage
    // dealer keep today's reach (the reach path is shared with melee, CC and pull
    // reaches; walking them toward the boss could pull a tank off its post).
    {
        bool const hoverApplies = ai::HoverReachApplies(true, true, true, 40.0f, 5.0f, true, -65.0f, -86.0f);
        CHECK(hoverApplies);
        CHECK(ai::HoverReachTakerIsRangedDps(false, false, true));   // ranged DPS: the walk
        CHECK(!ai::HoverReachTakerIsRangedDps(true, false, false));  // tank
        CHECK(!ai::HoverReachTakerIsRangedDps(true, false, true));   // tank that reads ranged
        CHECK(!ai::HoverReachTakerIsRangedDps(false, true, false));  // healer
        CHECK(!ai::HoverReachTakerIsRangedDps(false, true, true));   // healer that reads ranged
        CHECK(!ai::HoverReachTakerIsRangedDps(false, false, false)); // melee DPS
        CHECK(hoverApplies && ai::HoverReachTakerIsRangedDps(false, false, true));
        CHECK(!(hoverApplies && ai::HoverReachTakerIsRangedDps(true, false, true)));
        std::cout << "  [PASS] hover taker: ranged damage dealers only\n";
    }

    // No line of sight from the ground side: the second pass keeps the geometry
    // (standing still is no damage; the next tick's trigger re-judges).
    {
        std::vector<ApproachPoint> path = { P(-30, -212, -86), P(-10, -214, -86), P(10, -216, -86), P(22.9, -217.2, -86) };
        ApproachPoint const target = P(22.9, -217.2, -65);
        auto const seesNothing = [](ApproachPoint const&) { return false; };
        CHECK(!ai::SelectHoverApproachPoint(path, target, 19.0f, 22.0f, 1.5f, true, seesNothing).valid);
        RangedApproachChoice const choice =
            ai::SelectHoverApproachPoint(path, target, 19.0f, 22.0f, 1.5f, false, seesNothing);
        CHECK(choice.valid);
        CHECK(std::fabs(Dist(choice.point, target) - 42.5f) <= 0.05f);
        std::cout << "  [PASS] hover second pass ignores sight\n";
    }

    // The row's verdict words, one per step of the walk.
    {
        CHECK(std::string(ai::HoverReachWord(ai::HoverReachResult::NotHover)) == "not_hover");
        CHECK(std::string(ai::HoverReachWord(ai::HoverReachResult::NoGround)) == "no_ground");
        CHECK(std::string(ai::HoverReachWord(ai::HoverReachResult::NoPath)) == "no_path");
        CHECK(std::string(ai::HoverReachWord(ai::HoverReachResult::NoPoint)) == "no_point");
        CHECK(std::string(ai::HoverReachWord(ai::HoverReachResult::Hazard)) == "hazard");
        CHECK(std::string(ai::HoverReachWord(ai::HoverReachResult::MoveFailed)) == "move_failed");
        CHECK(std::string(ai::HoverReachWord(ai::HoverReachResult::Moved)) == "moved");
        CHECK(std::string(ai::HoverReachWord(ai::HoverReachResult::MovedNoLos)) == "moved_nolos");
        std::cout << "  [PASS] hover words: all of them\n";
    }

    // HoverCastHoldsSpread_InRange (the failing case): all true holds the spacing step. Each of these alone
    // flips it back to stepping: out of range, a live spread row, key off, not ranged dps, not hovering.
    {
        float const clear = 40.0f;
        CHECK(ai::HoverCastHoldsSpread(true, true, true, true, false, clear, ai::kSpreadSettledDistance));
        CHECK(!ai::HoverCastHoldsSpread(true, true, true, false, false, clear, ai::kSpreadSettledDistance));
        CHECK(!ai::HoverCastHoldsSpread(true, true, true, true, true, clear, ai::kSpreadSettledDistance));
        CHECK(!ai::HoverCastHoldsSpread(false, true, true, true, false, clear, ai::kSpreadSettledDistance));
        CHECK(!ai::HoverCastHoldsSpread(true, false, true, true, false, clear, ai::kSpreadSettledDistance));
        CHECK(!ai::HoverCastHoldsSpread(true, true, false, true, false, clear, ai::kSpreadSettledDistance));
        std::cout << "  [PASS] hover cast: holds the spread step in range\n";
    }

    // HoverCastHoldsSpread_StackedAlly (the failing case): the hold yields to spreading out of a stack. A living
    // group member inside the spread radius (2 yd of the 10 yd settled distance) means the caster steps out of
    // the splash first; a caster clear of the nearest ally (20 yd) holds and casts; no hover never holds.
    {
        float const radius = ai::kSpreadSettledDistance;
        CHECK(!ai::HoverCastHoldsSpread(true, true, true, true, false, 2.0f, radius));
        CHECK(ai::HoverCastHoldsSpread(true, true, true, true, false, 20.0f, radius));
        CHECK(!ai::HoverCastHoldsSpread(true, true, false, true, false, 20.0f, radius));
        std::cout << "  [PASS] hover cast: a stacked ally outranks the hold\n";
    }


    std::cout << "Ranged approach policy tests: OK\n";
    return 0;
}
