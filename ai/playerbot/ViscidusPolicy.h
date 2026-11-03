#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>
namespace ai
{
    inline unsigned ViscidusMergeTier(float distance) { return unsigned(std::max(0.0f, distance) / 3.0f); }
    inline bool ViscidusGlobBefore(float merge, float local, uint64_t guid,
        float bestMerge, float bestLocal, uint64_t bestGuid)
    {
        if (ViscidusMergeTier(merge) != ViscidusMergeTier(bestMerge))
            return ViscidusMergeTier(merge) < ViscidusMergeTier(bestMerge);
        if (std::fabs(local - bestLocal) > 0.5f) return local < bestLocal;
        return guid < bestGuid;
    }
    inline float ViscidusRingAngle(unsigned slot, unsigned count)
    {
        return count ? 6.28318530718f * slot / count : 0.0f;
    }
    // Freeze is the encounter's damage window, not crowd control to preserve.
    inline bool ViscidusShatterWindow(uint32_t map, uint32_t entry, bool enabled,
        bool alive, bool engaged, bool frozen)
    {
        return map == 531 && entry == 15299 && enabled && alive && engaged && frozen;
    }
    inline bool ViscidusHoldDamage(bool window, bool harmful, bool frost, bool waitingCaster)
    {
        return window && harmful && (frost || waitingCaster);
    }
    inline bool ViscidusDpsGlob(bool enabled, bool encounter, bool tank, bool healer,
        bool alive, bool attackable)
    {
        return enabled && encounter && !tank && !healer && alive && attackable;
    }
}
