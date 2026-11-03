#pragma once
#include <cstdint>
namespace ai
{
struct PatchwerkOpeningWindow
{
    uint64_t encounter = 0;
    uint32_t started = 0;
    bool engaged = false;
    bool pending = false;

    void Observe(uint64_t id, bool combat, bool ordered, uint32_t now)
    {
        if (!id) { *this = {}; return; }
        if (id != encounter || (!combat && engaged)) { *this = {}; encounter = id; }
        pending = ordered && !combat;
        if (combat && !engaged) { engaged = true; started = now; }
    }
    bool Holds(uint32_t now, uint32_t delayMs) const
    {
        return delayMs && (pending || (engaged && uint32_t(now - started) < delayMs));
    }
};
}
