#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ai
{
    // Sampling geometry for the hard travel-route gate (RouteIsSurvivable,
    // ChooseTravelTargetAction.cpp).
    //
    // The gate used to look only at the *named* route nodes, but the bot does
    // not walk from node to node - MoveToTravelTargetAction moves to the
    // destination point with the navmesh pathfinder, the node route only
    // supplies the waypoints - so a leg that crosses a hostile town or an area
    // far above the bot's level is invisible to a per-node check. Live pool
    // 2026-10-21: the 505 yd walk leg between the "Arathi Highlands" and
    // "Arathi Highlands Bouldergor" nodes passes 0.7 yd from a Refuge Pointe
    // Defender spawn while neither endpoint is inside the 60 yd town radius,
    // and four bots died to those guards on that leg (Redforge 32, Cobble 29,
    // Wena 33, Mahu 31); the "Stormwind City Valley of Heroes" <-> "Elwynn
    // Forest Mirror Lake" leg passes 0.4 yd from a Stormwind City Guard the
    // same way. The legs are sampled instead: cheap static-index lookups along
    // the straight line, so no A*, no terrain walk beyond the area lookup the
    // waypoint rule already does, and no world scan.
    constexpr float TRAVEL_ROUTE_SAMPLE_RADIUS_YD = 60.0f;

    // Sample step. Must stay below 2 x radius so the sampled corridor covers
    // the whole leg without a gap the danger can hide in.
    constexpr float TRAVEL_ROUTE_SAMPLE_STEP_YD = 64.0f;

    // Samples per leg. A cross-continent walk leg is ~20000 yd; the cap keeps
    // one candidate's check bounded instead of scaling with the continent.
    constexpr std::uint32_t TRAVEL_ROUTE_SAMPLE_MAX = 256;

    // Area levels a walking bot may pass: an area more than this above the bot
    // is ground its route may not cross. Same +5 every other travel level gate
    // uses (destination area, quest area, RPG area, and the waypoint rule this
    // one replaces), so a point the search already accepted is never refused a
    // second time for a different margin.
    //
    // Deliberately the area level, not the nearby creature levels: a 60 yd
    // corridor along a 500 yd leg holds a 5+ level pack almost everywhere
    // (measured on the shipped link table: 62% of walk edges carry a hostile
    // spawn 6+ levels above a level-30 bot), so a creature-level rule would
    // refuse most overland travel instead of routing around the camps that
    // actually kill. Sub-areas are what mark those camps (Dragonmaw Gates 25,
    // waypoint rule already trusts.
    constexpr int TRAVEL_ROUTE_LEVEL_MARGIN = 5;

    // Already-there waypoint: same zone as the start (0 never matches).
    // Leaving the zone re-arms the rule.
    inline bool TravelRouteWaypointSharesStartZone(std::uint32_t waypointZoneId,
        std::uint32_t startZoneId)
    {
        return startZoneId != 0 && waypointZoneId == startZoneId;
    }

    // Walk-out range for the town rule (guard scan radius x5).
    constexpr float TRAVEL_ROUTE_TOWN_WALKOUT_YD = TRAVEL_ROUTE_SAMPLE_RADIUS_YD * 5.0f;

    inline bool TravelRouteWaypointInWalkout(float distanceYd)
    {
        return distanceYd >= 0.0f && distanceYd <= TRAVEL_ROUTE_TOWN_WALKOUT_YD;
    }

    // Stranded-bot walk-out: while the route stays inside the bot's start
    // ground, each rule is skipped; the first endpoint outside ends that
    // rule's exemption for the rest of the route (no re-entry).
    class RouteStartGround
    {
    public:
        RouteStartGround(bool startTooHigh, bool startInTown)
            : levelOpen(startTooHigh), townOpen(startInTown) {}

        bool LevelExempt(std::uint32_t zoneId, std::uint32_t startZoneId)
        {
            if (!levelOpen)
                return false;
            if (TravelRouteWaypointSharesStartZone(zoneId, startZoneId))
                return true;
            levelOpen = false;
            return false;
        }

        bool TownExempt(float distanceFromStartYd)
        {
            if (!townOpen)
                return false;
            if (TravelRouteWaypointInWalkout(distanceFromStartYd))
                return true;
            townOpen = false;
            return false;
        }

    private:
        bool levelOpen;
        bool townOpen;
    };

    // Whether an area rating bars the route for a bot of this level. Unknown
    // areas (GetAreaLevel's 0/unresolved) never bar it, exactly like the
    // waypoint rule.
    inline bool TravelRouteAreaTooHigh(std::int32_t areaLevel, std::uint32_t botLevel)
    {
        return areaLevel > 0 &&
            areaLevel > static_cast<std::int32_t>(botLevel) + TRAVEL_ROUTE_LEVEL_MARGIN;
    }

    // Number of samples on a leg of this length: the two endpoints plus one
    // per step between them, never fewer than two (a leg shorter than a step
    // still gets both ends checked) and never more than the cap.
    inline std::uint32_t TravelRouteSampleCount(float legLengthYd)
    {
        if (!(legLengthYd > 0.0f))
            return 1;

        float const steps = std::ceil(legLengthYd / TRAVEL_ROUTE_SAMPLE_STEP_YD);
        float const wanted = std::max(2.0f, steps + 1.0f);
        return static_cast<std::uint32_t>(
            std::min(static_cast<float>(TRAVEL_ROUTE_SAMPLE_MAX), wanted));
    }

    // Position of sample `index` on the leg as a 0..1 fraction of its length,
    // evenly spaced with both endpoints included.
    inline float TravelRouteSampleFraction(std::uint32_t index, std::uint32_t count)
    {
        if (count <= 1)
            return 0.0f;

        return static_cast<float>(index) / static_cast<float>(count - 1);
    }
}
