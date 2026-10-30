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

// A hire joining a raid that is already bound (C6): only the new bot is placed, nobody else moves (a
// reshuffle in the middle of a raid breaks heal assignments and positions). seated: members now in each
// subgroup, the joiner not counted; tankIn / healerIn: the subgroup holds a tank / a healer.
struct JoinFacts
{
    Role role = Role::Damage;
    uint8_t seated[kSubgroups] = {};
    bool tankIn[kSubgroups] = {};
    bool healerIn[kSubgroups] = {};
};

struct JoinSeat
{
    bool ok = false; // false: the raid has no free seat
    uint8_t subgroup = 0;
};

// A tank takes the first subgroup that already holds a tank and has a free seat, a healer the first
// that holds a healer; a damage dealer, or a role whose subgroups are full, the first free seat.
inline JoinSeat PlanJoinerSubgroup(JoinFacts const& facts)
{
    JoinSeat seat;
    if (facts.role != Role::Damage)
        for (uint8_t sub = 0; sub < kSubgroups; ++sub)
            if (facts.seated[sub] < kSubgroupSize && (facts.role == Role::Tank ? facts.tankIn[sub] : facts.healerIn[sub]))
            {
                seat.ok = true;
                seat.subgroup = sub;
                return seat;
            }
    for (uint8_t sub = 0; sub < kSubgroups; ++sub)
        if (facts.seated[sub] < kSubgroupSize)
        {
            seat.ok = true;
            seat.subgroup = sub;
            return seat;
        }
    return seat;
}

// The tank (index in the raid's tank list, the lead bot first) a joining healer heals: the one with the
// fewest healers, the first on a tie. -1: a raid healer, because the raid keeps its healer minimum
// (RaidHealerMinimum of the healers) before tanks take more, or every tank has the most a tank takes.
inline int PickJoinerHealTank(std::vector<uint32_t> const& healersPerTank, uint32_t raidHealers)
{
    uint32_t healers = raidHealers;
    for (uint32_t count : healersPerTank)
        healers += count;
    if (raidHealers < RaidHealerMinimum(healers + 1))
        return -1;
    int best = -1;
    for (size_t t = 0; t < healersPerTank.size(); ++t)
    {
        if (healersPerTank[t] >= kMaxTankHealers)
            continue;
        if (best < 0 || healersPerTank[t] < healersPerTank[static_cast<size_t>(best)])
            best = static_cast<int>(t);
    }
    return best;
}

// Subgroup moves that never overfill (Group::ChangeMembersGroup does nothing
// when the target subgroup already holds 5): given each member's current and
// planned subgroup, the ordered ops that place every placeable member
// ({Move(member, sub) | Swap(a, b)}). A member whose target is full swaps
// with a member of that subgroup whose own plan takes it elsewhere; members
// already in place get no op. Best effort: a member with no room and no swap
// partner is left out for the caller to report. Pure: the caller applies the
// ops (ChangeMembersGroup / SwapMembersGroup) and reads the result back.
struct SubgroupMember
{
    uint64_t id = 0;
    uint8_t current = 0;
    uint8_t planned = 0;
};

struct SubgroupOp
{
    bool swap = false; // false: Move(member, sub); true: Swap(member, other)
    uint64_t member = 0;
    uint64_t other = 0; // swap partner, Move ignores it
    uint8_t sub = 0;    // move target, Swap ignores it
};

inline std::vector<SubgroupOp> PlanSubgroupMoves(std::vector<SubgroupMember> const& members)
{
    struct Sim
    {
        uint64_t id;
        uint8_t at;
        uint8_t planned;
    };
    std::vector<Sim> sim;
    sim.reserve(members.size());
    uint32_t counts[kSubgroups] = {};
    for (SubgroupMember const& m : members)
    {
        if (m.planned >= kSubgroups)
            continue;
        uint8_t const at = m.current < kSubgroups ? m.current : kSubgroups;
        if (at < kSubgroups)
            ++counts[at];
        sim.push_back({m.id, at, m.planned});
    }
    std::vector<SubgroupOp> ops;
    ops.reserve(sim.size());
    for (size_t pass = 0; pass <= sim.size() * 2 + 1; ++pass)
    {
        bool moved = false;
        for (Sim& s : sim)
        {
            if (s.at == s.planned)
                continue;
            if (counts[s.planned] < kSubgroupSize)
            {
                if (s.at < kSubgroups)
                    --counts[s.at];
                ++counts[s.planned];
                s.at = s.planned;
                ops.push_back({false, s.id, 0, s.planned});
                moved = true;
                continue;
            }
            if (s.at >= kSubgroups)
                continue;
            Sim* partner = nullptr;
            for (Sim& o : sim)
                if (o.id != s.id && o.at == s.planned && o.planned != s.planned)
                {
                    partner = &o;
                    break;
                }
            if (!partner)
                continue;
            if (partner->planned == s.at)
            {
                partner->at = s.at;
                s.at = s.planned;
                ops.push_back({true, s.id, partner->id, 0});
                moved = true;
                continue;
            }
            // The partner's own plan aims elsewhere but not here: free the
            // target with a direct move when it has room, else relay the
            // partner into its plan through a non-target occupant, which the
            // direct move or a later swap then places.
            if (counts[partner->planned] < kSubgroupSize)
            {
                if (partner->at < kSubgroups)
                    --counts[partner->at];
                ++counts[partner->planned];
                ops.push_back({false, partner->id, 0, partner->planned});
                partner->at = partner->planned;
                moved = true;
                continue;
            }
            Sim* relay = nullptr;
            for (Sim& o : sim)
                if (o.id != s.id && o.id != partner->id && o.at == partner->planned && o.planned != partner->planned)
                {
                    relay = &o;
                    break;
                }
            if (!relay)
                continue;
            relay->at = partner->at;
            partner->at = partner->planned;
            ops.push_back({true, partner->id, relay->id, 0});
            moved = true;
        }
        if (!moved)
            break;
    }
    return ops;
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
