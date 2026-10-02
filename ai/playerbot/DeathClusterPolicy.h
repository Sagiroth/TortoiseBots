#pragma once

#include <cstdint>

namespace ai
{
    // Death-cluster area avoidance (issue #398): the per-kind blacklist in
    // PlayerbotAI::OnDeath cannot break a camp loop whose mobs rotate as they
    // kill the bot (measured: median 7 distinct killer kinds per loop bot, 728
    // DeathClusterEscape rows across 191 bots with dying continuing). The
    // second escape inside the window below escalates from "leave this kind
    // alone" to "leave this hunting ground alone": the spot around the death
    // is avoided, so grind and quest-objective picks inside it are refused
    // and the bot walks elsewhere.
    //
    // Pure decision rule, no core includes: PlayerbotAI owns the per-bot
    // avoidance list, SetBestTarget only asks IsDeathSpotAvoided. All clocks
    // are WorldTimer ms (uint32, wraps every ~49 d); every window/expiry
    // comparison below uses unsigned subtraction, which stays correct across
    // the wrap (a direct nowMs >= expiryMs read goes true the instant a
    // wrapped expiry is stored).
    std::uint32_t const kDeathAvoidEscapes = 2; // escapes inside the window that trigger avoidance

    std::uint32_t const kDeathAvoidWindowMs = 60u * 60u * 1000u; // escapes this far apart belong to one loop

    float const kDeathAvoidRadiusYd = 300.0f; // the hunting-ground size the cluster rule measures

    std::uint32_t const kDeathAvoidMs = 60u * 60u * 1000u; // how long the spot stays avoided (outlasts the 30-min kind blacklist)

    // Starter valleys are only ~400-500 yd across and a level <= 5 bot cannot
    // walk out of its own (1500 yd cap, area-level gate), so a full-size spot
    // would swallow every eligible destination and strand it for the hour.
    // Lowbies avoid a smaller camp for a shorter while; a ding clears the
    // list outright (see ClearDeathAvoidanceOnLevelUp), since the band that
    // killed them no longer applies.
    float const kDeathAvoidRadiusLowYd = 100.0f;

    std::uint32_t const kDeathAvoidLowMs = 15u * 60u * 1000u;

    std::uint32_t const kDeathAvoidLowLevel = 5;

    std::uint32_t const kDeathAvoidSpots = 3; // avoided spots kept per bot; the oldest live one is replaced

    // Radius and duration for the bot's current level.
    inline float DeathAvoidRadiusYd(std::uint32_t botLevel)
    {
        return botLevel <= kDeathAvoidLowLevel ? kDeathAvoidRadiusLowYd : kDeathAvoidRadiusYd;
    }

    inline std::uint32_t DeathAvoidDurationMs(std::uint32_t botLevel)
    {
        return botLevel <= kDeathAvoidLowLevel ? kDeathAvoidLowMs : kDeathAvoidMs;
    }

    // Escape count after one more escape now: a repeat inside the window
    // extends the streak, an escape after the window starts a fresh one.
    inline std::uint32_t NextDeathEscapeCount(std::uint32_t prevCount, std::uint32_t nowMs, std::uint32_t lastEscapeMs,
        std::uint32_t windowMs = kDeathAvoidWindowMs)
    {
        if (prevCount > 0 && (nowMs - lastEscapeMs) <= windowMs)
            return prevCount + 1;
        return 1;
    }

    inline bool DeathAvoidanceEscalated(std::uint32_t escapeCount, std::uint32_t threshold = kDeathAvoidEscapes)
    {
        return escapeCount >= threshold;
    }

    struct DeathAvoidSpot
    {
        std::uint32_t mapId = 0;
        float x = 0.0f;
        float y = 0.0f;
        std::uint32_t expiryMs = 0; // 0 = slot free
        float radiusYd = kDeathAvoidRadiusYd;
    };

    // Wrap-safe expiry: remaining = expiryMs - nowMs in unsigned arithmetic;
    // a wrapped (already passed) expiry yields a huge remainder, never <= 0.
    // An int32 cast reads the sign back: remaining > 0 means still avoided.
    inline bool DeathSpotActive(std::uint32_t nowMs, std::uint32_t expiryMs)
    {
        if (expiryMs == 0)
            return false;
        return static_cast<std::int32_t>(expiryMs - nowMs) > 0;
    }

    // Whether map position (mapId, x, y) falls inside one active avoidance
    // spot. Radius compares squared like the cluster rule.
    inline bool IsDeathPositionAvoided(std::uint32_t mapId, float x, float y, std::uint32_t nowMs,
        DeathAvoidSpot const* spots, std::uint32_t spotCount)
    {
        for (std::uint32_t i = 0; i < spotCount; ++i)
        {
            DeathAvoidSpot const& spot = spots[i];
            if (!DeathSpotActive(nowMs, spot.expiryMs))
                continue;
            if (mapId != spot.mapId)
                continue;
            float const dx = x - spot.x;
            float const dy = y - spot.y;
            if (dx * dx + dy * dy <= spot.radiusYd * spot.radiusYd)
                return true;
        }
        return false;
    }

    // Record one more avoided spot: reuse an expired/free slot when there is
    // one, otherwise replace the oldest live spot so a second camp cannot
    // erase the first while a third still rotates in.
    inline void AddDeathAvoidSpot(DeathAvoidSpot* spots, std::uint32_t spotCount, std::uint32_t mapId, float x,
        float y, std::uint32_t nowMs, std::uint32_t durationMs, float radiusYd)
    {
        std::uint32_t slot = spotCount;
        for (std::uint32_t i = 0; i < spotCount; ++i)
        {
            if (!DeathSpotActive(nowMs, spots[i].expiryMs))
            {
                slot = i;
                break;
            }
        }
        if (slot == spotCount)
        {
            slot = 0;
            for (std::uint32_t i = 1; i < spotCount; ++i)
            {
                // Oldest = smallest remaining life (unsigned compare would
                // flip across the wrap; both are live so signed is safe).
                if (static_cast<std::int32_t>(spots[i].expiryMs - nowMs) <
                    static_cast<std::int32_t>(spots[slot].expiryMs - nowMs))
                    slot = i;
            }
        }
        spots[slot].mapId = mapId;
        spots[slot].x = x;
        spots[slot].y = y;
        spots[slot].expiryMs = nowMs + durationMs;
        spots[slot].radiusYd = radiusYd;
    }

    // Quest-objective purposes (bitmask enum) share one bitmask with the
    // composite QuestAllObjective flag, so test with & instead of a range:
    // a composite purpose id would fail a >=/<= check.
    inline bool IsDeathGatedPurpose(std::uint32_t purposeId, std::uint32_t grindId, std::uint32_t allObjectivesId)
    {
        return purposeId == grindId || (purposeId & allObjectivesId) != 0;
    }
}
