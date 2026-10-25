#pragma once
#include <cmath>
#include <cstdint>
#include <string>

namespace ai
{
struct GeddonBombPoint { float x = 0, y = 0, z = 0; };
inline float GeddonBombDistance(GeddonBombPoint a, GeddonBombPoint b)
{
    return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));
}
enum class GeddonBombPhase { Idle, Outbound, Waiting, Returning };
struct GeddonBombState
{
    bool configured = false;
    uint32_t instance = 0, group = 0, returnStarted = 0;
    int64_t application = 0;
    GeddonBombPoint spot, home, waitingPoint;
    bool hasWaitingPoint = false;
    GeddonBombPhase phase = GeddonBombPhase::Idle;
    bool failureLogged = false;

    void Clear() { *this = GeddonBombState(); }
    void Arm(GeddonBombPoint point, uint32_t instanceId, uint32_t groupId)
    {
        Clear(); configured = true; spot = point; instance = instanceId; group = groupId;
    }
    bool InScope(uint32_t map, uint32_t instanceId, uint32_t groupId) const
    {
        return configured && map == 409 && instance == instanceId && group == groupId;
    }
    bool Active() const { return phase != GeddonBombPhase::Idle; }
    void Observe(bool bomb, int64_t applied, GeddonBombPoint current, uint32_t now)
    {
        if (!configured) return;
        if (bomb)
        {
            if (!Active()) home = current;
            if (!Active() || application != applied)
            {
                failureLogged = false;
                hasWaitingPoint = false;
            }
            application = applied;
            phase = hasWaitingPoint && GeddonBombDistance(current, waitingPoint) <= 1.5f ? GeddonBombPhase::Waiting : GeddonBombPhase::Outbound;
        }
        else if (Active())
        {
            if (phase != GeddonBombPhase::Returning) { returnStarted = now; phase = GeddonBombPhase::Returning; }
            if (GeddonBombDistance(current, home) <= 1.5f || uint32_t(now-returnStarted) >= 30000)
                phase = GeddonBombPhase::Idle;
        }
    }
};
inline bool GeddonBombAllowsAction(std::string const& name)
{
    return name == "geddon bomb move" || name == "set dead state" ||
        name == "set combat state" || name == "set non combat state";
}
}
