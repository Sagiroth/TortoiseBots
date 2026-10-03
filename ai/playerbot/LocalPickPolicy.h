#pragma once

#include <cstdint>

// Local grind/camp picks (issue #424, donor mod-playerbots
// `NewRpgBaseAction::SelectRandomGrindPos` / `SelectRandomCampPos`).
//
// Donor rule (donor `src/Ai/World/Rpg/Action/NewRpgBaseAction.cpp:964-1062`):
// grind picks come from the level-bucketed POI cache (`GetLocsPerLevelCache`,
// donor `TravelMgr.h:884`) and camp picks from the per-level inn-hub cache
// (`GetTravelHubs`, donor `TravelMgr.h:880`, `:4490`), both filtered to the
// bot's map, the bot's zone (unless the bot stands in a capital), and a
// distance window - grind 500 yd near / 2500 yd far (both /3 below level 5),
// camp 500 yd at level <= 5 else 2500 yd with a 50 yd minimum push - with a
// 50% coin flip preferring the near grind set.
//
// Mapping onto our architecture (no donor status machine, destinations only):
// - Same map: already holds - `GetDestinations` drops destinations whose
//   distance reads FLT_MAX ("maps you can't path to").
// - Level bucket + same zone: superseded by stronger local gates - the grind
//   creature band (`GrindSpotPolicy.h`), the area ceiling
//   (`TravelMgr::IsLocationLevelValid`, `GrindTravelDestination::IsPossible`)
//   and the point-danger gate (`PointDangerPolicy.h`, #418/#428). Not
//   re-implemented here and must stay in force (brief constraint).
// - 50% near coin: dominated by our picker - `SetBestTarget` walks partitions
//   near to far and takes the first active point, which is a stronger near
//   bias than the donor coin, plus the crowd spread. Recorded below as a
//   constant for the record, no call site needs it.
// - 50 yd camp push: errand trips only - refusing a destination next to the
//   bot would break arrive-and-work micro-trips, so only the resting camp
//   status (a future RPG-mixer concern, not this issue) may use it.
// - Distance windows: the genuine gap. Our grind/RPG searches run with a
//   10000 yd radius, so a bot with nothing suitable nearby walks across the
//   zone while the donor caps at 2500 yd (833 below level 5, 500 camp at
//   level <= 5). Ported here as request-side caps for masterless pool bots;
//   owned/hired bots keep the full radius (their player decides), and the
//   leave-outgrown-zone grind errand keeps it too (a zone exit is a far walk
//   by design - the cap would strand it).

namespace ai
{
    // Donor grind window (`SelectRandomGrindPos`: hiRange 500, loRange 2500,
    // both /3 below level 5).
    constexpr float LOCAL_GRIND_NEAR_YD = 500.0f;
    constexpr float LOCAL_GRIND_FAR_YD = 2500.0f;

    // Donor camp window (`SelectRandomCampPos`: 500 at level <= 5, 2500 above;
    // spots within 50 yd are refused).
    constexpr float LOCAL_CAMP_BEGINNER_YD = 500.0f;
    constexpr float LOCAL_CAMP_FAR_YD = 2500.0f;
    constexpr float LOCAL_CAMP_MIN_PUSH_YD = 50.0f;

    // Donor near-set coin (`urand(1, 100) <= 50`): percent of picks drawn from
    // the near grind set when it is non-empty. Recorded, not consumed - the
    // nearest-partition pick dominates it (see above).
    constexpr int LOCAL_GRIND_NEAR_BIAS_PERCENT = 50;

    // Far edge of the local grind window for a bot of this level.
    inline float GrindLocalFarRange(std::uint32_t botLevel)
    {
        return botLevel < 5 ? LOCAL_GRIND_FAR_YD / 3.0f : LOCAL_GRIND_FAR_YD;
    }

    // Near edge of the local grind window for a bot of this level.
    inline float GrindLocalNearRange(std::uint32_t botLevel)
    {
        return botLevel < 5 ? LOCAL_GRIND_NEAR_YD / 3.0f : LOCAL_GRIND_NEAR_YD;
    }

    // Far edge of the local camp window for a bot of this level.
    inline float CampLocalRange(std::uint32_t botLevel)
    {
        return botLevel <= 5 ? LOCAL_CAMP_BEGINNER_YD : LOCAL_CAMP_FAR_YD;
    }

    // Pool (masterless random) bots only: owned/hired bots keep player control
    // and the full search radius.
    inline bool LocalPickAppliesToBot(bool masterlessRandom)
    {
        return masterlessRandom;
    }

    // Request-side search radius for an ordinary grind errand. A zone exit
    // keeps the caller's radius: leaving the valley for the next fitting field
    // is a far walk by design, and capping it would park the bot instead.
    inline float GrindRequestMaxDistance(bool leavingOutgrownZone, bool masterlessRandom,
        std::uint32_t botLevel, float defaultMax)
    {
        if (!leavingOutgrownZone && LocalPickAppliesToBot(masterlessRandom))
            return GrindLocalFarRange(botLevel);

        return defaultMax;
    }

    // Request-side search radius for a camp (GenericRpg inn-hub) errand.
    inline float CampRequestMaxDistance(bool masterlessRandom,
        std::uint32_t botLevel, float defaultMax)
    {
        if (LocalPickAppliesToBot(masterlessRandom))
            return CampLocalRange(botLevel);

        return defaultMax;
    }
}
