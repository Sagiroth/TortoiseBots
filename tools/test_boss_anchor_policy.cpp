// Standalone check for ai/playerbot/BossAnchorPolicy.h and the `anchor` kind of ai/playerbot/BossTacticsPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_boss_anchor_policy.cpp -o /tmp/test_boss_anchor_policy && /tmp/test_boss_anchor_policy
#include "../ai/playerbot/BossAnchorPolicy.h"
#include "../ai/playerbot/BossTacticsPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

static bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

// Hakkar's spawn (tw_world creature guid 49678) and the shipped radii: 30 yd for everyone, 25 yd for the tanks.
static BossAnchorCircle Hakkar(bool tank)
{
    BossAnchorCircle c;
    c.x = -11787.4f;
    c.y = -1649.34f;
    c.z = 54.1f;
    c.radius = BossAnchorRadius(30.0f, 25.0f, tank);
    return c;
}

static float Dist2d(const BossAnchorCircle& c, float x, float y)
{
    return std::sqrt((x - c.x) * (x - c.x) + (y - c.y) * (y - c.y));
}

static void TestInsideUnchanged()
{
    const BossAnchorCircle c = Hakkar(false);
    float x = c.x + 10.0f;
    float y = c.y - 15.0f;
    CHECK(InsideAnchor(c, x, y));
    CHECK(!ClampToAnchor(c, x, y));
    CHECK(Near(x, c.x + 10.0f) && Near(y, c.y - 15.0f));
    // The centre itself and a point right on the circle stay where they are.
    float cx = c.x, cy = c.y;
    CHECK(!ClampToAnchor(c, cx, cy));
    float ex = c.x + 30.0f, ey = c.y;
    CHECK(InsideAnchor(c, ex, ey));
    CHECK(!ClampToAnchor(c, ex, ey));
    CHECK(Near(ex, c.x + 30.0f));
}

static void TestOutsideProjectedOntoCircle()
{
    const BossAnchorCircle c = Hakkar(false);
    // The authored seek spot of 20261104020000 (-11802, -1683): 37 yd SW of his spawn.
    float x = -11802.0f;
    float y = -1683.0f;
    CHECK(!InsideAnchor(c, x, y));
    const float before = Dist2d(c, x, y);
    CHECK(before > 36.0f);
    const float dirX = (x - c.x) / before;
    const float dirY = (y - c.y) / before;
    CHECK(ClampToAnchor(c, x, y));
    CHECK(Near(Dist2d(c, x, y), 30.0f));
    // On the line from the centre toward the destination.
    CHECK(Near(x, c.x + dirX * 30.0f));
    CHECK(Near(y, c.y + dirY * 30.0f));
    CHECK(InsideAnchor(c, x, y));
    // Far out along an axis.
    float fx = c.x - 80.0f, fy = c.y;
    CHECK(ClampToAnchor(c, fx, fy));
    CHECK(Near(fx, c.x - 30.0f) && Near(fy, c.y));
}

static void TestTankRadius25()
{
    const BossAnchorCircle tank = Hakkar(true);
    const BossAnchorCircle other = Hakkar(false);
    CHECK(Near(tank.radius, 25.0f));
    CHECK(Near(other.radius, 30.0f));
    // 28 yd out: inside for a healer, clamped to 25 for a tank.
    float hx = other.x, hy = other.y + 28.0f;
    CHECK(!ClampToAnchor(other, hx, hy));
    float tx = tank.x, ty = tank.y + 28.0f;
    CHECK(ClampToAnchor(tank, tx, ty));
    CHECK(Near(tx, tank.x) && Near(ty, tank.y + 25.0f));
    // A row without a tank radius (0) holds the tanks at the shared radius.
    CHECK(Near(BossAnchorRadius(30.0f, 0.0f, true), 30.0f));
    CHECK(Near(BossAnchorRadius(30.0f, 25.0f, false), 30.0f));
}

static void TestFloor()
{
    const BossAnchorCircle c = Hakkar(false);
    CHECK(AnchorFloorOk(c, true, 54.0f));
    CHECK(AnchorFloorOk(c, true, 50.2f));
    // The SW slope (z 47 / 44 / 41) is off the platform: the move is skipped.
    CHECK(!AnchorFloorOk(c, true, 47.0f));
    CHECK(!AnchorFloorOk(c, true, 41.0f));
    // No floor read at all.
    CHECK(!AnchorFloorOk(c, false, 54.0f));
}

static void TestSeekPullSonSelection()
{
    // The third off-tank is a tank: its own radius (25) plus the hit reach (5).
    const BossAnchorCircle c = Hakkar(true);
    // The walk 42 Son 49034 (-11842.9, -1594.18, z 40.75): 78 yd NW and 13 yd lower. Skipped.
    CHECK(!SeekPullSonInAnchor(c, -11842.9f, -1594.18f, 40.75f));
    // Inside the radius on the platform: picked.
    CHECK(SeekPullSonInAnchor(c, c.x + 10.0f, c.y - 12.0f, 54.0f));
    // Inside radius + 5 yd: picked (the clamped walk ends within the hit reach).
    CHECK(SeekPullSonInAnchor(c, c.x, c.y + 29.5f, 54.0f));
    // Outside radius + 5 yd: skipped.
    CHECK(!SeekPullSonInAnchor(c, c.x, c.y + 30.5f, 54.0f));
    // Inside the radius but more than 4 yd below the centre: skipped.
    CHECK(!SeekPullSonInAnchor(c, c.x + 5.0f, c.y + 5.0f, 49.9f));
    CHECK(SeekPullSonInAnchor(c, c.x + 5.0f, c.y + 5.0f, 50.2f));
    // Above too.
    CHECK(!SeekPullSonInAnchor(c, c.x + 5.0f, c.y + 5.0f, 58.3f));
}

static void TestSeekPullEdgeHasNoSlack()
{
    // The pick reach is exactly the puller's radius plus the hit reach: a Son a hair past it is not picked, so the clamped
    // walk (ending on the circle) always ends within the hit reach of a picked Son.
    const BossAnchorCircle c = Hakkar(true);
    CHECK(SeekPullSonInAnchor(c, c.x, c.y + 29.99f, 54.0f));
    CHECK(!SeekPullSonInAnchor(c, c.x, c.y + 30.02f, 54.0f));
}

static void TestRangedShotFromAnchor()
{
    // A Son 50 yd out and 13 yd lower: no tank walks to it, but a shooter at the circle's point toward it (30 yd out,
    // 20 yd from the Son) reaches it with a 26 yd spell range, and the z rule does not apply to the shot.
    const BossAnchorCircle tank = Hakkar(true);
    const BossAnchorCircle shooter = Hakkar(false);
    const float sx = shooter.x - 50.0f;
    const float sy = shooter.y;
    const float sz = shooter.z - 13.0f;
    CHECK(!SeekPullSonInAnchor(tank, sx, sy, sz));
    float standX = 0.0f, standY = 0.0f;
    CHECK(AnchorRangedShotStand(shooter, sx, sy, 26.0f, standX, standY));
    CHECK(Near(standX, shooter.x - 30.0f) && Near(standY, shooter.y));
    CHECK(AnchorSeekPull(tank, sx, sy, sz, true, true) == AnchorPull::RangedFromAnchor);
    // Without sight from the stand, or out of the shooter's range, the cycle is skipped.
    CHECK(AnchorSeekPull(tank, sx, sy, sz, true, false) == AnchorPull::Skip);
    CHECK(!AnchorRangedShotStand(shooter, shooter.x - 60.0f, shooter.y, 26.0f, standX, standY));
    CHECK(AnchorSeekPull(tank, sx, sy, sz, false, true) == AnchorPull::Skip);
    // A Son the tank may walk to stays the tank's walk.
    CHECK(AnchorSeekPull(tank, tank.x + 10.0f, tank.y, 54.0f, true, true) == AnchorPull::TankWalk);
    // A Son already inside the shooter's circle is shot from where it stands (the stand is the Son's own point).
    CHECK(AnchorRangedShotStand(shooter, shooter.x + 28.0f, shooter.y, 26.0f, standX, standY));
    CHECK(Near(standX, shooter.x + 28.0f));
}

static void TestCloudOutsideAnchorIgnored()
{
    const BossAnchorCircle c = Hakkar(false);
    // An 8 yd cloud whose edge reaches the circle is still taken.
    CHECK(AnchorReachesCloud(c, c.x + 37.0f, c.y, 8.0f));
    CHECK(AnchorReachesCloud(c, c.x + 10.0f, c.y, 8.0f));
    // One whose nearest edge lies outside the circle is ignored: the seeker would stick at the edge.
    CHECK(!AnchorReachesCloud(c, c.x + 38.5f, c.y, 8.0f));
}

static void TestChaseRefusedOutsideReach()
{
    // A chase may go to a hostile within the radius plus the melee reach of the centre; one farther out is refused (the
    // bot holds and its rotation or the target logic answers).
    const BossAnchorCircle c = Hakkar(false);
    CHECK(AnchorAllowsChase(c, c.x + 10.0f, c.y, kBossAnchorChaseReachYd));
    CHECK(AnchorAllowsChase(c, c.x + 34.9f, c.y, kBossAnchorChaseReachYd));
    CHECK(!AnchorAllowsChase(c, c.x + 35.1f, c.y, kBossAnchorChaseReachYd));
    // The walk 42 Son 49034, 78 yd NW.
    CHECK(!AnchorAllowsChase(c, -11842.9f, -1594.18f, kBossAnchorChaseReachYd));
    // Tanks hold the tighter circle: 25 + 5.
    const BossAnchorCircle tank = Hakkar(true);
    CHECK(!AnchorAllowsChase(tank, tank.x + 31.0f, tank.y, kBossAnchorChaseReachYd));
    CHECK(AnchorAllowsChase(tank, tank.x + 29.0f, tank.y, kBossAnchorChaseReachYd));
}

static void TestHazardDodgeNeverLeavesTheAnchor()
{
    // A 5 yd hazard on the circle's edge (28 yd E of the centre) and a chase end point beside it: the perpendicular
    // dodge (1.5 x its range sideways) lands outside the circle. Under the anchor it is clamped back inside when the
    // clamped point is still clear of the hazard, else the move is refused; it never ends outside.
    const BossAnchorCircle c = Hakkar(false);
    {
        float x = c.x + 33.0f, y = c.y + 7.5f;  // the dodge, 33.8 yd out
        CHECK(!InsideAnchor(c, x, y));
        CHECK(AnchorHazardDodge(c, c.x + 28.0f, c.y, 5.0f, x, y));
        CHECK(InsideAnchor(c, x, y));
        CHECK(std::sqrt((x - c.x - 28.0f) * (x - c.x - 28.0f) + (y - c.y) * (y - c.y)) > 5.0f);
    }
    {
        // A dodge whose clamp falls back into the hazard: refused.
        float x = c.x + 34.0f, y = c.y + 1.0f;
        CHECK(!AnchorHazardDodge(c, c.x + 28.0f, c.y, 5.0f, x, y));
    }
    {
        // A dodge inside the circle and clear of the hazard is kept as it is.
        float x = c.x + 20.0f, y = c.y + 8.0f;
        CHECK(AnchorHazardDodge(c, c.x + 28.0f, c.y, 5.0f, x, y));
        CHECK(Near(x, c.x + 20.0f) && Near(y, c.y + 8.0f));
    }
}

static void TestDodgePickTriesBothSides()
{
    // Hazard 5 yd on the circle's edge, 28 yd E of the centre. The glue's accept stands for its floor, hazard and
    // navigation checks on the clamped point.
    const BossAnchorCircle c = Hakkar(false);
    const float hx = c.x + 28.0f, hy = c.y;
    float x = 0.0f, y = 0.0f;
    // Left lands outside and its clamp falls back into the hazard; right stays clear: right is used.
    {
        const float xs[2] = {c.x + 34.0f, c.x + 22.0f};
        const float ys[2] = {c.y + 1.0f, c.y - 6.0f};
        CHECK(AnchorPickDodge(c, hx, hy, 5.0f, xs, ys, x, y, [](float, float) { return true; }) == 1);
        CHECK(Near(x, c.x + 22.0f) && Near(y, c.y - 6.0f));
    }
    // Left clamps clear but the accept refuses it (no floor there); right passes: right is used.
    {
        const float xs[2] = {c.x + 33.0f, c.x + 22.0f};
        const float ys[2] = {c.y + 7.5f, c.y - 6.0f};
        const float leftClampedY = c.y + 7.5f * 30.0f / std::sqrt(33.0f * 33.0f + 7.5f * 7.5f);
        auto const noFloorLeft = [&](float, float py) { return !Near(py, leftClampedY); };
        CHECK(AnchorPickDodge(c, hx, hy, 5.0f, xs, ys, x, y, noFloorLeft) == 1);
        CHECK(Near(x, c.x + 22.0f));
    }
    // Left passes: left is used, right never asked.
    {
        const float xs[2] = {c.x + 33.0f, c.x + 22.0f};
        const float ys[2] = {c.y + 7.5f, c.y - 6.0f};
        int asked = 0;
        CHECK(AnchorPickDodge(c, hx, hy, 5.0f, xs, ys, x, y, [&](float, float) { ++asked; return true; }) == 0);
        CHECK(asked == 1);
        CHECK(InsideAnchor(c, x, y));
    }
    // Both fail: refused.
    {
        const float xs[2] = {c.x + 34.0f, c.x + 22.0f};
        const float ys[2] = {c.y + 1.0f, c.y - 6.0f};
        CHECK(AnchorPickDodge(c, hx, hy, 5.0f, xs, ys, x, y, [](float, float) { return false; }) == -1);
    }
}

static void TestPathInsideAnchor()
{
    const BossAnchorCircle c = Hakkar(false);
    std::vector<std::pair<float, float>> path = {{c.x, c.y}, {c.x + 10.0f, c.y}, {c.x + 25.0f, c.y + 5.0f}};
    CHECK(AnchorPathInside(c, path));
    // A hazard-avoiding point routed outside the circle: the path is refused.
    path.insert(path.begin() + 2, {c.x + 31.0f, c.y + 2.0f});
    CHECK(!AnchorPathInside(c, path));
    CHECK(AnchorPathInside(c, {}));
}

static BossTacticRow AnchorRow()
{
    BossTacticRow row;
    row.map = 309;
    row.bossEntry = 14834;
    row.kind = BossTacticKind::Anchor;
    row.param = 30.0f;
    row.priority = 25;
    row.windowSec = 0.0f;
    row.spotX = -11787.4f;
    row.spotY = -1649.34f;
    row.spotZ = 54.1f;
    return row;
}

static void TestAnchorKindParsesAndLoads()
{
    CHECK(ParseBossTacticKind("anchor") == BossTacticKind::Anchor);
    CHECK(ParseTacticMode(BossTacticKind::Anchor, "") == TacticMode::Default);
    CHECK(ParseTacticMode(BossTacticKind::Anchor, "all") == TacticMode::Invalid);
    BossTacticTable table;
    CHECK(table.Add(AnchorRow()));
    table.Seal();
    CHECK(table.HasAnchorMap(309));
    CHECK(!table.HasAnchorMap(409));
    const std::vector<BossTacticRow>* rows = table.Rows(309, 14834);
    CHECK(rows && rows->size() == 1);
    const BossAnchorCircle tank = AnchorCircleOf(rows->front(), true);
    const BossAnchorCircle other = AnchorCircleOf(rows->front(), false);
    CHECK(Near(tank.radius, 25.0f) && Near(other.radius, 30.0f));
    CHECK(Near(other.x, -11787.4f) && Near(other.y, -1649.34f) && Near(other.z, 54.1f));
    // A table without an anchor row says so (data-gated: nothing is clamped).
    BossTacticTable empty;
    empty.Seal();
    CHECK(!empty.HasAnchorMap(309));
}

static void TestAnchorRowShapeRefused()
{
    {
        BossTacticTable t;
        BossTacticRow row = AnchorRow();
        row.spotX = row.spotY = row.spotZ = 0.0f;   // no centre
        CHECK(!t.Add(row));
    }
    {
        BossTacticTable t;
        BossTacticRow row = AnchorRow();
        row.param = 0.0f;   // no radius
        CHECK(!t.Add(row));
    }
    {
        BossTacticTable t;
        BossTacticRow row = AnchorRow();
        row.param = 80.0f;  // a radius past 60 yd anchors nothing
        CHECK(!t.Add(row));
    }
    {
        BossTacticTable t;
        BossTacticRow row = AnchorRow();
        row.priority = 40;  // the tank radius cannot be wider than everyone's
        CHECK(!t.Add(row));
    }
    {
        BossTacticTable t;
        BossTacticRow row = AnchorRow();
        row.targetEntry = 11357;
        CHECK(!t.Add(row));
    }
    {
        BossTacticTable t;
        BossTacticRow row = AnchorRow();
        row.spellId = 24321;
        CHECK(!t.Add(row));
    }
    {
        BossTacticTable t;
        BossTacticRow row = AnchorRow();
        row.hpMin = 20;     // no gate: the anchor holds the whole fight
        CHECK(!t.Add(row));
    }
    {
        BossTacticTable t;
        BossTacticRow row = AnchorRow();
        row.cycleSpell = 24324;
        CHECK(!t.Add(row));
    }
    {
        BossTacticTable t;
        BossTacticRow row = AnchorRow();
        row.priority = 0;   // no tank radius: the tanks take the shared one
        CHECK(t.Add(row));
        t.Seal();
        CHECK(Near(AnchorCircleOf(t.Rows(309, 14834)->front(), true).radius, 30.0f));
    }
    {
        // The spot columns stay the seek_aura and anchor kinds' own.
        BossTacticTable t;
        BossTacticRow row = AnchorRow();
        row.kind = BossTacticKind::Spread;
        row.param = 12.0f;
        row.priority = 0;
        CHECK(!t.Add(row));
    }
}

int main()
{
    TestInsideUnchanged();
    TestOutsideProjectedOntoCircle();
    TestTankRadius25();
    TestFloor();
    TestSeekPullSonSelection();
    TestSeekPullEdgeHasNoSlack();
    TestRangedShotFromAnchor();
    TestCloudOutsideAnchorIgnored();
    TestChaseRefusedOutsideReach();
    TestHazardDodgeNeverLeavesTheAnchor();
    TestPathInsideAnchor();
    TestDodgePickTriesBothSides();
    TestAnchorKindParsesAndLoads();
    TestAnchorRowShapeRefused();
    std::cout << "boss anchor policy: all checks passed\n";
    return 0;
}
