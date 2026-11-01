#pragma once

// Pure rules for the pool-bot flee step in MovementAction::Flee.
//
// Pool bots (no real player master) used to flee with a MoveChase at flee
// range. The core chase generator holds position while a standing target is
// within 1.4x that range ("prevent redundant micro-movement"), so the flee
// reported success every tick without moving and nothing else could run.
// The step below is a checked point move instead (mod-playerbots @ 79bd4281
// MovementAction::MoveAway shape): straight away from the target first, then
// fanned out to either side in 22.5 degree steps up to 90 degrees.
#include <cstdint>

namespace ai
{
    constexpr int kPoolFleeHeadingCount = 9;

    // Heading offsets from "straight away from the target", in try order.
    inline int PoolFleeHeadingOffsets(float out[kPoolFleeHeadingCount])
    {
        float const step = 0.3926990817f; // PI/8
        int count = 0;
        out[count++] = 0.0f;
        for (int i = 1; i <= 4; ++i)
        {
            out[count++] = step * i;
            out[count++] = -step * i;
        }
        return count;
    }

    // After a search that found no spot, flee fails at once for this long:
    // the bot fights instead of paying the candidate checks every tick.
    constexpr std::uint32_t kPoolFleeNoSpotBackoffMs = 2000;

    // noSpotMs == 0 means no failed search is remembered.
    inline bool PoolFleeSearchBackedOff(std::uint32_t noSpotMs, std::uint32_t nowMs)
    {
        return noSpotMs != 0 && std::uint32_t(nowMs - noSpotMs) < kPoolFleeNoSpotBackoffMs;
    }

    // Extra yards kept from an unpulled mob's aggro radius.
    constexpr float kPoolFleeAggroMargin = 2.0f;

    // An unpulled hostile mob vetoes a landing that is inside its aggro
    // radius (plus margin) and closer to it than the bot stands now, so a
    // bot already inside a radius may still step away from that mob.
    inline bool PoolFleeLandingWakesMob(float mobToLanding, float mobToBot, float aggroRadius)
    {
        return mobToLanding < aggroRadius + kPoolFleeAggroMargin && mobToLanding < mobToBot;
    }

    // A mob too far for one step to bring the bot into its radius is not
    // worth checking per candidate.
    inline bool PoolFleeMobInReach(float mobToBot, float aggroRadius, float stepLength)
    {
        return mobToBot < aggroRadius + kPoolFleeAggroMargin + stepLength;
    }

    // Cap for the per-tick live-attacker scratch in FleeStepAway: attacker
    // counts are tiny and this runs every flee tick, so a fixed stack array
    // replaces the heap vector.
    constexpr int kPoolFleeMaxPackAttackers = 8;

    // Non-owning prefix over a filled stack array, so the centroid helper
    // keeps its range interface without a heap vector.
    template <typename T>
    struct FleeAttackerPrefix
    {
        T* data = nullptr;
        int count = 0;
        T const* begin() const { return data; }
        T const* end() const { return data + count; }
    };

    // Pack centroid for the R3 outnumbered flee: the average position of the
    // live attackers, so the flee step runs from their pack, not from one
    // mob. A bot between two mobs must not "flee" toward the second one;
    // the centroid of a split pack sits between them and the fan-out finds
    // the gap. Count 0 (or no live attacker) means "no pack": the caller
    // keeps the single-target heading.
    struct FleePackPoint
    {
        float x = 0.0f;
        float y = 0.0f;
        bool valid = false;
    };
    template <typename AttackerList, typename PosFn>
    inline FleePackPoint PoolFleePackCentroid(AttackerList const& attackers, PosFn posOf)
    {
        FleePackPoint out;
        float sx = 0.0f, sy = 0.0f;
        int n = 0;
        for (auto const& attacker : attackers)
        {
            float x = 0.0f, y = 0.0f;
            if (!posOf(attacker, x, y))
                continue;
            sx += x;
            sy += y;
            ++n;
        }
        if (n == 0)
            return out;
        out.x = sx / n;
        out.y = sy / n;
        out.valid = true;
        return out;
    }
}
