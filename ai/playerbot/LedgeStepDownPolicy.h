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
// a few yards towards its target and a few yards lower, which it can see,
// stand on and path from to the target, and moves there without the navmesh.
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
        LedgePoint point;
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

    // Probe pacing, per bot: only after the bot has stood within a yard for
    // kLedgeStandStillMs, then at most once per kLedgeProbeIntervalMs; after
    // kLedgeMaxAttempts stuck probes for one target, nothing for kLedgeGiveUpMs.
    static uint32_t const kLedgeStandStillMs = 2000;
    static uint32_t const kLedgeProbeIntervalMs = 3000;
    static uint32_t const kLedgeMaxAttempts = 3;
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

    // The first candidate, in LedgeStepDownCandidates order, that has ground
    // (`groundAt(x, y, z, outZ)` false = no valid height), lies in the height
    // band, is visible from the bot (`seenFromBot(point)`) and has a complete
    // navmesh path to the target (`normalPathToTarget(point)`). The checks run
    // cheapest first, so the path query only runs for points that passed the
    // rest.
    template <typename GroundFn, typename LosFn, typename PathFn>
    inline LedgeStepDownChoice SelectLedgeStepDownPoint(LedgePoint const& bot, LedgePoint const& target,
        GroundFn groundAt, LosFn seenFromBot, PathFn normalPathToTarget)
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
            if (!seenFromBot(candidate))
                continue;
            if (!normalPathToTarget(candidate))
                continue;

            result.valid = true;
            result.point = candidate;
            return result;
        }
        return result;
    }

    // Per-bot pacing of the probe, so nothing searches on every tick. Times
    // are WorldTimer milliseconds; only differences are compared, so the
    // 49-day wrap of the clock does not matter.
    struct LedgeStepDownClock
    {
        uint64_t target = 0;
        float anchorX = 0.0f;
        float anchorY = 0.0f;
        uint32_t stillSinceMs = 0;
        uint32_t lastProbeMs = 0;
        bool probed = false;
        uint32_t attempts = 0;
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
                return false;
            }

            if (givenUp)
            {
                if (nowMs - giveUpSinceMs < kLedgeGiveUpMs)
                    return false;
                givenUp = false;
                attempts = 0;
            }

            if (nowMs - stillSinceMs < kLedgeStandStillMs)
                return false;

            return !probed || nowMs - lastProbeMs >= kLedgeProbeIntervalMs;
        }

        // A probe ran. `stuck` = the path was partial with no headway, whether
        // or not a floor point was found; those count towards the give-up.
        void Probed(uint32_t nowMs, bool stuck)
        {
            probed = true;
            lastProbeMs = nowMs;
            if (!stuck)
                return;

            if (++attempts >= kLedgeMaxAttempts)
            {
                givenUp = true;
                giveUpSinceMs = nowMs;
            }
        }
    };
}
