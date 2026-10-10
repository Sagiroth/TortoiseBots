#pragma once

#include <cmath>

// Pure decision rules for the Sapphiron fight (mod-playerbots parity:
// NaxxActions_Sapphiron.cpp + SapphironBossHelper, constants re-derived
// for vanilla). Key vanilla difference: Sapphiron HOVERS (MOVEFLAG_HOVER,
// SetFly commented out in boss_sapphiron.cpp) — the donor's IsFlying()
// check ports as a hover check. IDs verified in tw_world: boss 15989,
// Icebolt 28522, Chill 28534/28547, Blizzard NPC 16474, Ice Block GO
// 181247, Tail Sweep 15847.

namespace ai
{
// Hide spot: behind the iceblocked player relative to the boss, 3yd out
// (donor MoveToNearestIcebolt: member pos + 3yd along boss->member angle).
inline void SapphironHideSpot(float bossX, float bossY, float memberX,
    float memberY, float& outX, float& outY)
{
    const float dx = memberX - bossX;
    const float dy = memberY - bossY;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f)
    {
        outX = memberX + 3.0f;
        outY = memberY;
        return;
    }
    outX = memberX + dx / len * 3.0f;
    outY = memberY + dy / len * 3.0f;
}

// Blizzard step-out: directly away from the Blizzard NPC, 10yd.
inline void SapphironBlizzardExit(float blizX, float blizY, float botX,
    float botY, float& outX, float& outY)
{
    const float dx = botX - blizX;
    const float dy = botY - blizY;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f)
    {
        outX = botX + 10.0f;
        outY = botY;
        return;
    }
    outX = botX + dx / len * 10.0f;
    outY = botY + dy / len * 10.0f;
}
} // namespace ai
