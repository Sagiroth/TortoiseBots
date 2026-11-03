#pragma once
#include <algorithm>
#include <cmath>
#include <string>

namespace ai
{
inline float FankrissHealingSearchRange(float ordinary, bool active)
{
    // Search the bounded raid roster beyond the usual two-heal-range envelope
    // after a web. This is candidate discovery, never a spell range override.
    return active ? std::max(ordinary, 200.0f) : ordinary;
}
struct FankrissRearPoint { float x, y; };
inline FankrissRearPoint FankrissRear(float x, float y, float facing, float meleeReach)
{
    float const radius = meleeReach * 0.7f;
    return {x - std::cos(facing) * radius, y - std::sin(facing) * radius};
}
// Core boss_fankriss.cpp pullCenter. Both swap tanks use the same front.
inline FankrissRearPoint FankrissCenterTank(float meleeReach)
{
    return {-8074.88f + std::max(1.0f, meleeReach - 0.5f), 1193.64f};
}
inline FankrissRearPoint FankrissRaidSlot(float x, float y, float facing, float meleeReach, bool hunter)
{
    float radius = hunter ? meleeReach + 12.0f : std::min(7.0f, meleeReach * 0.7f);
    return {x - std::cos(facing) * radius, y - std::sin(facing) * radius};
}
inline bool FankrissSpawnInRadius(float x, float y, float bossX, float bossY)
{
    return std::hypot(x - bossX, y - bossY) <= 30.0f;
}
inline FankrissRearPoint FankrissStackEdge(FankrissRearPoint rear, float x, float y,
    float bossX, float bossY, float facing, float reach)
{
    float dx = x - rear.x, dy = y - rear.y, distance = std::hypot(dx, dy);
    float step = std::min(3.0f, std::max(0.0f, distance - reach * 0.8f));
    if (distance < 0.01f) return rear;
    FankrissRearPoint goal{rear.x + dx * step / distance, rear.y + dy * step / distance};
    // Never step around to the boss's front to reach an incoming Spawn.
    return (goal.x - bossX) * std::cos(facing) + (goal.y - bossY) * std::sin(facing) < -1.0f ? goal : rear;
}
inline bool FankrissAtRear(float x, float y, FankrissRearPoint goal, bool melee, bool behind)
{
    return melee && behind && std::hypot(x - goal.x, y - goal.y) <= 1.5f;
}
inline bool FankrissOrdinaryMovement(std::string const& name)
{
    return name == "follow" || name == "follow master" || name == "formation station" ||
        name == "move to position" || name == "reach melee" || name == "reach spell" ||
        name == "reach target" || name == "set behind" || name == "move out of contact" ||
        name == "move out of enemy contact" || name == "flee" || name == "flee to master";
}
inline bool FankrissMovementBlocked(std::string const& name, bool stack, bool healing, bool healHere)
{
    if (stack && (name == "charge" || name == "intercept" || name == "feral charge" || FankrissOrdinaryMovement(name) || name == "dps aoe" || name == "reach party member to heal")) return true;
    if (!healing) return false;
    if (name == "reach party member to heal") return healHere;
    if (name == "flee" || name == "flee to master") return healHere;
    return FankrissOrdinaryMovement(name);
}
}
