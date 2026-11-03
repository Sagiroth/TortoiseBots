#pragma once

// Pure decision rules for the universal bomb runout (the carrier's own
// escape: Living Bomb, Burning Adrenaline, Mutating Injection, Detonate
// Mana, Emerald Rot). Ported donor behaviour (mod-playerbots @ 79bd4281):
//   MovementAction::MoveFromGroup (src/Ai/Base/Actions/MovementActions.cpp)
//   used by McMoveFromGroupAction (src/Ai/Raid/MC/MCActions.cpp)
// Donor shape: run directly away from the centroid of the living same-map
// group members, only while the nearest one is inside the radius, by
// `radius - nearest`.
// Adapted: the step is lengthened (2yd at a time) until the end point
// clears every member by the radius plus a small margin, so one move gets
// the carrier clear instead of re-stepping each tick; once clear the
// action stops (the margin keeps it from twitching at the edge).
// No core includes: the action translates group positions into anchors,
// so tools/test_bomb_runout_policy.cpp can test the rules.

#include "policy/combat/DebuffSpreadPolicy.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ai
{
    // The one list of bomb debuff spell ids: Living Bomb, Burning Adrenaline
    // (Turtle + classic), Mutating Injection, Emerald Rot, Detonate Mana
    // (27819). Shared by the bomb trigger and ReactionEngine's wake-up check.
    constexpr std::uint32_t kRaidBombSpellIds[] = { 20475, 23620, 18173, 23478, 28169, 27819 };
    constexpr std::size_t kRaidBombSpellCount = sizeof(kRaidBombSpellIds) / sizeof(kRaidBombSpellIds[0]);

    // Extra clearance aimed for past the radius, and the step search grain.
    constexpr float kBombRunoutMarginYd = 2.0f;
    constexpr float kBombRunoutStepYd = 2.0f;

    // A flee_on_aura escape keeps a healer within reach of its end point when one can: 40 yd spell range minus a
    // margin, so the healer can still heal the carrier on the way out.
    constexpr float kFleeHealerReachYd = 36.0f;

    // Whether a point lies within `reach` of at least one living group healer. No healer at all: not healable.
    inline bool FleePointHealable(float x, float y, const DebuffAnchor* healers, std::size_t count, float reach)
    {
        if (!healers || count == 0)
            return false;
        for (std::size_t i = 0; i < count; ++i)
        {
            float const dx = x - healers[i].x;
            float const dy = y - healers[i].y;
            if (dx * dx + dy * dy <= reach * reach)
                return true;
        }
        return false;
    }

    // Centroid of the members and the bot's distance to the nearest one.
    // False when there is nobody to run from.
    inline bool BombRunoutCentre(float botX, float botY, const DebuffAnchor* members, std::size_t count,
        float& centreX, float& centreY, float& nearestDist, std::size_t& nearestIdx)
    {
        if (!members || count == 0)
            return false;
        centreX = 0.0f;
        centreY = 0.0f;
        nearestDist = -1.0f;
        nearestIdx = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            centreX += members[i].x;
            centreY += members[i].y;
            float dx = botX - members[i].x;
            float dy = botY - members[i].y;
            float d = std::sqrt(dx * dx + dy * dy);
            if (nearestDist < 0.0f || d < nearestDist)
            {
                nearestDist = d;
                nearestIdx = i;
            }
        }
        centreX /= static_cast<float>(count);
        centreY /= static_cast<float>(count);
        return true;
    }

    // Already clear: nobody inside the blast radius, so stay put.
    inline bool IsBombRunoutClear(float nearestDist, float radius)
    {
        return nearestDist >= radius;
    }

    // Heading directly away from the group's centre. A bot standing on the
    // centre runs away from its nearest member instead.
    inline float BombRunoutAwayAngle(float botX, float botY, float centreX, float centreY,
        float nearestX, float nearestY)
    {
        float dx = botX - centreX, dy = botY - centreY;
        if (dx * dx + dy * dy < 0.01f)
        {
            dx = botX - nearestX;
            dy = botY - nearestY;
            if (dx * dx + dy * dy < 0.01f)
                return 0.0f;
        }
        return std::atan2(dy, dx);
    }

    // One sweep along `angle`: the donor's `radius - nearest` first, then 2yd longer at a time, up to twice the radius.
    //   healable: the first clear step that also lands within kFleeHealerReachYd of a healer (-1 when none)
    //   clear:    the first step that clears every member by radius + margin (-1 when none)
    //   best:     the step with the most clearance (the first one tried, on a tie)
    struct BombRunoutSweep
    {
        float healable = -1.0f;
        float clear = -1.0f;
        float best = 0.0f;
    };

    inline BombRunoutSweep SweepBombRunout(float botX, float botY, float angle, float nearestDist, float radius,
        const DebuffAnchor* members, std::size_t count, const DebuffAnchor* healers, std::size_t nHealers)
    {
        BombRunoutSweep sweep;
        float step = radius - nearestDist;
        if (step < kBombRunoutStepYd)
            step = kBombRunoutStepYd;
        const float cap = radius * 2.0f;
        sweep.best = step;
        float bestClear = -1.0f;
        for (; step <= cap; step += kBombRunoutStepYd)
        {
            float x = botX + std::cos(angle) * step;
            float y = botY + std::sin(angle) * step;
            float clear = DebuffEscapeClearance(x, y, members, count);
            if (clear >= radius + kBombRunoutMarginYd)
            {
                if (sweep.clear < 0.0f)
                    sweep.clear = step;
                if (sweep.healable < 0.0f && FleePointHealable(x, y, healers, nHealers, kFleeHealerReachYd))
                    sweep.healable = step;
            }
            if (clear > bestClear)
            {
                bestClear = clear;
                sweep.best = step;
            }
        }
        return sweep;
    }

    // How far to run along `angle`. With `healers` the first clear step that also lands within kFleeHealerReachYd of
    // one of them wins; when this angle has no healable clear step the old answer stands (the first clear step, else
    // the best clearance), so an escape still escapes.
    inline float BombRunoutStep(float botX, float botY, float angle, float nearestDist, float radius,
        const DebuffAnchor* members, std::size_t count, const DebuffAnchor* healers, std::size_t nHealers)
    {
        const BombRunoutSweep sweep = SweepBombRunout(botX, botY, angle, nearestDist, radius, members, count,
            healers, nHealers);
        if (sweep.healable >= 0.0f)
            return sweep.healable;
        return sweep.clear >= 0.0f ? sweep.clear : sweep.best;
    }

    // Picks the escape angle and reports the step to `tryStep(index, step)`, which returns true when that end point
    // was accepted (pathable and the move started); the first accepted angle wins and its index is returned, -1 when
    // none is accepted. With healers the order is two passes over ALL angles, in preference order:
    //   pass 1: the first angle whose clear step lands within healer reach,
    //   pass 2: only when pass 1 found nothing accepted: each angle's clear, unhealable step (else its best clearance).
    // So a clear but unhealable angle never beats a healable one that is also accepted, whatever its position in the
    // list. Without healers only pass 2 runs, which is the hard-coded bomb runout's behaviour.
    template <typename TryStep>
    inline int PickRunoutAngle(float botX, float botY, const float* offsets, std::size_t nOffsets, float away,
        float nearestDist, float radius, const DebuffAnchor* members, std::size_t count,
        const DebuffAnchor* healers, std::size_t nHealers, TryStep&& tryStep)
    {
        if (healers && nHealers > 0)
        {
            for (std::size_t i = 0; i < nOffsets; ++i)
            {
                const BombRunoutSweep sweep = SweepBombRunout(botX, botY, away + offsets[i], nearestDist, radius,
                    members, count, healers, nHealers);
                if (sweep.healable >= 0.0f && tryStep(i, sweep.healable))
                    return static_cast<int>(i);
            }
        }
        for (std::size_t i = 0; i < nOffsets; ++i)
        {
            const float step = BombRunoutStep(botX, botY, away + offsets[i], nearestDist, radius, members, count,
                nullptr, 0);
            if (tryStep(i, step))
                return static_cast<int>(i);
        }
        return -1;
    }

    // The hard-coded bomb runout's rule, unchanged: no healer to keep in reach.
    inline float BombRunoutStep(float botX, float botY, float angle, float nearestDist, float radius,
        const DebuffAnchor* members, std::size_t count)
    {
        return BombRunoutStep(botX, botY, angle, nearestDist, radius, members, count, nullptr, 0);
    }
} // namespace ai
