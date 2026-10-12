#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

// Pure policy for stepping a bot off a cut-off platform inside a dungeon.
//
// Dungeon navmeshes have islands: raised daises, ledges and roofs that the
// mesh does not connect to the floor around them (Uldaman's Hall of the
// Keepers has 17 of them, about 4 yd above the floor). A bot that lands on one
// (a summon, a teleport, a jump) gets only a partial path to its target, and
// the partial end lies about a yard from where it stands. The core's chase
// and follow stand still on such a path in sight of the target, the module's
// MoveTo walks the yard and stops, and the bot waited out the whole fight.
// A player simply steps off the edge, so the bot does too: it looks for floor
// a few yards towards its target and a few yards lower that really is walkable
// navmesh (not lava, slime or a hole next to the floor) with a complete path
// to the target, and moves there without the navmesh in two legs: flat out
// over the edge at its own height, then down, so it never cuts through the
// platform it stands on.
//
// The geometry (candidates, height band, the order of the checks) is decided
// here and tested on its own; the map queries are callbacks.

namespace ai
{
    struct LedgePoint
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct LedgeStepDownChoice
    {
        bool valid = false;
        LedgePoint point; // the landing point on the floor
        LedgePoint edge;  // out over the edge, at the bot's own height
    };

    // What the navmesh says about a path from a landing point to the target:
    // whether it is complete (PATHFIND_NORMAL), and the point the path really
    // starts from. The core snaps a start up to 5 yd sideways and 3 yd up to
    // the nearest walkable polygon, so a NORMAL path alone does not prove the
    // landing point is walkable.
    struct LedgePathProbe
    {
        bool normal = false;
        bool hasStart = false;
        LedgePoint start;
    };

    // Candidate floor points lie this far from the bot horizontally: the
    // outer ring clears a dais of Uldaman's size (about 8 x 8 yd) from its
    // middle, the inner ring catches a narrow ledge whose far side is wall.
    static float const kLedgeStepOuterRadius = 8.0f;
    static float const kLedgeStepInnerRadius = 5.0f;
    // A step of less than 2 yd is one the navmesh would have climbed (its
    // WMO step is 1.2 yd) or the same platform; more than 8 yd is a fall that
    // hurts and is no longer "stepping off".
    static float const kLedgeStepMinDrop = 2.0f;
    static float const kLedgeStepMaxDrop = 8.0f;
    // The partial path makes no headway when it ends this close to the bot.
    static float const kLedgeNoHeadwayYards = 2.0f;
    // The landing point is on the navmesh when the path from it starts this
    // close to it (horizontally / vertically).
    static float const kLedgeMeshSnapHorizontal = 1.0f;
    static float const kLedgeMeshSnapVertical = 1.5f;
    // The edge search samples the ground every half yard along the way out and
    // steps half a yard past the first sample that is a drop, to clear the lip.
    static float const kLedgeEdgeSampleStep = 0.5f;
    static float const kLedgeEdgeOvershoot = 0.5f;
    // Ground that rises more than this above the bot on the way out is a wall
    // or a higher step, not an edge.
    static float const kLedgeEdgeMaxRise = 1.0f;

    // Probe pacing, per action of a bot (reach and follow each keep a clock):
    // only after the bot has stood within a yard for kLedgeStandStillMs, then
    // at most once per kLedgeProbeIntervalMs. After kLedgeMaxAttempts probes
    // that found no floor, or kLedgeMaxSteps launched steps (something keeps
    // putting the bot back up, e.g. a jump), for one target: nothing for
    // kLedgeGiveUpMs.
    static uint32_t const kLedgeStandStillMs = 2000;
    static uint32_t const kLedgeProbeIntervalMs = 3000;
    static uint32_t const kLedgeMaxAttempts = 3;
    static uint32_t const kLedgeMaxSteps = 3;
    static uint32_t const kLedgeGiveUpMs = 30000;

    inline float LedgeHorizontalDistance(LedgePoint const& a, LedgePoint const& b)
    {
        float const dx = a.x - b.x;
        float const dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    // Only inside a dungeon or raid (the owner's choice), only on a partial
    // path that ends within kLedgeNoHeadwayYards of the bot, and never while
    // something is hitting the bot (stepping away would drag the fight).
    inline bool UseLedgeStepDown(bool inDungeon, bool pathIncomplete, float partialEndDistance, bool beingHit)
    {
        return inDungeon && pathIncomplete && !beingHit && partialEndDistance <= kLedgeNoHeadwayYards;
    }

    // A floor point the bot may step down to: within the outer radius
    // horizontally (and not under its feet), and 2-8 yd below it.
    inline bool IsLedgeStepDownPoint(LedgePoint const& bot, LedgePoint const& point)
    {
        float const horizontal = LedgeHorizontalDistance(bot, point);
        if (horizontal < 1.0f || horizontal > kLedgeStepOuterRadius + 0.5f)
            return false;

        float const drop = bot.z - point.z;
        return drop >= kLedgeStepMinDrop && drop <= kLedgeStepMaxDrop;
    }

    // The path from the landing point starts on the point itself, not on a
    // polygon the core snapped to from a lava pool, a slime pool or a hole.
    inline bool LedgeLandingOnMesh(LedgePoint const& landing, LedgePoint const& pathStart)
    {
        return LedgeHorizontalDistance(landing, pathStart) <= kLedgeMeshSnapHorizontal &&
            std::fabs(landing.z - pathStart.z) <= kLedgeMeshSnapVertical;
    }

    // The point out over the edge, at the bot's height, on the way from the
    // bot to `landing`: half a yard past the first ground sample (every half
    // yard) that lies kLedgeStepMinDrop or more below the bot, or straight
    // above the landing point when no sample before it does. False when a
    // sample on the way has no ground or rises over kLedgeEdgeMaxRise above
    // the bot (a wall, a higher step): that way out is not a ledge.
    template <typename GroundFn>
    inline bool LedgeEdgePoint(LedgePoint const& bot, LedgePoint const& landing, GroundFn groundAt, LedgePoint& edge)
    {
        float const horizontal = LedgeHorizontalDistance(bot, landing);
        if (horizontal < 1.0f)
            return false;

        float const ux = (landing.x - bot.x) / horizontal;
        float const uy = (landing.y - bot.y) / horizontal;
        float edgeDistance = horizontal;
        for (float d = kLedgeEdgeSampleStep; d < horizontal; d += kLedgeEdgeSampleStep)
        {
            float groundZ = 0.0f;
            if (!groundAt(bot.x + ux * d, bot.y + uy * d, bot.z, groundZ))
                return false;
            if (groundZ > bot.z + kLedgeEdgeMaxRise)
                return false;
            if (bot.z - groundZ >= kLedgeStepMinDrop)
            {
                edgeDistance = std::fmin(d + kLedgeEdgeOvershoot, horizontal);
                break;
            }
        }

        edge.x = bot.x + ux * edgeDistance;
        edge.y = bot.y + uy * edgeDistance;
        edge.z = bot.z;
        return true;
    }

    // Candidate (x, y) points, best first: straight towards the target, then
    // fanning out 30, 60 and 90 degrees to either side, outer ring before
    // inner ring. z is the bot's own height; the caller replaces it with the
    // ground height found under the point.
    inline std::vector<LedgePoint> LedgeStepDownCandidates(LedgePoint const& bot, LedgePoint const& target)
    {
        float const dx = target.x - bot.x;
        float const dy = target.y - bot.y;
        float const bearing = (dx * dx + dy * dy) > 0.0001f ? std::atan2(dy, dx) : 0.0f;
        float const step = 3.14159265f / 6.0f;
        float const offsets[] = { 0.0f, step, -step, 2.0f * step, -2.0f * step, 3.0f * step, -3.0f * step };
        float const radii[] = { kLedgeStepOuterRadius, kLedgeStepInnerRadius };

        std::vector<LedgePoint> candidates;
        candidates.reserve(14);
        for (float const radius : radii)
        {
            for (float const offset : offsets)
            {
                LedgePoint point;
                point.x = bot.x + radius * std::cos(bearing + offset);
                point.y = bot.y + radius * std::sin(bearing + offset);
                point.z = bot.z;
                candidates.push_back(point);
            }
        }
        return candidates;
    }

    // The first candidate, in LedgeStepDownCandidates order, that passes, in
    // this order (cheapest first, each failure moves on to the next candidate):
    // - ground under it (`groundAt(x, y, z, outZ)`, false = no valid height),
    // - the height band and radius (IsLedgeStepDownPoint),
    // - a safe landing (`landingSafe(point)`: no lava, slime or hazard),
    // - a way out over the edge (LedgeEdgePoint),
    // - a clear line for both legs (`clearLine(a, b)`: bot to edge, edge to
    //   landing; the caller checks low enough to catch railings),
    // - a complete navmesh path from it to the target that starts on the point
    //   itself (`pathFromLanding(point)` -> LedgePathProbe, LedgeLandingOnMesh).
    template <typename GroundFn, typename SafeFn, typename LineFn, typename PathFn>
    inline LedgeStepDownChoice SelectLedgeStepDownPoint(LedgePoint const& bot, LedgePoint const& target,
        GroundFn groundAt, SafeFn landingSafe, LineFn clearLine, PathFn pathFromLanding)
    {
        LedgeStepDownChoice result;
        for (LedgePoint candidate : LedgeStepDownCandidates(bot, target))
        {
            float groundZ = 0.0f;
            if (!groundAt(candidate.x, candidate.y, candidate.z, groundZ))
                continue;
            candidate.z = groundZ;

            if (!IsLedgeStepDownPoint(bot, candidate))
                continue;
            if (!landingSafe(candidate))
                continue;

            LedgePoint edge;
            if (!LedgeEdgePoint(bot, candidate, groundAt, edge))
                continue;
            if (!clearLine(bot, edge) || !clearLine(edge, candidate))
                continue;

            LedgePathProbe const probe = pathFromLanding(candidate);
            if (!probe.normal || !probe.hasStart || !LedgeLandingOnMesh(candidate, probe.start))
                continue;

            result.valid = true;
            result.point = candidate;
            result.edge = edge;
            return result;
        }
        return result;
    }

    enum class LedgeProbeResult
    {
        NotStuck, // the path makes headway: nothing to do
        NoFloor,  // stuck, and no candidate passed
        Stepped   // stuck, and a step was launched
    };

    // Pacing of the probe for one action of one bot, so nothing searches on
    // every tick. Times are WorldTimer milliseconds; only differences are
    // compared, so the 49-day wrap of the clock does not matter. The failed
    // and launched counts belong to one circumstance (one spot, one target):
    // the bot walking somewhere resets the caps, not just the stand-still
    // clock, so three looks in Dalaran do not disarm a ledge in Uldaman.
    struct LedgeStepDownClock
    {
        uint64_t target = 0;
        float anchorX = 0.0f;
        float anchorY = 0.0f;
        uint32_t stillSinceMs = 0;
        uint32_t lastProbeMs = 0;
        bool probed = false;
        uint32_t attempts = 0;
        uint32_t steps = 0;
        uint32_t giveUpSinceMs = 0;
        bool givenUp = false;

        // Feeds the bot's position for this target; true when a probe is due.
        bool Due(uint32_t nowMs, float x, float y, uint64_t targetKey)
        {
            if (targetKey != target)
            {
                *this = LedgeStepDownClock();
                target = targetKey;
                anchorX = x;
                anchorY = y;
                stillSinceMs = nowMs;
                return false;
            }

            float const dx = x - anchorX;
            float const dy = y - anchorY;
            if (dx * dx + dy * dy > 1.0f)
            {
                anchorX = x;
                anchorY = y;
                stillSinceMs = nowMs;
                attempts = 0;
                steps = 0;
                givenUp = false;
                return false;
            }

            if (givenUp)
            {
                if (nowMs - giveUpSinceMs < kLedgeGiveUpMs)
                    return false;
                givenUp = false;
                attempts = 0;
                steps = 0;
            }

            if (nowMs - stillSinceMs < kLedgeStandStillMs)
                return false;

            return !probed || nowMs - lastProbeMs >= kLedgeProbeIntervalMs;
        }

        // A probe ran. Only probes that found no floor count towards the
        // give-up; launched steps have their own cap, so a bot that keeps
        // being put back up (a jump strategy) does not step forever.
        void Probed(uint32_t nowMs, LedgeProbeResult result)
        {
            probed = true;
            lastProbeMs = nowMs;
            if (result == LedgeProbeResult::NotStuck)
                return;

            uint32_t& count = result == LedgeProbeResult::NoFloor ? attempts : steps;
            uint32_t const limit = result == LedgeProbeResult::NoFloor ? kLedgeMaxAttempts : kLedgeMaxSteps;
            if (++count >= limit)
            {
                givenUp = true;
                giveUpSinceMs = nowMs;
            }
        }
    };
}
