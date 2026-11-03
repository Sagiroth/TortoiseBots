#pragma once

#include <cstdint>
#include <ctime>

// Craft-order workshop legs (order focus trips). A forced forge pick that the
// walk cannot reach used to be re-picked forever: the failure only parked the
// purpose for 5 min and the next search offered the same point again. These
// rules remember the failed point itself, so the next pick walks a different
// forge instead.
//
// The world-facing parts (marking the point on drop, skipping marked points
// in the pick) live in MoveToTravelTargetAction / ChooseTravelTargetAction;
// the decisions below are pure so they can be tested on their own.
namespace ai
{
    // Unreachable workshop points kept per bot. Small on purpose: a craft
    // order only needs the forges near its current zone.
    std::uint32_t const kUnreachableFocusSpots = 4;

    // One respawn-ish window: long enough to finish the order elsewhere,
    // short enough that a fixed mesh is retried the same session.
    std::uint32_t const kUnreachableFocusMs = 15u * 60u * 1000u;

    float const kUnreachableFocusRadiusYd = 10.0f;

    struct UnreachableFocusSpot
    {
        std::uint32_t mapId = 0;
        float x = 0.0f;
        float y = 0.0f;
        std::uint32_t expiryMs = 0;
    };

    inline bool FocusSpotActive(std::uint32_t nowMs, std::uint32_t expiryMs)
    {
        if (expiryMs == 0)
            return false;
        return static_cast<std::int32_t>(expiryMs - nowMs) > 0;
    }

    inline bool IsFocusPointAvoided(std::uint32_t mapId, float x, float y, std::uint32_t nowMs,
        UnreachableFocusSpot const* spots, std::uint32_t spotCount)
    {
        for (std::uint32_t i = 0; i < spotCount; ++i)
        {
            if (!FocusSpotActive(nowMs, spots[i].expiryMs))
                continue;
            if (mapId != spots[i].mapId)
                continue;
            float const dx = x - spots[i].x;
            float const dy = y - spots[i].y;
            if (dx * dx + dy * dy <= kUnreachableFocusRadiusYd * kUnreachableFocusRadiusYd)
                return true;
        }
        return false;
    }

    inline void AddUnreachableFocusSpot(UnreachableFocusSpot* spots, std::uint32_t spotCount,
        std::uint32_t mapId, float x, float y, std::uint32_t nowMs,
        std::uint32_t durationMs = kUnreachableFocusMs)
    {
        std::uint32_t slot = spotCount;
        for (std::uint32_t i = 0; i < spotCount; ++i)
        {
            if (!FocusSpotActive(nowMs, spots[i].expiryMs))
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
                if (static_cast<std::int32_t>(spots[i].expiryMs - nowMs) <
                    static_cast<std::int32_t>(spots[slot].expiryMs - nowMs))
                    slot = i;
            }
        }
        spots[slot].mapId = mapId;
        spots[slot].x = x;
        spots[slot].y = y;
        spots[slot].expiryMs = nowMs + durationMs;
        if (spots[slot].expiryMs == 0)
            spots[slot].expiryMs = 1;
    }

    // Only workshop legs feed this book: a forge the bot could not walk to,
    // dropped after its move budget ran out. Inside 8 yd the bot can smelt
    // from where it stands, so no mark.
    inline bool FocusDropMarksSpot(bool orderFocusTrip, float stuckYd)
    {
        return orderFocusTrip && stuckYd >= 8.0f;
    }

    // A dropped workshop leg parks its purpose briefly, not for the full
    // move-fail window: the order cannot progress while it stands down, so
    // the next pick must come soon enough to try the next forge. Other
    // purposes are untouched.
    inline constexpr std::time_t ORDER_FOCUS_PARK_SECONDS = 60;
}
