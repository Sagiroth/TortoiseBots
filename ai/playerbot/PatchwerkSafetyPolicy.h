#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace ai {
struct PatchwerkTankState { uint64_t guid; float threat; };
inline uint64_t PatchwerkChooseMain(uint64_t previous, uint64_t victim, std::vector<PatchwerkTankState> const& tanks)
{
    for (auto const& tank : tanks) if (tank.guid == previous) return previous;
    for (auto const& tank : tanks) if (tank.guid == victim) return victim;
    if (!previous) return 0;
    uint64_t next = 0; float highest = -1;
    for (auto const& tank : tanks) if (tank.threat > highest) { next = tank.guid; highest = tank.threat; }
    return next;
}
// A reserve below the requested 80% ceiling absorbs damage already in flight.
inline bool PatchwerkDamageBudget(float mine, float holder, float lowestTank, unsigned tanks,
    uint32_t age, bool held, unsigned requiredTanks = 4)
{
    if (tanks < requiredTanks || age < 4000 || holder < 10000 || lowestTank < 5000) return false;
    float limit = (held ? 0.60f : 0.70f) * (holder < lowestTank ? holder : lowestTank);
    float reserve = 0.80f * lowestTank - 3000.0f;
    return mine < limit && mine < reserve;
}
inline std::size_t PatchwerkHealerSeat(std::size_t healer, std::size_t tanks)
{
    return tanks ? healer % tanks : 0;
}
inline bool PatchwerkSoakerHealthSafe(uint32_t health, uint32_t dpsHealth)
{
    return uint64_t(health) > uint64_t(dpsHealth) + 500;
}
}
