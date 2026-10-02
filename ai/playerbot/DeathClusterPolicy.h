#pragma once

#include <cstdint>

namespace ai
{
    // Death-cluster area avoidance (issue #398): the per-kind blacklist in
    // PlayerbotAI::OnDeath cannot break a camp loop whose mobs rotate as they
    // kill the bot (measured: median 7 distinct killer kinds per loop bot, 728
    // DeathClusterEscape rows across 191 bots with dying continuing). The
    // second escape inside the window below escalates from "leave this kind
    // alone" to "leave this hunting ground alone": the 300 yd spot around the
    // death is avoided for an hour, so grind and quest-objective destinations
    // and picks inside it go inactive and the bot walks elsewhere.
    //
    // Pure decision rule, no core includes: PlayerbotAI owns the per-bot
    // counters and the avoidance spot, TravelMgr / SetBestTarget only ask
    // IsDeathSpotAvoided. All clocks are WorldTimer ms (uint32, wraps every
    // ~49 d); window math uses unsigned subtraction like the callers.
    std::uint32_t const kDeathAvoidEscapes = 2; // escapes inside the window that trigger avoidance

    std::uint32_t const kDeathAvoidWindowMs = 60u * 60u * 1000u; // escapes this far apart belong to one loop

    float const kDeathAvoidRadiusYd = 300.0f; // the hunting-ground size the cluster rule measures

    std::uint32_t const kDeathAvoidMs = 60u * 60u * 1000u; // how long the spot stays avoided (outlasts the 30-min kind blacklist)

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

    // Whether map position (mapId, x, y) falls inside an active avoidance
    // spot. No avoidance is stored as avoidExpiryMs == 0; an expired spot
    // reads as not avoided. Radius compares squared like the cluster rule.
    inline bool IsDeathPositionAvoided(std::uint32_t mapId, float x, float y, std::uint32_t avoidMapId, float avoidX,
        float avoidY, std::uint32_t nowMs, std::uint32_t avoidExpiryMs, float radiusYd = kDeathAvoidRadiusYd)
    {
        if (avoidExpiryMs == 0 || nowMs >= avoidExpiryMs)
            return false;
        if (mapId != avoidMapId)
            return false;
        float const dx = x - avoidX;
        float const dy = y - avoidY;
        return dx * dx + dy * dy <= radiusYd * radiusYd;
    }
}
