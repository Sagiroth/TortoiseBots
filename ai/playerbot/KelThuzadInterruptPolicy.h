#pragma once
#include <cstdint>

namespace ai
{
// The pinned encounter repeats Frostbolt no sooner than five seconds.
// Keep attempts together for four seconds, beyond its two-second cast.
struct KelThuzadInterruptWindow
{
    uint32_t started = 0;
    uint64_t first = 0;
    unsigned attempts = 0;

    void Observe(uint32_t now)
    {
        if (attempts && uint32_t(now - started) >= 4000)
        {
            attempts = 0;
            first = 0;
        }
    }
    bool Ready(uint64_t caller, uint32_t now) const
    {
        return caller && (!attempts || (attempts == 1 && caller != first &&
            uint32_t(now - started) >= 400));
    }
    bool Claim(uint64_t caller, uint64_t selected, uint32_t now)
    {
        Observe(now);
        if (caller != selected || !Ready(caller, now))
            return false;
        if (!attempts)
        {
            started = now;
            first = caller;
        }
        ++attempts;
        return true;
    }
};
}
