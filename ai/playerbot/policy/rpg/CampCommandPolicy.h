#pragma once

#include "CampPolicy.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{

inline constexpr float CAMP_SHOW_RANGE_YD = 60.0f;
inline constexpr float CAMP_REMOVE_RANGE_YD = 10.0f;
inline constexpr uint32_t CAMP_SHOW_DURATION_SEC = 180;
inline constexpr uint32_t CAMP_FIRE_GO_ENTRY = 29784;
inline constexpr uint32_t CAMP_SEAT_MARKER_CREATURE_ENTRY = 1;
inline constexpr uint8_t CAMP_SPOT_SOURCE_SHIPPED = 0;
inline constexpr uint8_t CAMP_SPOT_SOURCE_GM = 1;
inline constexpr size_t CAMP_SPOT_NAME_MAX_LEN = 100;

inline float ComputeCamp4PointSpread(float tentZ, float castZ, float fireZ, float seatZ)
{
    float const minZ = std::min({tentZ, castZ, fireZ, seatZ});
    float const maxZ = std::max({tentZ, castZ, fireZ, seatZ});
    return maxZ - minZ;
}

inline bool IsCamp4PointTerrainLevel(float tentZ,
                                     float castZ,
                                     float fireZ,
                                     float seatZ,
                                     float maxDiffYd = CAMP_MAX_SLOPE_Z_DIFF_YD)
{
    return ComputeCamp4PointSpread(tentZ, castZ, fireZ, seatZ) <= maxDiffYd;
}

enum class CampAddCheckFailure : uint8_t
{
    None = 0,
    TooCloseToExistingSpot,
    NotOutdoors,
    InLiquid,
    NotOnNavmeshOrUnreachable,
    TerrainTooSteep
};

struct CampAddPointCheck
{
    bool outdoors = false;
    bool inLiquid = false;
    bool onNavmeshAndReachable = false;
    float z = 0.0f;
};

struct CampAddValidationInput
{
    bool nearbySpotExists = false;
    float nearestSpotDistYd = 0.0f;
    CampAddPointCheck tent;
    CampAddPointCheck cast;
    CampAddPointCheck fire;
    CampAddPointCheck seat;
};

struct CampAddValidationResult
{
    CampAddCheckFailure failure = CampAddCheckFailure::None;
    char const* failedPoint = nullptr;
    float spreadYd = 0.0f;
};

inline CampAddValidationResult EvaluateCampAddSpot(CampAddValidationInput const& input)
{
    if (input.nearbySpotExists)
        return {CampAddCheckFailure::TooCloseToExistingSpot, nullptr, 0.0f};

    struct NamedPoint
    {
        char const* label;
        CampAddPointCheck const& pt;
    };
    NamedPoint const points[] = {
        {"tent", input.tent},
        {"cast", input.cast},
        {"fire", input.fire},
        {"seat", input.seat},
    };

    for (NamedPoint const& np : points)
    {
        if (!np.pt.outdoors)
            return {CampAddCheckFailure::NotOutdoors, np.label, 0.0f};
        if (np.pt.inLiquid)
            return {CampAddCheckFailure::InLiquid, np.label, 0.0f};
        if (!np.pt.onNavmeshAndReachable)
            return {CampAddCheckFailure::NotOnNavmeshOrUnreachable, np.label, 0.0f};
    }

    float const spread = ComputeCamp4PointSpread(
        input.tent.z, input.cast.z, input.fire.z, input.seat.z);
    if (spread > CAMP_MAX_SLOPE_Z_DIFF_YD)
        return {CampAddCheckFailure::TerrainTooSteep, nullptr, spread};

    return {CampAddCheckFailure::None, nullptr, spread};
}

struct NearestCampPick
{
    int index = -1;
    float distanceYd = 0.0f;
};

inline NearestCampPick PickNearestCampSpot(uint32_t mapId,
                                           float x,
                                           float y,
                                           float z,
                                           float maxRangeYd,
                                           std::vector<CampSpotCoord> const& spots)
{
    int const idx = PickNearestCampSpotIndex(mapId, x, y, z, maxRangeYd, spots);
    if (idx < 0)
        return {-1, 0.0f};
    CampSpotCoord const& s = spots[static_cast<size_t>(idx)];
    float const dx = s.x - x;
    float const dy = s.y - y;
    float const dz = s.z - z;
    return {idx, std::sqrt(dx * dx + dy * dy + dz * dz)};
}

inline std::string FormatDefaultCampSpotName(std::string const& zoneName)
{
    std::string out = zoneName.empty() ? std::string("Camp") : (zoneName + " camp");
    if (out.size() > CAMP_SPOT_NAME_MAX_LEN)
        out.resize(CAMP_SPOT_NAME_MAX_LEN);
    return out;
}

}  // namespace TortoiseBots
