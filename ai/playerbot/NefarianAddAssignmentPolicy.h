#pragma once
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace ai
{
    struct NefarianAddTankCandidate
    {
        std::uint64_t guid;
        std::uint64_t lockedTarget = 0;
        std::vector<std::uint64_t> possibleAdds;
    };

    // Assign only targets each candidate can currently see and attack. Locks
    // represent an existing tank victim; candidate locks are explicit orders.
    inline std::vector<std::pair<std::uint64_t, std::uint64_t>> PlanNefarianAdds(
        std::vector<NefarianAddTankCandidate> tanks,
        std::vector<std::uint64_t> adds,
        std::vector<std::pair<std::uint64_t, std::uint64_t>> tankVictimLocks)
    {
        std::sort(tanks.begin(), tanks.end(), [](auto const& a, auto const& b) { return a.guid < b.guid; });
        std::sort(adds.begin(), adds.end());
        adds.erase(std::unique(adds.begin(), adds.end()), adds.end());
        std::sort(tankVictimLocks.begin(), tankVictimLocks.end());

        for (auto& tank : tanks)
        {
            std::sort(tank.possibleAdds.begin(), tank.possibleAdds.end());
            tank.possibleAdds.erase(std::unique(tank.possibleAdds.begin(), tank.possibleAdds.end()), tank.possibleAdds.end());
        }

        std::vector<std::pair<std::uint64_t, std::uint64_t>> plan;
        std::vector<std::pair<std::uint64_t, std::size_t>> assignedCounts;
        for (auto const& tank : tanks)
            assignedCounts.push_back({ tank.guid, 0 });
        for (std::uint64_t add : adds)
        {
            std::uint64_t owner = 0;
            for (auto const& lock : tankVictimLocks)
                if (lock.first == add)
                {
                    owner = lock.second;
                    break;
                }
            if (!owner)
                for (auto const& tank : tanks)
                    if (tank.lockedTarget == add)
                    {
                        owner = tank.guid;
                        break;
                    }

            std::vector<std::uint64_t> available;
            if (!owner)
                for (auto const& tank : tanks)
                    if (!tank.lockedTarget && std::binary_search(tank.possibleAdds.begin(), tank.possibleAdds.end(), add))
                        available.push_back(tank.guid);
            if (!owner && !available.empty())
            {
                std::size_t leastAssigned = static_cast<std::size_t>(-1);
                for (std::uint64_t candidate : available)
                {
                    auto count = std::find_if(assignedCounts.begin(), assignedCounts.end(),
                        [candidate](auto const& item) { return item.first == candidate; });
                    if (count != assignedCounts.end())
                        leastAssigned = std::min(leastAssigned, count->second);
                }
                auto selected = std::find_if(available.begin(), available.end(), [&](std::uint64_t candidate)
                {
                    auto count = std::find_if(assignedCounts.begin(), assignedCounts.end(),
                        [candidate](auto const& item) { return item.first == candidate; });
                    return count != assignedCounts.end() && count->second == leastAssigned;
                });
                owner = selected == available.end() ? 0 : *selected;
            }

            if (owner)
            {
                auto count = std::find_if(assignedCounts.begin(), assignedCounts.end(),
                    [owner](auto const& item) { return item.first == owner; });
                if (count != assignedCounts.end())
                    ++count->second;
            }

            plan.push_back({ add, owner });
        }
        return plan;
    }
}
