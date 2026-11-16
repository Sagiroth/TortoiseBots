#pragma once

#include <cstdint>
#include <vector>

namespace TortoiseBots
{

// Pool characters below level 60 the lineages keep in stock: reserve times the
// level ladder's online target for those bands.
inline uint64_t LineageLowBandWanted(uint32_t lowBandTarget, uint32_t reserve)
{
    return uint64_t(lowBandTarget) * reserve;
}

// Maximum pool size; at or above it no alt is rolled. The multiple is at least 1,
// so the ceiling never sits below the online target itself.
inline uint64_t LineagePoolCeiling(uint32_t onlineTarget, uint32_t multiple)
{
    return uint64_t(onlineTarget) * (multiple ? multiple : 1);
}

enum class LineageRollGate
{
    Roll,
    Enough,      // the low bands hold the wanted stock
    NotYet,      // the hourly rate spaces alts out, the next one is not due
    Ceiling,     // the pool reached the safety ceiling
    RateZero     // LineagePerHour = 0
};

// Whether one alt may be rolled now. Alts are spaced evenly over the hour
// (3600 / perHour seconds apart) instead of a burst at the start of each hour;
// lastCreatedAt survives a restart because it comes from the lineage table.
inline LineageRollGate CheckLineageRoll(uint32_t lowPool, uint64_t lowWanted, uint64_t poolTotal,
    uint64_t ceiling, int64_t now, int64_t lastCreatedAt, uint32_t perHour)
{
    if (poolTotal >= ceiling)
        return LineageRollGate::Ceiling;
    if (lowPool >= lowWanted)
        return LineageRollGate::Enough;
    if (!perHour)
        return LineageRollGate::RateZero;
    int64_t const spacing = 3600 / int64_t(perHour);
    if (lastCreatedAt && now < lastCreatedAt + spacing)
        return LineageRollGate::NotYet;
    return LineageRollGate::Roll;
}

struct LineageParentOption
{
    uint32_t guid = 0;
    uint32_t lineageSize = 1; // founder plus every alt of the line
};

// The level 60 with the smallest lineage rolls the next alt; the roll picks
// among the ties so no single line is always first. 0 when there is none.
inline uint32_t PickLineageParent(std::vector<LineageParentOption> const& options, uint32_t roll)
{
    uint32_t smallest = UINT32_MAX;
    uint32_t ties = 0;
    for (LineageParentOption const& o : options)
    {
        if (o.lineageSize < smallest)
        {
            smallest = o.lineageSize;
            ties = 1;
        }
        else if (o.lineageSize == smallest)
            ++ties;
    }
    if (!ties)
        return 0;
    uint32_t pick = roll % ties;
    for (LineageParentOption const& o : options)
        if (o.lineageSize == smallest && pick-- == 0)
            return o.guid;
    return 0;
}

// A class the lineage does not have yet (takenMask bit = 1 << (class - 1));
// any valid class once all are taken. 0 when no class is valid.
inline uint8_t PickLineageClass(uint32_t takenMask, std::vector<uint8_t> const& validClasses, uint32_t roll)
{
    std::vector<uint8_t> fresh;
    for (uint8_t cls : validClasses)
        if (cls && !(takenMask & (1u << (cls - 1))))
            fresh.push_back(cls);
    std::vector<uint8_t> const& pool = fresh.empty() ? validClasses : fresh;
    if (pool.empty())
        return 0;
    return pool[roll % pool.size()];
}

} // namespace TortoiseBots
