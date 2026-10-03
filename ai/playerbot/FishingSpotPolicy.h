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

    // Fishing casts land 10-20 yd out (donor MIN/MAX_DISTANCE_TO_WATER).
    float const FISH_MIN_CAST_DISTANCE = 10.0f;
    float const FISH_MAX_CAST_DISTANCE = 20.0f;
    // Radial search step (donor SEARCH_INCREMENT).
    float const FISH_SEARCH_STEP = 2.5f;
    // Water shallower than this cannot be fished (donor depth check).
    float const FISH_MIN_WATER_DEPTH = 0.5f;
    // Ray directions per search ring (donor default).
    int const FISH_SEARCH_DIRECTIONS = 16;
    // How far from itself a bot looks for water (donor fishingDistance /
    // fishingDistanceFromMaster). Masterless pool bots get the wide window;
    // the mastered value is kept for parity even though the direct-water path
    // below never runs for mastered bots.
    float const FISH_SEARCH_RADIUS_MASTERLESS = 40.0f;
    float const FISH_SEARCH_RADIUS_MASTERED = 10.0f;
    // A failed search parks for a minute instead of re-scanning every tick.
    std::uint32_t const FISH_NO_WATER_RETRY_MS = 60000;

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
}
