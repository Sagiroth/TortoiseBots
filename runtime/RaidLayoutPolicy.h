#pragma once
// The shape of a bound raid (main tank = the lead bot, off-tanks, tank healers,
// raid healers, subgroups) and the run rules that scale with its size; the glue
// is commands/Commander.cpp (bind and run facts); plain data and the standard
// library only.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots::RaidLayout
{

enum class Role : uint8_t { Tank, Healer, Damage };

constexpr uint8_t kSubgroups = 8;
constexpr uint8_t kSubgroupSize = 5;
constexpr size_t kMaxMembers = 40;
constexpr float kRaidGatherYd = 30.0f;
constexpr uint32_t kRaidGatherPct = 90;
constexpr uint32_t kRaidWipePct = 40;
constexpr uint32_t kRaidRestIgnorePct = 10;
constexpr uint32_t kMaxTankHealers = 2;

struct Member
{
    std::string name;
    Role role = Role::Damage;
    bool lead = false;
};

struct Placement
{
    std::string name;
    Role role = Role::Damage;
    uint8_t subgroup = 0;
    int tankIndex = -1;
    int healsTank = -1;
};

struct Layout
{
    bool ok = false;
    std::string error;
    std::vector<Placement> placements;
};

inline uint32_t RaidHealerMinimum(uint32_t healers) { return (healers + 2) / 3; }

inline std::vector<uint32_t> TankHealerCounts(uint32_t tanks, uint32_t healers)
{
    std::vector<uint32_t> counts(tanks, 0);
    if (tanks == 0)
        return counts;
    uint32_t minimum = RaidHealerMinimum(healers);
    uint32_t budget = healers >= minimum ? healers - minimum : 0;
    for (uint32_t t = 0; t < tanks && budget > 0; ++t)
    {
        counts[t] = 1;
        --budget;
    }
    for (uint32_t t = 0; t < tanks && budget > 0; ++t)
    {
        if (counts[t] >= kMaxTankHealers)
            continue;
        ++counts[t];
        --budget;
    }
    return counts;
}

inline Layout Plan(std::vector<Member> const& members)
{
    Layout layout;
    size_t lead = members.size();
    for (size_t i = 0; i < members.size(); ++i)
        if (members[i].lead)
        {
            lead = i;
            break;
        }
    if (lead == members.size())
    {
        layout.error = "no-lead";
        return layout;
    }
    if (members[lead].role != Role::Tank)
    {
        layout.error = "lead-not-tank";
        return layout;
    }
    if (members.size() > kMaxMembers)
    {
        layout.error = "too-many";
        return layout;
    }

    layout.placements.reserve(members.size());
    for (Member const& m : members)
        layout.placements.push_back({m.name, m.role, 0, -1, -1});

    std::vector<uint32_t> tankAt(members.size(), 0);
    std::vector<size_t> tanks;
    tanks.push_back(lead);
    for (size_t i = 0; i < members.size(); ++i)
        if (i != lead && members[i].role == Role::Tank)
            tanks.push_back(i);
    for (uint32_t t = 0; t < tanks.size(); ++t)
    {
        tankAt[tanks[t]] = t;
        layout.placements[tanks[t]].tankIndex = static_cast<int>(t);
    }

    std::vector<uint32_t> occupancy(kSubgroups, 0);
    for (uint32_t t = 0; t < tanks.size() && t < kSubgroups; ++t)
    {
        layout.placements[tanks[t]].subgroup = static_cast<uint8_t>(t);
        ++occupancy[t];
    }

    uint32_t healers = 0;
    for (Member const& m : members)
        healers += (m.role == Role::Healer) ? 1 : 0;
    std::vector<uint32_t> counts = TankHealerCounts(static_cast<uint32_t>(tanks.size()), healers);
    std::vector<size_t> raidHealers;
    for (size_t i = 0; i < members.size(); ++i)
    {
        if (members[i].role != Role::Healer)
            continue;
        uint32_t t = 0;
        while (t < counts.size() && counts[t] == 0)
            ++t;
        if (t == counts.size())
        {
            raidHealers.push_back(i);
            continue;
        }
        --counts[t];
        layout.placements[i].healsTank = static_cast<int>(t);
        if (t < kSubgroups)
        {
            layout.placements[i].subgroup = static_cast<uint8_t>(t);
            ++occupancy[t];
        }
    }

    uint32_t groups = static_cast<uint32_t>(members.size() + kSubgroupSize - 1) / kSubgroupSize;
    if (groups < 1)
        groups = 1;
    uint32_t cursor = 0;
    for (size_t i : raidHealers)
    {
        uint32_t start = cursor;
        while (occupancy[cursor] >= kSubgroupSize)
        {
            cursor = (cursor + 1) % groups;
            if (cursor == start)
                break;
        }
        if (occupancy[cursor] >= kSubgroupSize)
        {
            uint32_t room = 0;
            while (room + 1 < kSubgroups && occupancy[room] >= kSubgroupSize)
                ++room;
            cursor = room;
        }
        layout.placements[i].subgroup = static_cast<uint8_t>(cursor);
        if (cursor < kSubgroups)
            ++occupancy[cursor];
        cursor = (cursor + 1) % groups;
    }

    for (size_t i = 0; i < members.size(); ++i)
    {
        bool overflowTank = members[i].role == Role::Tank && tankAt[i] >= kSubgroups;
        if (members[i].role != Role::Damage && !overflowTank)
            continue;
        uint32_t sub = 0;
        while (sub + 1 < kSubgroups && occupancy[sub] >= kSubgroupSize)
            ++sub;
        layout.placements[i].subgroup = static_cast<uint8_t>(sub);
        ++occupancy[sub];
    }

    layout.ok = true;
    return layout;
}

inline uint32_t WipeDeadThreshold(uint32_t total, bool raid, uint32_t partyThreshold)
{
    if (!raid)
        return partyThreshold;
    uint32_t scaled = (total * kRaidWipePct + 99) / 100;
    return scaled > partyThreshold ? scaled : partyThreshold;
}

inline bool CoreDown(uint32_t tanksTotal, uint32_t tanksDead, uint32_t healersTotal, uint32_t healersDead)
{
    if (tanksTotal > 0 && tanksDead >= tanksTotal)
        return true;
    return healersTotal > 0 && healersDead * 2 > healersTotal;
}

inline bool RaidGathered(uint32_t alive, uint32_t aliveWithin, uint32_t coreAway)
{
    return coreAway == 0 && aliveWithin * 100 >= alive * kRaidGatherPct;
}

inline uint32_t RestFloor(std::vector<uint32_t> readings, uint32_t ignorePct)
{
    if (readings.empty())
        return 100;
    std::sort(readings.begin(), readings.end());
    size_t skip = readings.size() * ignorePct / 100;
    if (skip >= readings.size())
        skip = readings.size() - 1;
    return readings[skip];
}

} // namespace TortoiseBots::RaidLayout
