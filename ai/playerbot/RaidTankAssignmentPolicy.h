#pragma once
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace ai
{
    struct RaidTankCandidate
    {
        std::uint64_t guid;
        bool bossRole;
        bool addRole;
        std::uint64_t lockedTarget = 0;
    };

    struct GolemaggTankPlan
    {
        std::uint64_t bossTank = 0;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> addOwners;
    };

    // Inputs contain only live, same-instance participants. Locks are explicit
    // orders (or the human currently holding the boss), not automatic targets.
    inline GolemaggTankPlan PlanGolemaggTanks(std::vector<RaidTankCandidate> tanks,
        std::uint64_t boss, std::vector<std::uint64_t> adds)
    {
        std::sort(tanks.begin(), tanks.end(), [](auto const& a, auto const& b) { return a.guid < b.guid; });
        std::sort(adds.begin(), adds.end());
        adds.erase(std::unique(adds.begin(), adds.end()), adds.end());
        GolemaggTankPlan plan;
        for (auto const& tank : tanks)
            if (tank.lockedTarget == boss)
            {
                plan.bossTank = tank.guid;
                break;
            }
        if (!plan.bossTank)
            for (auto const& tank : tanks)
                if (tank.bossRole && !tank.lockedTarget)
                {
                    plan.bossTank = tank.guid;
                    break;
                }
        if (!plan.bossTank)
            for (auto const& tank : tanks)
                if (tank.addRole && !tank.lockedTarget)
                {
                    plan.bossTank = tank.guid;
                    break;
                }
        std::vector<std::uint64_t> freeAddTanks;
        for (auto const& tank : tanks)
            if (tank.guid != plan.bossTank && tank.addRole && !tank.lockedTarget)
                freeAddTanks.push_back(tank.guid);
        unsigned next = 0;
        for (auto add : adds)
        {
            std::uint64_t owner = 0;
            for (auto const& tank : tanks)
                if (tank.guid != plan.bossTank && tank.lockedTarget == add)
                {
                    owner = tank.guid;
                    break;
                }
            if (!owner && !freeAddTanks.empty())
                owner = freeAddTanks[next++ % freeAddTanks.size()];
            plan.addOwners.push_back({ add, owner });
        }
        return plan;
    }
}
