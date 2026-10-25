#pragma once
#include <cstdint>

namespace ai
{
// Only a wound-free tank may take the next turn. A third tank is a spare,
// not a reason to taunt a healthy current tank.
inline bool FankrissNeedsSwap(unsigned victimStacks, unsigned volunteerStacks,
    unsigned threshold, bool victimIsTank)
{
    return volunteerStacks == 0 && (!victimIsTank || victimStacks >= threshold);
}

class FankrissSwapWindow
{
public:
    bool Ready(std::uint32_t now) const { return !attempted || now - lastAttempt >= 2000; }
    bool Claim(std::uint64_t caller, std::uint64_t chosen, std::uint32_t now)
    {
        if (!chosen || caller != chosen || !Ready(now)) return false;
        attempted = true;
        lastAttempt = now;
        return true;
    }
private:
    bool attempted = false;
    std::uint32_t lastAttempt = 0;
};
}
