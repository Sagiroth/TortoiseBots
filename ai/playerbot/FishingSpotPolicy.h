#pragma once

#include <cstdint>

namespace ai
{
    // Open-water fishing search geometry for issue #402.
    //
    // The travel fish table (`FISH_LOCATION_*`) is empty and fish-location
    // generation is disabled, so `TravelMgr::GetFishSpot` never returns a spot
    // and pool bots never fish. Ported from mod-playerbots
    // (`src/Ai/Base/Actions/FishingAction.cpp`: `FindWaterRadial`,
    // `FindFishingHole`, `HasFishableWaterOrLand`), adapted to the Tortoise
    // core: liquid queries go through `TerrainInfo::GetWaterLevel` /
    // `getLiquidStatus` and line of sight through `Map::isInLineOfSight`.
    //
    // Pool (masterless random) bots only: owned/hired bots keep player control
    // and never take the direct-water path. The travel fish errand is untouched
    // - when the dataset has fish destinations they still win through the
    // normal travel target; this only fishes nearby water while the bot has no
    // better errand, so the travel/level gates (#418, #428, #434) are not
    // bypassed: no destination is picked, no route is walked.
    //
    // Levelling first (review): fishing is an occasional side activity, never
    // a stall. One session per hour of play, at most ~5 min / 5 casts, never
    // while the bot has an active travel errand, a rewardable finished quest,
    // or vendor/trainer/money/repair needs, and combat ends the session at
    // once with the weapon back. The search itself is coarse (8 rays, 5 yd
    // rings), throttled per bot, negatively cached per bot for 15 min, and
    // positively/negatively cached per map cell for 30 min across all bots.

    // Fishing casts land 10-20 yd out (donor MIN/MAX_DISTANCE_TO_WATER).
    float const FISH_MIN_CAST_DISTANCE = 10.0f;
    float const FISH_MAX_CAST_DISTANCE = 20.0f;
    // Coarse radial rings (donor used 2.5 yd / 16 dirs = 336 probes; 5 yd /
    // 8 dirs = ~88 probes for the full 60 yd window).
    float const FISH_SEARCH_STEP = 5.0f;
    // Water shallower than this cannot be fished (donor depth check).
    float const FISH_MIN_WATER_DEPTH = 0.5f;
    // Ray directions per search ring.
    int const FISH_SEARCH_DIRECTIONS = 8;
    // How far from itself a bot looks for water (donor fishingDistance /
    // fishingDistanceFromMaster). Masterless pool bots get the wide window;
    // the mastered value is kept for parity even though the direct-water path
    // below never runs for mastered bots.
    float const FISH_SEARCH_RADIUS_MASTERLESS = 40.0f;
    float const FISH_SEARCH_RADIUS_MASTERED = 10.0f;
    // A failed search parks for 15 min instead of re-scanning every tick.
    std::uint32_t const FISH_NO_WATER_RETRY_MS = 900000;
    // Minimum gap between two searches of the same bot.
    std::uint32_t const FISH_SEARCH_THROTTLE_MS = 15000;
    // Shared per-map-cell water cache lifetime across all bots.
    std::uint32_t const FISH_WATER_CACHE_MS = 1800000;
    // Side-activity budget: at most one session per hour ...
    std::uint32_t const FISH_SESSION_COOLDOWN_MS = 3600000;
    // ... of at most ~5 min ...
    std::uint32_t const FISH_SESSION_MAX_MS = 300000;
    // ... and at most 5 casts.
    int const FISH_SESSION_MAX_CASTS = 5;

    // Scope: masterless random (pool) bots only.
    inline bool FishingSpotApplies(bool isRandomBot, bool hasRealPlayerMaster)
    {
        return isRandomBot && !hasRealPlayerMaster;
    }

    inline float FishingSearchRadius(bool hasMaster)
    {
        return hasMaster ? FISH_SEARCH_RADIUS_MASTERED : FISH_SEARCH_RADIUS_MASTERLESS;
    }

    // A fishing hole (or water point) must sit inside the cast window.
    inline bool FishingCastInRange(float dist2d)
    {
        return dist2d >= FISH_MIN_CAST_DISTANCE && dist2d <= FISH_MAX_CAST_DISTANCE;
    }

    // The water surface must sit above the lakebed by a fishable depth.
    inline bool FishingWaterDeepEnough(float waterLevel, float groundLevel)
    {
        return waterLevel > groundLevel && (waterLevel - groundLevel) >= FISH_MIN_WATER_DEPTH;
    }

    // Mirrors GatherTravelDestination::IsPossible for fishing: the zone must be
    // fishable at all, and the bot's skill must cover the requirement with the
    // same -5 head start the travel errand grants.
    inline bool FishingZoneSkillOk(std::int32_t baseSkill, std::uint32_t skillValue)
    {
        if (baseSkill <= 0)
            return false;
        std::int32_t const req = baseSkill > 5 ? baseSkill - 5 : baseSkill;
        return req <= static_cast<std::int32_t>(skillValue);
    }

    // The bot fishes standing on dry land, never while swimming.
    inline bool FishingStandDry(bool standHasWater, bool groundValid)
    {
        return !standHasWater && groundValid;
    }

    // Session budget. All timestamps are `WorldTimer::getMSTime()` (uint32 ms,
    // wraps ~49 days); unsigned subtraction keeps every comparison wrap-safe.
    // No session (`sessionStartMs == 0`) counts as expired so a fresh bot is
    // startable subject only to the cooldown below.
    inline bool FishingSessionExpired(std::uint32_t sessionStartMs, int sessionCasts, std::uint32_t nowMs)
    {
        if (sessionStartMs == 0)
            return true;
        return (nowMs - sessionStartMs) >= FISH_SESSION_MAX_MS || sessionCasts >= FISH_SESSION_MAX_CASTS;
    }

    inline bool FishingSessionActive(std::uint32_t sessionStartMs, int sessionCasts, std::uint32_t nowMs)
    {
        return sessionStartMs != 0 && !FishingSessionExpired(sessionStartMs, sessionCasts, nowMs);
    }

    // A new session may start when no session is running and the hourly budget
    // is unspent. An abandoned session (expired, never formally ended) does not
    // block: the next search ends it and its cooldown starts then.
    inline bool FishingSessionStartable(std::uint32_t sessionStartMs, std::uint32_t sessionEndMs, int sessionCasts, std::uint32_t nowMs)
    {
        if (sessionStartMs != 0 && !FishingSessionExpired(sessionStartMs, sessionCasts, nowMs))
            return false;
        if (sessionEndMs != 0 && (nowMs - sessionEndMs) < FISH_SESSION_COOLDOWN_MS)
            return false;
        return true;
    }

    // Searching is allowed inside a running session (spot lost, e.g. bobber
    // looted) and for a fresh session inside the hourly budget.
    inline bool FishingSessionAllowsSearch(std::uint32_t sessionStartMs, std::uint32_t sessionEndMs, int sessionCasts, std::uint32_t nowMs)
    {
        if (FishingSessionActive(sessionStartMs, sessionCasts, nowMs))
            return true;
        return FishingSessionStartable(sessionStartMs, sessionEndMs, sessionCasts, nowMs);
    }

    inline bool FishingSearchAllowed(std::uint32_t lastSearchMs, std::uint32_t nowMs)
    {
        return lastSearchMs == 0 || (nowMs - lastSearchMs) >= FISH_SEARCH_THROTTLE_MS;
    }

    inline bool FishingWaterCacheFresh(std::uint32_t cachedAtMs, std::uint32_t nowMs)
    {
        return cachedAtMs != 0 && (nowMs - cachedAtMs) < FISH_WATER_CACHE_MS;
    }

    // Shared water-result cache key: one entry per map cell. Cell ids fit in
    // 10 bits each (512 cells per map axis), the map id in the high bits.
    inline std::uint64_t FishingCellKey(std::uint32_t mapId, std::int32_t cellX, std::int32_t cellY)
    {
        return (std::uint64_t(mapId) << 32) | (std::uint64_t(std::uint32_t(cellX)) << 16) | std::uint64_t(std::uint32_t(cellY));
    }
}
