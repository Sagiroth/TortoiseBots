#include "../ai/playerbot/LedgeStepDownPolicy.h"

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

using ai::IsLedgeStepDownPoint;
using ai::LedgeEdgePoint;
using ai::LedgeLandingOnMesh;
using ai::LedgePathProbe;
using ai::LedgePoint;
using ai::LedgeProbeResult;
using ai::LedgeStepDownCandidates;
using ai::LedgeStepDownChoice;
using ai::LedgeStepDownClock;
using ai::SelectLedgeStepDownPoint;
using ai::UseLedgeStepDown;

static LedgePoint P(float x, float y, float z = 0.0f)
{
    LedgePoint point;
    point.x = x;
    point.y = y;
    point.z = z;
    return point;
}

static bool Near(LedgePoint const& a, LedgePoint const& b)
{
    float const dx = a.x - b.x;
    float const dy = a.y - b.y;
    float const dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz) < 0.01f;
}

// Uldaman's Hall of the Keepers, simplified: a dais 8 x 8 yd centred on the
// bot, its top at -45.0, the floor around it at -48.7 (3.7 yd lower).
static float const kDaisTop = -45.0f;
static float const kFloor = -48.7f;

static bool DaisGround(float x, float y, float /*z*/, float& outZ)
{
    outZ = (std::fabs(x) <= 4.0f && std::fabs(y) <= 4.0f) ? kDaisTop : kFloor;
    return true;
}

int main()
{
    std::cout << "Starting TortoiseBots ledge step-down policy tests...\n";

    auto const safeAll = [](LedgePoint const&) { return true; };
    auto const clearAll = [](LedgePoint const&, LedgePoint const&) { return true; };
    // A complete path that starts on the landing point itself.
    auto const pathsAll = [](LedgePoint const& point)
    {
        LedgePathProbe probe;
        probe.normal = true;
        probe.hasStart = true;
        probe.start = point;
        return probe;
    };
    LedgePoint const bot = P(0, 0, kDaisTop);
    LedgePoint const rockchewer = P(12, 0, kFloor);

    // Gate: dungeon only, partial path only, no headway only, never while hit.
    {
        CHECK(UseLedgeStepDown(true, true, 1.0f, false));
        CHECK(UseLedgeStepDown(true, true, 2.0f, false));
        CHECK(!UseLedgeStepDown(false, true, 1.0f, false));   // open world
        CHECK(!UseLedgeStepDown(true, false, 1.0f, false));   // complete path
        CHECK(!UseLedgeStepDown(true, true, 6.0f, false));    // partial path that still walks somewhere
        CHECK(!UseLedgeStepDown(true, true, 1.0f, true));     // something is hitting the bot
        std::cout << "  [PASS] gate: dungeon + partial path at the feet + not being hit\n";
    }

    // Height band 2-8 yd below the bot and horizontal radius up to ~8 yd.
    {
        CHECK(IsLedgeStepDownPoint(bot, P(8, 0, kDaisTop - 2.0f)));
        CHECK(IsLedgeStepDownPoint(bot, P(8, 0, kDaisTop - 8.0f)));
        CHECK(IsLedgeStepDownPoint(bot, P(8, 0, kFloor)));
        CHECK(!IsLedgeStepDownPoint(bot, P(8, 0, kDaisTop - 1.9f)));  // a step the navmesh climbs, or the dais itself
        CHECK(!IsLedgeStepDownPoint(bot, P(8, 0, kDaisTop - 8.1f)));  // a fall, not a step
        CHECK(!IsLedgeStepDownPoint(bot, P(8, 0, kDaisTop + 3.0f)));  // up is never a step down
        CHECK(IsLedgeStepDownPoint(bot, P(8.4f, 0, kFloor)));
        CHECK(!IsLedgeStepDownPoint(bot, P(9.0f, 0, kFloor)));        // too far out
        CHECK(!IsLedgeStepDownPoint(bot, P(0.5f, 0, kFloor)));        // straight down under the feet
        std::cout << "  [PASS] height band 2-8 yd lower, horizontal radius about 8 yd\n";
    }

    // Candidates: towards the target first, then fanning out; two rings.
    {
        std::vector<LedgePoint> const candidates = LedgeStepDownCandidates(bot, rockchewer);
        CHECK(candidates.size() == 14);
        CHECK(Near(candidates[0], P(8, 0, kDaisTop)));
        CHECK(Near(candidates[7], P(5, 0, kDaisTop)));
        for (LedgePoint const& candidate : candidates)
        {
            float const r = std::sqrt(candidate.x * candidate.x + candidate.y * candidate.y);
            CHECK(std::fabs(r - 8.0f) < 0.01f || std::fabs(r - 5.0f) < 0.01f);
            CHECK(candidate.x > -0.01f); // never behind the bot (fan stops at 90 degrees)
        }
        std::cout << "  [PASS] candidates: towards the target, +-30/60/90 degrees, 8 then 5 yd\n";
    }

    // The Uldaman dais: the point straight towards the Rockchewer, on the floor.
    {
        LedgeStepDownChoice const choice = SelectLedgeStepDownPoint(bot, rockchewer, DaisGround, safeAll, clearAll, pathsAll);
        CHECK(choice.valid);
        CHECK(Near(choice.point, P(8, 0, kFloor)));
        // Out over the edge at the dais height first: the lip is at 4 yd, the
        // edge point half a yard past the first floor sample (4.5 yd).
        CHECK(Near(choice.edge, P(5, 0, kDaisTop)));
        std::cout << "  [PASS] dais: steps down straight towards the target\n";
    }

    // Needs-path rule: floor without a complete path to the target (another
    // island, a pit) is skipped; the first floor point with one is taken.
    {
        auto const pathOnlyLeft = [&pathsAll](LedgePoint const& point)
        {
            LedgePathProbe probe = pathsAll(point);
            probe.normal = point.y > 1.0f;
            return probe;
        };
        LedgeStepDownChoice const choice = SelectLedgeStepDownPoint(bot, rockchewer, DaisGround, safeAll, clearAll, pathOnlyLeft);
        CHECK(choice.valid);
        CHECK(choice.point.y > 1.0f);
        CHECK(std::fabs(choice.point.z - kFloor) < 0.01f);
        float const angle = std::atan2(choice.point.y, choice.point.x);
        CHECK(std::fabs(angle - 3.14159265f / 6.0f) < 0.01f); // the +30 degree point, outer ring

        auto const noPath = [](LedgePoint const&) { return LedgePathProbe(); };
        CHECK(!SelectLedgeStepDownPoint(bot, rockchewer, DaisGround, safeAll, clearAll, noPath).valid);
        std::cout << "  [PASS] needs a complete path from the point to the target\n";
    }

    // On the navmesh: a NORMAL path whose start the core snapped away from the
    // point (a lava pool or a hole next to the floor, the shore polygon up to
    // 5 yd away) does not count; the next candidate whose path starts on it
    // does.
    {
        CHECK(LedgeLandingOnMesh(P(8, 0, kFloor), P(8, 0, kFloor)));
        CHECK(LedgeLandingOnMesh(P(8, 0, kFloor), P(8.9f, 0, kFloor + 1.4f)));
        CHECK(!LedgeLandingOnMesh(P(8, 0, kFloor), P(9.2f, 0, kFloor)));        // snapped sideways
        CHECK(!LedgeLandingOnMesh(P(8, 0, kFloor), P(8, 0, kFloor + 2.0f)));    // snapped up out of a pit

        auto const snappedAhead = [](LedgePoint const& point)
        {
            LedgePathProbe probe;
            probe.normal = true;
            probe.hasStart = true;
            probe.start = point;
            if (point.y > -1.0f && point.y < 1.0f)
                probe.start.x += 4.0f; // the point straight ahead is in a pool
            return probe;
        };
        LedgeStepDownChoice const choice = SelectLedgeStepDownPoint(bot, rockchewer, DaisGround, safeAll, clearAll, snappedAhead);
        CHECK(choice.valid);
        CHECK(choice.point.y > 1.0f); // the +30 degree point

        auto const noStart = [](LedgePoint const&)
        {
            LedgePathProbe probe;
            probe.normal = true; // complete, but no points: nothing proves where it starts
            return probe;
        };
        CHECK(!SelectLedgeStepDownPoint(bot, rockchewer, DaisGround, safeAll, clearAll, noStart).valid);
        std::cout << "  [PASS] NORMAL path but the point is off the mesh: next candidate\n";
    }

    // Lava, slime or a hazard at a point rejects that point only.
    {
        auto const lavaAhead = [](LedgePoint const& point) { return !(point.y > -1.0f && point.y < 1.0f); };
        LedgeStepDownChoice const choice = SelectLedgeStepDownPoint(bot, rockchewer, DaisGround, lavaAhead, clearAll, pathsAll);
        CHECK(choice.valid);
        CHECK(choice.point.y > 1.0f);

        auto const allLava = [](LedgePoint const&) { return false; };
        CHECK(!SelectLedgeStepDownPoint(bot, rockchewer, DaisGround, allLava, clearAll, pathsAll).valid);
        std::cout << "  [PASS] unsafe landing (liquid, hazard) skips only that point\n";
    }

    // Both legs need a clear line: a railing on the way out to the edge, or
    // something between the edge and the landing, skips that candidate.
    {
        std::vector<std::pair<LedgePoint, LedgePoint>> asked;
        auto const railingAhead = [&asked](LedgePoint const& a, LedgePoint const& b)
        {
            asked.push_back(std::make_pair(a, b));
            return !(b.y > -1.0f && b.y < 1.0f); // anything straight ahead is blocked
        };
        LedgeStepDownChoice const choice = SelectLedgeStepDownPoint(bot, rockchewer, DaisGround, safeAll, railingAhead, pathsAll);
        CHECK(choice.valid && choice.point.y > 1.0f);
        CHECK(!asked.empty());
        CHECK(Near(asked.front().first, bot));                 // first leg starts at the bot
        CHECK(Near(asked.front().second, P(5, 0, kDaisTop)));  // ... and ends at the edge, flat

        int legs = 0;
        auto const dropBlocked = [&legs](LedgePoint const& a, LedgePoint const&)
        {
            ++legs;
            return std::fabs(a.z - kDaisTop) < 0.01f && a.x * a.x + a.y * a.y < 0.01f; // only the bot's own leg is clear
        };
        CHECK(!SelectLedgeStepDownPoint(bot, rockchewer, DaisGround, safeAll, dropBlocked, pathsAll).valid);
        CHECK(legs > 0);

        auto const blind = [](LedgePoint const&, LedgePoint const&) { return false; };
        CHECK(!SelectLedgeStepDownPoint(bot, rockchewer, DaisGround, safeAll, blind, pathsAll).valid);
        std::cout << "  [PASS] both legs need a clear line (railings, walls)\n";
    }

    // The edge point: flat at the bot's height, past the lip, never through
    // rising ground or a hole on the way out.
    {
        LedgePoint edge;
        CHECK(LedgeEdgePoint(bot, P(8, 0, kFloor), DaisGround, edge));
        CHECK(Near(edge, P(5, 0, kDaisTop)));

        auto const flatThenDrop = [](float x, float, float, float& outZ)
        {
            outZ = x < 7.9f ? kDaisTop : kFloor; // the drop only right at the landing point
            return true;
        };
        CHECK(LedgeEdgePoint(bot, P(8, 0, kFloor), flatThenDrop, edge));
        CHECK(Near(edge, P(8, 0, kDaisTop))); // straight above the landing

        auto const wallOnTheWay = [](float x, float, float, float& outZ)
        {
            outZ = (x > 2.0f && x < 3.0f) ? kDaisTop + 2.0f : (x <= 4.0f ? kDaisTop : kFloor);
            return true;
        };
        CHECK(!LedgeEdgePoint(bot, P(8, 0, kFloor), wallOnTheWay, edge));

        auto const holeOnTheWay = [](float x, float, float, float& outZ)
        {
            if (x > 2.0f && x < 3.0f)
                return false;
            outZ = x <= 4.0f ? kDaisTop : kFloor;
            return true;
        };
        CHECK(!LedgeEdgePoint(bot, P(8, 0, kFloor), holeOnTheWay, edge));
        CHECK(!LedgeEdgePoint(bot, P(0.5f, 0, kFloor), DaisGround, edge)); // no way out to speak of
        std::cout << "  [PASS] edge point: flat, past the lip, not through walls or holes\n";
    }

    // No ground, too deep, too shallow, or a big dais: no point.
    {
        auto const noGround = [](float, float, float, float&) { return false; };
        CHECK(!SelectLedgeStepDownPoint(bot, rockchewer, noGround, safeAll, clearAll, pathsAll).valid);

        auto const pit = [](float x, float y, float, float& outZ)
        {
            outZ = (std::fabs(x) <= 4.0f && std::fabs(y) <= 4.0f) ? kDaisTop : kDaisTop - 12.0f;
            return true;
        };
        CHECK(!SelectLedgeStepDownPoint(bot, rockchewer, pit, safeAll, clearAll, pathsAll).valid);

        auto const step = [](float x, float y, float, float& outZ)
        {
            outZ = (std::fabs(x) <= 4.0f && std::fabs(y) <= 4.0f) ? kDaisTop : kDaisTop - 1.0f;
            return true;
        };
        CHECK(!SelectLedgeStepDownPoint(bot, rockchewer, step, safeAll, clearAll, pathsAll).valid);

        auto const wide = [](float x, float y, float, float& outZ)
        {
            outZ = (std::fabs(x) <= 10.0f && std::fabs(y) <= 10.0f) ? kDaisTop : kFloor;
            return true;
        };
        CHECK(!SelectLedgeStepDownPoint(bot, rockchewer, wide, safeAll, clearAll, pathsAll).valid);
        std::cout << "  [PASS] no ground / a fall / a climbable step / edge out of reach: no point\n";
    }

    // A narrow ledge: the outer ring lands in a wall (no ground), the inner ring finds floor.
    {
        auto const ledge = [](float x, float, float, float& outZ)
        {
            if (x > 6.0f)
                return false; // wall
            outZ = x <= 3.0f ? kDaisTop : kFloor;
            return true;
        };
        auto const lineShort = [](LedgePoint const&, LedgePoint const& b) { return b.x * b.x + b.y * b.y < 30.0f; };
        LedgeStepDownChoice const choice = SelectLedgeStepDownPoint(bot, rockchewer, ledge, safeAll, lineShort, pathsAll);
        CHECK(choice.valid);
        CHECK(Near(choice.point, P(5, 0, kFloor)));
        std::cout << "  [PASS] narrow ledge: the 5 yd ring\n";
    }

    // Pacing: stand still 2 s first, then one probe per 3 s. Three probes
    // that found no floor, or three launched steps, give up for 30 s; a probe
    // that was not stuck counts for neither. Moving or a new target restarts
    // the stand-still clock and the caps: the counts belong to one spot.
    {
        LedgeStepDownClock clock;
        uint32_t t = 100000;
        CHECK(!clock.Due(t, 0, 0, 7));             // first sight of this target
        CHECK(!clock.Due(t + 1000, 0, 0, 7));      // not standing long enough
        CHECK(clock.Due(t + 2000, 0, 0, 7));
        clock.Probed(t + 2000, LedgeProbeResult::NoFloor);
        CHECK(!clock.Due(t + 3000, 0, 0, 7));      // probe interval
        CHECK(clock.Due(t + 5000, 0.5f, 0.5f, 7)); // half a yard is still standing
        clock.Probed(t + 5000, LedgeProbeResult::Stepped); // a step does not count as a failed look
        CHECK(clock.Due(t + 8000, 0, 0, 7));
        clock.Probed(t + 8000, LedgeProbeResult::NoFloor);
        CHECK(clock.Due(t + 11000, 0, 0, 7));      // two failed looks only
        clock.Probed(t + 11000, LedgeProbeResult::NoFloor); // third failed look: give up
        CHECK(!clock.Due(t + 14000, 0, 0, 7));
        CHECK(!clock.Due(t + 40000, 0, 0, 7));
        CHECK(clock.Due(t + 41000, 0, 0, 7));      // 30 s later it tries again
        clock.Probed(t + 41000, LedgeProbeResult::NotStuck); // counts for nothing
        CHECK(clock.Due(t + 44000, 0, 0, 7));
        clock.Probed(t + 44000, LedgeProbeResult::NoFloor);
        clock.Probed(t + 47000, LedgeProbeResult::NoFloor);
        CHECK(clock.Due(t + 50000, 0, 0, 7));      // only two failed since the reset

        CHECK(!clock.Due(t + 53000, 5, 0, 7));     // the bot moved: restart the stand-still clock
        CHECK(!clock.Due(t + 54000, 5, 0, 7));
        CHECK(clock.Due(t + 55000, 5, 0, 7));
        clock.Probed(t + 55000, LedgeProbeResult::NoFloor);
        clock.Probed(t + 58000, LedgeProbeResult::NoFloor);
        CHECK(clock.Due(t + 61000, 5, 0, 7));      // the walk cleared the two old failures

        CHECK(!clock.Due(t + 62000, 5, 0, 8));     // another target: fresh clock
        CHECK(clock.Due(t + 64000, 5, 0, 8));

        // Put back up three times (a jump strategy): the third step gives up.
        LedgeStepDownClock jumpy;
        uint32_t u = 200000;
        CHECK(!jumpy.Due(u, 0, 0, 9));
        CHECK(jumpy.Due(u + 2000, 0, 0, 9));
        jumpy.Probed(u + 2000, LedgeProbeResult::Stepped);
        CHECK(jumpy.Due(u + 5000, 0, 0, 9));
        jumpy.Probed(u + 5000, LedgeProbeResult::Stepped);
        CHECK(jumpy.Due(u + 8000, 0, 0, 9));
        jumpy.Probed(u + 8000, LedgeProbeResult::Stepped);
        CHECK(!jumpy.Due(u + 11000, 0, 0, 9));
        CHECK(!jumpy.Due(u + 37000, 0, 0, 9));
        CHECK(jumpy.Due(u + 38000, 0, 0, 9));

        LedgeStepDownClock wrap;                   // the millisecond clock wraps
        uint32_t const late = 0xFFFFFC00u;
        CHECK(!wrap.Due(late, 0, 0, 1));
        CHECK(wrap.Due(late + 2500u, 0, 0, 1));
        std::cout << "  [PASS] pacing: stand still, interval, failed-look and step caps, reset\n";
    }

    std::cout << "Ledge step-down policy tests: OK\n";
    return 0;
}
