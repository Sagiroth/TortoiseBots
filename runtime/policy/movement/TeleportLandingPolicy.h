#pragma once

// Pure policy for the ground a bot lands on after a near teleport (no core
// types, so tools/test_teleport_landing_policy.cpp builds it standalone).
//
// A player's client makes it fall to the floor after the server moves it. A
// bot has no client, so a teleport that ends in mid-air leaves its server
// position there: Jin'do's Banish 24466 lands a bot 12 yd over the ZG pit
// floor (z 90 over 77.6) and it never paths back out. The landing is the jump
// landing's rule: a floor at or under the feet, more than a step below the
// destination. The navmesh probe under the feet is authoritative; a floor read
// off the map alone (UpdateAllowedPositionZ, which answers with the terrain
// under a WMO deck) is accepted only within the jump height limit, so a deck
// never drags a bot through a floor.

#include <cmath>
#include <cstdint>

namespace ai::TeleportLandingPolicy
{
    // Under this the destination is on its floor already (a summon dropped on the ground, a revive port, a step):
    // nothing falls.
    inline constexpr float kLandingYd = 1.0f;

    // The feet probe: centered this far below the feet and this wide, so only the navmesh at or under the bot is
    // found and a deck over its head never counts as landing ground.
    inline constexpr float kFeetProbeDepth = 4.0f;
    inline constexpr float kFeetProbeRange = 2.0f;

    inline float TeleportLandingZ(float destZ, bool floorFound, float floorZ, float jumpHeightLimit)
    {
        if (destZ - floorZ <= kLandingYd)
            return destZ; // at or above the feet: no floor below to fall to
        if (floorFound)
            return floorZ; // the navmesh's floor, however deep it is
        if (destZ - floorZ > jumpHeightLimit)
            return destZ; // terrain only, deeper than any jump: a deck above its floor, not a landing
        return floorZ;
    }

    // The simulated fall's time over `dropYd` from rest, in ms: gravity 19.2911 yd/s², terminal 60.148 yd/s.
    // The jump landing and a teleport landing share it, so the same fall lasts as long whichever started it.
    inline std::uint32_t SimulatedFallMs(float dropYd)
    {
        constexpr float kGravity = 19.2911f;
        constexpr float kTerminalVelocity = 60.148f;
        float const path = std::fabs(dropYd);
        float const terminalLength = kTerminalVelocity * kTerminalVelocity / (2.0f * kGravity);
        float const terminalFallTime = kTerminalVelocity / kGravity;
        float const seconds = path >= terminalLength
            ? (path - terminalLength) / kTerminalVelocity + terminalFallTime
            : std::sqrt(2.0f * path / kGravity);
        return static_cast<std::uint32_t>(seconds * 1000.0f);
    }
}
