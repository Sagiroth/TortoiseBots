#include "../ai/playerbot/LedgeStepDownPolicy.h"

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

using ai::IsLedgeStepDownPoint;
using ai::LedgePoint;
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

    auto const seesAll = [](LedgePoint const&) { return true; };
    auto const pathsAll = [](LedgePoint const&) { return true; };
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
        LedgeStepDownChoice const choice = SelectLedgeStepDownPoint(bot, rockchewer, DaisGround, seesAll, pathsAll);
        CHECK(choice.valid);
        CHECK(Near(choice.point, P(8, 0, kFloor)));
        std::cout << "  [PASS] dais: steps down straight towards the target\n";
    }

    // Needs-path rule: floor without a complete path to the target (another
    // island, a pit) is skipped; the first floor point with one is taken.
    {
        auto const pathOnlyLeft = [](LedgePoint const& point) { return point.y > 1.0f; };
        LedgeStepDownChoice const choice = SelectLedgeStepDownPoint(bot, rockchewer, DaisGround, seesAll, pathOnlyLeft);
        CHECK(choice.valid);
        CHECK(choice.point.y > 1.0f);
        CHECK(std::fabs(choice.point.z - kFloor) < 0.01f);
        float const angle = std::atan2(choice.point.y, choice.point.x);
        CHECK(std::fabs(angle - 3.14159265f / 6.0f) < 0.01f); // the +30 degree point, outer ring

        auto const noPath = [](LedgePoint const&) { return false; };
        CHECK(!SelectLedgeStepDownPoint(bot, rockchewer, DaisGround, seesAll, noPath).valid);
        std::cout << "  [PASS] needs a complete path from the point to the target\n";
    }

    // Line of sight from the bot is required (no stepping through a wall).
    {
        auto const blindAhead = [](LedgePoint const& point) { return point.y < -1.0f; };
        LedgeStepDownChoice const choice = SelectLedgeStepDownPoint(bot, rockchewer, DaisGround, blindAhead, pathsAll);
        CHECK(choice.valid && choice.point.y < -1.0f);

        auto const blind = [](LedgePoint const&) { return false; };
        CHECK(!SelectLedgeStepDownPoint(bot, rockchewer, DaisGround, blind, pathsAll).valid);
        std::cout << "  [PASS] needs line of sight from the bot\n";
    }

    // No ground, too deep, too shallow, or a big dais: no point.
    {
        auto const noGround = [](float, float, float, float&) { return false; };
        CHECK(!SelectLedgeStepDownPoint(bot, rockchewer, noGround, seesAll, pathsAll).valid);

        auto const pit = [](float x, float y, float, float& outZ)
        {
            outZ = (std::fabs(x) <= 4.0f && std::fabs(y) <= 4.0f) ? kDaisTop : kDaisTop - 12.0f;
            return true;
        };
        CHECK(!SelectLedgeStepDownPoint(bot, rockchewer, pit, seesAll, pathsAll).valid);

        auto const step = [](float x, float y, float, float& outZ)
        {
            outZ = (std::fabs(x) <= 4.0f && std::fabs(y) <= 4.0f) ? kDaisTop : kDaisTop - 1.0f;
            return true;
        };
        CHECK(!SelectLedgeStepDownPoint(bot, rockchewer, step, seesAll, pathsAll).valid);

        auto const wide = [](float x, float y, float, float& outZ)
        {
            outZ = (std::fabs(x) <= 10.0f && std::fabs(y) <= 10.0f) ? kDaisTop : kFloor;
            return true;
        };
        CHECK(!SelectLedgeStepDownPoint(bot, rockchewer, wide, seesAll, pathsAll).valid);
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
        auto const losShort = [](LedgePoint const& point) { return point.x * point.x + point.y * point.y < 30.0f; };
        LedgeStepDownChoice const choice = SelectLedgeStepDownPoint(bot, rockchewer, ledge, losShort, pathsAll);
        CHECK(choice.valid);
        CHECK(Near(choice.point, P(5, 0, kFloor)));
        std::cout << "  [PASS] narrow ledge: the 5 yd ring\n";
    }

    // Pacing: stand still 2 s first, then one probe per 3 s, three stuck probes
    // then 30 s of nothing; moving or a new target restarts the clock.
    {
        LedgeStepDownClock clock;
        uint32_t t = 100000;
        CHECK(!clock.Due(t, 0, 0, 7));             // first sight of this target
        CHECK(!clock.Due(t + 1000, 0, 0, 7));      // not standing long enough
        CHECK(clock.Due(t + 2000, 0, 0, 7));
        clock.Probed(t + 2000, true);
        CHECK(!clock.Due(t + 3000, 0, 0, 7));      // probe interval
        CHECK(clock.Due(t + 5000, 0.5f, 0.5f, 7)); // half a yard is still standing
        clock.Probed(t + 5000, true);
        CHECK(clock.Due(t + 8000, 0, 0, 7));
        clock.Probed(t + 8000, true);              // third stuck probe: give up
        CHECK(!clock.Due(t + 11000, 0, 0, 7));
        CHECK(!clock.Due(t + 37000, 0, 0, 7));
        CHECK(clock.Due(t + 38000, 0, 0, 7));      // 30 s later it tries again
        clock.Probed(t + 38000, false);            // not stuck: does not count
        CHECK(clock.Due(t + 41000, 0, 0, 7));
        clock.Probed(t + 41000, true);
        clock.Probed(t + 44000, true);
        CHECK(clock.Due(t + 47000, 0, 0, 7));      // only two stuck since the reset

        CHECK(!clock.Due(t + 50000, 5, 0, 7));     // the bot moved: restart the stand-still clock
        CHECK(!clock.Due(t + 51000, 5, 0, 7));
        CHECK(clock.Due(t + 52000, 5, 0, 7));

        CHECK(!clock.Due(t + 53000, 5, 0, 8));     // another target: fresh clock
        CHECK(clock.Due(t + 55000, 5, 0, 8));

        LedgeStepDownClock wrap;                   // the millisecond clock wraps
        uint32_t const late = 0xFFFFFC00u;
        CHECK(!wrap.Due(late, 0, 0, 1));
        CHECK(wrap.Due(late + 2500u, 0, 0, 1));
        std::cout << "  [PASS] pacing: stand still, interval, give-up, reset\n";
    }

    std::cout << "Ledge step-down policy tests: OK\n";
    return 0;
}
