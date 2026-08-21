#pragma once
#include <cstdint>

// TortoiseBots behavior constants — single source of truth for tuning.
// Values are derived from cmangos-playerbots PlayerbotAIConfig (followDistance 1.5y,
// sightDistance 75y, reactDistance 150y, contactDistance 0.5y) but owned here.

namespace TortoiseBots {

namespace Config {

constexpr float FollowDistance = 1.5f;          // yards — dead-zone radius (cmangos PlayerbotAIConfig.cpp:140)
constexpr float ContactDistance = 0.5f;         // yards — hysteresis for stop
constexpr float SightDistance = 75.0f;          // yards — not used for follow, but for validation later
constexpr uint32_t FollowUpdateIntervalMs = 200;  // ms — follow tick throttle (discovery §8: 200-250ms)
constexpr uint32_t IdleUpdateIntervalMs = 1000;   // ms — when already close

// Follow angle: behind the master (M_PI) — single bot sits behind owner.
// For multi-bot formations this will become per-bot offset; for now constant.
constexpr float FollowAngle = 3.1415926535f; // M_PI

} // namespace Config

} // namespace TortoiseBots
