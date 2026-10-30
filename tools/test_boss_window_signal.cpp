// Standalone tests for runtime/BossWindowSignal.h and the committed rows of runtime/BossWindowData.h: the dashboard's
// breath window. A window opens when the boss wears the window aura, is stopped and has no target near one of its hover
// points, and reports that point's breath spell and lane; it closes when its time has run; a boss without rows never
// opens. The countdown is per map, instance and unit.
//
// Standard library only, no game header.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -Werror=return-type tools/test_boss_window_signal.cpp -o /tmp/test_boss_window_signal
//   /tmp/test_boss_window_signal

#include "../runtime/BossWindowSignal.h"
#include "../runtime/SpellShape.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <unordered_set>

using namespace TortoiseBots::BossWindow;

namespace {

int g_failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

constexpr uint32_t kMap = 249;
constexpr uint32_t kOnyxia = 10184;
constexpr uint32_t kHover = 17131;

auto const kWearsHover = [](uint32_t aura) { return aura == kHover; };
auto const kNoAura = [](uint32_t) { return false; };

// Onyxia in her breath state at (x, y): stopped, no target.
Facts Breathing(float x, float y)
{
    Facts facts;
    facts.map = kMap;
    facts.entry = kOnyxia;
    facts.x = x;
    facts.y = y;
    facts.stopped = true;
    facts.hasTarget = false;
    return facts;
}

BreathPoint const& Row(uint32_t point)
{
    for (BreathPoint const& row : kBreathPoints)
        if (row.point == point)
            return row;
    std::fprintf(stderr, "no row for point %u\n", point);
    std::exit(EXIT_FAILURE);
}

float Dist(float ax, float ay, float bx, float by)
{
    return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
}

// The committed rows are boss_onyxia.cpp's aMoveData: 8 points, each its own breath spell, each ending at the opposite
// point (point + 4 mod 8, DoMovement's Deep Breath roll), 5000 ms (m_uiDeepBreathTimer), the Hover aura.
void TestCommittedRows()
{
    Check(std::size(kBreathPoints) == 8, "Onyxia has 8 hover points");
    std::unordered_set<uint32_t> points;
    std::unordered_set<uint32_t> spells;
    for (BreathPoint const& row : kBreathPoints)
    {
        Check(row.map == kMap && row.bossEntry == kOnyxia, "every row is Onyxia's, in her lair");
        Check(row.windowAura == kHover, "every row opens on the Hover aura");
        Check(row.windowMs == 5000, "every window is the script's 5 s breath");
        Check(row.radius > 0.0f && row.radius < 20.0f, "a point's radius is small");
        Check(points.insert(row.point).second, "a point appears once");
        Check(spells.insert(row.spell).second, "a breath spell belongs to one point");
        Check(row.endPoint == (row.point + 4) % 8, "a breath ends at the opposite point");
    }
    Check(Row(7).spell == 17086 && Row(3).spell == 18351, "north breathes north-to-south, south south-to-north");
    Check(Row(0).spell == 18617 && Row(4).spell == 18596, "north-east breathes NE-to-SW, south-west SW-to-NE");
}

// Every row's lane is its spell's line in the shape table, and it starts at the row's point and runs towards its end
// point: the lane the dashboard draws is the one the boss breathes from where she stands.
void TestEveryPointOpensWithItsLane()
{
    for (BreathPoint const& row : kBreathPoints)
    {
        BreathPoint const* open = OpenPoint(Breathing(row.x, row.y), kWearsHover);
        Check(open == &row, "a boss in the breath state at a hover point opens that point's row");
        if (open != &row)
            continue;
        TortoiseBots::SpellShape::Shape const* lane = TortoiseBots::SpellShape::For(open->spell);
        Check(lane && lane->kind == TortoiseBots::SpellShape::Kind::Line && lane->hasSegment,
            "every breath spell has a lane in the shape table");
        if (!lane || !lane->hasSegment)
            continue;
        BreathPoint const& end = Row(open->endPoint);
        Check(Dist(lane->x0, lane->y0, row.x, row.y) < 12.0f, "the lane starts at the boss's hover point");
        float const dot = (lane->x1 - lane->x0) * (end.x - row.x) + (lane->y1 - lane->y0) * (end.y - row.y);
        Check(dot > 0.0f, "the lane runs towards the opposite point");
    }
}

// Each fact the window needs, taken away alone, keeps it closed.
void TestEveryFactIsNeeded()
{
    BreathPoint const& north = Row(7);
    Check(OpenPoint(Breathing(north.x, north.y), kWearsHover) != nullptr, "the full breath state opens");
    Check(OpenPoint(Breathing(north.x, north.y), kNoAura) == nullptr, "without the Hover aura it stays closed");
    Facts moving = Breathing(north.x, north.y);
    moving.stopped = false;
    Check(OpenPoint(moving, kWearsHover) == nullptr, "a moving boss is not breathing");
    Facts targeting = Breathing(north.x, north.y);
    targeting.hasTarget = true;
    Check(OpenPoint(targeting, kWearsHover) == nullptr, "a boss with a target is not breathing");
    Check(OpenPoint(Breathing(north.x + north.radius + 0.5f, north.y), kWearsHover) == nullptr,
        "away from every hover point it stays closed");
    Check(OpenPoint(Breathing(north.x + north.radius - 0.1f, north.y), kWearsHover) == &north,
        "just inside the radius opens");
    Check(OpenPoint(Breathing(-30.0f, -215.0f), kWearsHover) == nullptr, "the room's centre is no hover point");
}

// The failing case the policy exists to rule out: a boss the table does not name (another entry, or Onyxia's entry on
// another map) at Onyxia's own point, in the same state, never opens.
void TestBossWithoutRowsNeverOpens()
{
    BreathPoint const& north = Row(7);
    Facts other = Breathing(north.x, north.y);
    other.entry = 11583; // Nefarian
    Check(OpenPoint(other, kWearsHover) == nullptr, "a boss without rows never opens");
    Facts elsewhere = Breathing(north.x, north.y);
    elsewhere.map = 469;
    Check(OpenPoint(elsewhere, kWearsHover) == nullptr, "Onyxia's entry on another map has no rows");
    Check(OpenPoint(nullptr, 0, Breathing(north.x, north.y), kWearsHover) == nullptr, "an empty table never opens");
}

// Two points whose radii overlap: the nearer one wins, whichever comes first in the table.
void TestNearestPointWins()
{
    BreathPoint const rows[] = {
        {kMap, kOnyxia, kHover, 0, 0.0f, 0.0f, 10.0f, 111, 1, 5000},
        {kMap, kOnyxia, kHover, 1, 6.0f, 0.0f, 10.0f, 222, 0, 5000},
    };
    BreathPoint const* near1 = OpenPoint(rows, 2, Breathing(4.0f, 0.0f), kWearsHover);
    Check(near1 && near1->spell == 222, "the nearer point's spell is picked even when it comes second");
    BreathPoint const* near0 = OpenPoint(rows, 2, Breathing(1.0f, 0.0f), kWearsHover);
    Check(near0 && near0->spell == 111, "and the first when it is nearer");
    BreathPoint const* edge = OpenPoint(rows, 1, Breathing(-10.0f, 0.0f), kWearsHover);
    Check(edge && edge->spell == 111, "on the radius counts");
}

// The countdown runs from the first sighting and the window closes when its time has run, then stays closed while the
// state lasts; the state ending (Forget) or a gap longer than a window starts a new one.
void TestCountdownClosesAfterTheCastTime()
{
    Countdowns marks;
    Key const key{kMap, 7, 0xF130002798000001ull};
    uint32_t left = 0;
    Check(marks.Left(key, 1000, 5000, left) && left == 5000, "the first sighting opens a full window");
    Check(marks.Left(key, 3000, 5000, left) && left == 3000, "two seconds in, three are left");
    Check(marks.Left(key, 5999, 5000, left) && left == 1, "a millisecond before the end it is still open");
    Check(!marks.Left(key, 6000, 5000, left), "the window closes after the cast time");
    Check(!marks.Left(key, 8000, 5000, left), "and stays closed while the state lasts");
    marks.Forget(key);
    Check(!marks.Contains(key), "the state ending forgets the mark");
    Check(marks.Left(key, 9000, 5000, left) && left == 5000, "the next breath starts a new window");
    Check(marks.Left(key, 20000, 5000, left) && left == 5000, "a sighting after a gap longer than a window is new");
}

// A creature's raw guid repeats across copies of a map: two instances count down apart.
void TestCountdownPerInstance()
{
    Countdowns marks;
    uint64_t const guid = 0xF130002798000001ull;
    Key const first{kMap, 1, guid};
    Key const second{kMap, 2, guid};
    Check(!(first == second) && !(first == Key{kMap + 1, 1, guid}) && !(first == Key{kMap, 1, guid + 1}),
        "a key is the map, the instance and the guid together");
    Check(first == Key{kMap, 1, guid}, "the same unit in the same instance is the same key");
    uint32_t left = 0;
    Check(marks.Left(first, 1000, 5000, left) && left == 5000, "the first copy opens");
    Check(marks.Left(second, 3000, 5000, left) && left == 5000, "the second copy opens its own window");
    Check(marks.Left(first, 3000, 5000, left) && left == 3000, "the first copy keeps its own count");
    Check(marks.Size() == 2, "two marks");
    marks.Prune(3000 + kPruneMs);
    Check(marks.Size() == 2, "a mark seen exactly kPruneMs ago is kept");
    marks.Prune(3001 + kPruneMs);
    Check(marks.Size() == 0, "marks nobody saw for longer than kPruneMs are dropped");
}

} // namespace

int main()
{
    TestCommittedRows();
    TestEveryPointOpensWithItsLane();
    TestEveryFactIsNeeded();
    TestBossWithoutRowsNeverOpens();
    TestNearestPointWins();
    TestCountdownClosesAfterTheCastTime();
    TestCountdownPerInstance();
    if (g_failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::puts("test_boss_window_signal: all checks passed");
    return EXIT_SUCCESS;
}
