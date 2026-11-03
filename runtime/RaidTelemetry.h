#pragma once

// Raids tab telemetry (spec dash-raids-tab, decided 2026-11-02): one RAIDS datagram set per 2 s snapshot cycle lists every
// raid map copy that holds a bot, without a commander binding. The roster already carries each member's map, instance,
// group, subgroup, vitals and position, and DUNGEON_MOBS carries the mobs of every instance copy (raids included), so this
// datagram carries only what those two lack: the group id and fight key, the map copy, the leader, the bound commander when
// one stands on the group, the member list (guid and subgroup, plus a name and an offline flag for members not online) and
// the live combat window raw JSON.
//
// Wire format (additive: an older dashboard drops the unknown type; the protocol version is unchanged):
//   {"v":12,"session":S,"seq":N[,"part":i,"parts":n],"ts":T,"ts_ms":M,"type":"RAIDS","raids":[
//     {"group":123,"key":"raid#123","map":409,"instance":7,"rg":true,"leader":"Celindra","leader_guid":4567,
//      "commander":"Hypeer",
//      "members":[{"g":4567,"s":0},{"g":4570,"s":1,"n":"Alez","off":true}],
//      "more":2,
//      "combat":{...},
//      "trimmed":"combat"}]}
// `key` is the fight key: the bound commander's display name when a binding stands on the group, else "raid#<group id>".
// One part carries no `part`/`parts` fields. A group is never split across parts. One "raids":[] datagram follows the cycle
// in which the last group ended.
//
// Standard library only (Player is a forward declaration), so tools/test_raid_telemetry.cpp compiles it without a game
// header.

#include "CommanderTelemetry.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

class Player;

namespace TortoiseBots {
namespace RaidTelemetry {

constexpr size_t kMaxRaidGroupsPerCycle = 40; // more groups take turns (RotationWindow), each gets a turn
constexpr size_t kMaxMembersPerGroup = 40;    // MAX_RAID_SIZE; a guard only, the overflow is counted in `more`
constexpr size_t kMaxRaidName = 24;           // the leader's and an offline member's name
constexpr size_t kRaidPartByteCap = 16000;    // one serialized part (a 40-man group is at most ~7.6 KB, so two fit)
constexpr size_t kDatagramByteLimit = 65507;  // the most one IPv4 UDP payload carries
static_assert(kRaidPartByteCap < kDatagramByteLimit, "a part stays inside one datagram");

// The ladder of one group that alone still goes past the part byte cap, in this fixed order: the window, then the offline
// members' names, then members from the end (counted in `more`). The member rows themselves go last because they are what
// the tab lists.
constexpr uint8_t kMaxRaidTrimLevel = 3;

inline char const* TrimSectionName(uint8_t level)
{
    switch (level)
    {
    case 1: return "combat";
    case 2: return "names";
    case 3: return "members";
    default: return "none";
    }
}

// One member of a group as the group slots hold it (a member on another map or offline is listed too).
struct MemberRef
{
    uint32_t guid = 0;
    int sub = 0;           // the raid subgroup (0..7)
    bool online = false;   // in the world at collection time
    std::string name;      // only written (and only read) for a member not online
};

// The ready check and boss debuffs of one member (G2): the same wire
// objects the bound path writes, keyed by low guid. Sent at most every
// 5 s, appended at the END of the RAIDS group object.
struct DetailEntry
{
    uint32_t guid = 0;
    bool hasReady = false;
    uint32_t readyBuffs = 0;
    uint32_t readyCons = 0;
    uint8_t cdReady = 0;
    uint8_t cdKnown = 0;
    uint32_t cdLongestS = 0;
    std::vector<CommanderTelemetry::AuraInfo> debuffs;
    bool mc = false;
};

// One qualifying group as it goes on the wire. The commands layer fills it; the emitters below only serialize it.
struct GroupInfo
{
    uint32_t groupId = 0;
    uint32_t map = 0;
    uint32_t instance = 0;
    bool raidGroup = false;    // the core's raid group type (`rg`); a plain party on a raid map is false
    std::string leader;
    uint32_t leaderGuid = 0;
    std::string commander;     // the bound commander's display name while a binding stands on the group, else empty
    std::vector<MemberRef> members;
    std::string combatJson;    // the live window raw JSON while the group's book is in a fight, else empty
    std::vector<DetailEntry> detail; // G2: ready + debuffs, filled at most every 5 s, empty when off or not due
};

// One group trimmed to fit a part, for the caller's log (once per group per minute).
struct TrimEvent
{
    uint32_t groupId = 0;
    std::string key;
    uint8_t level = 0;
};

// The fight key of a group: the commander's display name when a binding stands on it (the name its fights are filed under),
// else "raid#<group id>". A character name cannot contain '#', so the two never collide.
inline std::string FightKey(GroupInfo const& group)
{
    if (!group.commander.empty())
        return group.commander;
    return "raid#" + std::to_string(group.groupId);
}

// Whether one unit makes its group a raid run: in the world, outside a battleground, on a raid map (the core's MapEntry
// instance type), inside an instance copy.
inline bool Qualifies(bool inWorld, bool inBg, bool raidMap, uint32_t instance)
{
    return inWorld && !inBg && raidMap && instance != 0;
}

// Which of `total` groups are sent this cycle: all of them up to `max`, else `max` of them starting at offset (mod total),
// wrapping. The caller advances the offset by `max` each cycle so every group takes its turn.
inline std::vector<size_t> RotationWindow(size_t total, size_t offset, size_t max = kMaxRaidGroupsPerCycle)
{
    std::vector<size_t> out;
    if (total == 0 || max == 0)
        return out;
    if (total <= max)
    {
        out.reserve(total);
        for (size_t i = 0; i < total; ++i)
            out.push_back(i);
        return out;
    }
    out.reserve(max);
    size_t const start = offset % total;
    for (size_t i = 0; i < max; ++i)
        out.push_back((start + i) % total);
    return out;
}

// The G2 detail array: one entry per online member with ready facts or
  // boss debuffs, in member order. Level 1+ trim drops it with the window.
  inline void AppendDetailJson(std::ostringstream& ss, GroupInfo const& group, uint8_t trim)
  {
    if (trim >= 1 || group.detail.empty())
        return;
    bool first = true;
    for (DetailEntry const& d : group.detail)
    {
        if (d.guid == 0 && !d.hasReady && d.debuffs.empty() && !d.mc)
            continue;
        if (first)
        {
            ss << ",\"detail\":[";
            first = false;
        }
        else
            ss << ",";
        ss << "{\"g\":" << d.guid;
        if (d.hasReady)
        {
            ss << ",\"rdy\":{\"b\":" << d.readyBuffs
               << ",\"c\":" << d.readyCons
               << ",\"cr\":" << static_cast<uint32_t>(d.cdReady)
               << ",\"cn\":" << static_cast<uint32_t>(d.cdKnown);
            if (d.cdLongestS)
                ss << ",\"cl\":" << d.cdLongestS;
            ss << "}";
        }
        if (!d.debuffs.empty())
    {
        size_t listed = 0;
        for (CommanderTelemetry::AuraInfo const& a : d.debuffs)
        {
            if (a.id == 0 || listed >= CommanderTelemetry::kMaxBotDebuffs)
                continue;
            ss << (listed ? "," : ",\"db\":[");
            ++listed;
            CommanderTelemetry::AppendAuraJson(ss, a, true);
        }
        if (listed)
            ss << "]";
    }
    if (d.mc)
        ss << ",\"mc\":1";
    ss << "}";
    }
    if (!first)
        ss << "]";
  }

// One group's JSON object at a trim level (0 none). `memberLimit` is the level-3 cut; from level 2 on the offline names are
// left out. `more` counts every member not listed, the kMaxMembersPerGroup overflow included.
inline std::string BuildGroupJson(GroupInfo const& group, uint8_t trim = 0, size_t memberLimit = kMaxMembersPerGroup)
{
    size_t limit = std::min(group.members.size(), kMaxMembersPerGroup);
    limit = std::min(limit, memberLimit);

    std::ostringstream ss;
    ss << "{\"group\":" << group.groupId
       << ",\"key\":\"" << CommanderTelemetry::JsonEscape(FightKey(group)) << "\""
       << ",\"map\":" << group.map
       << ",\"instance\":" << group.instance
       << ",\"rg\":" << (group.raidGroup ? "true" : "false")
       << ",\"leader\":\"" << CommanderTelemetry::JsonEscape(CommanderTelemetry::Clip(group.leader, kMaxRaidName)) << "\""
       << ",\"leader_guid\":" << group.leaderGuid;
    if (!group.commander.empty())
        ss << ",\"commander\":\"" << CommanderTelemetry::JsonEscape(CommanderTelemetry::Clip(group.commander, kMaxRaidName)) << "\"";
    ss << ",\"members\":[";
    for (size_t i = 0; i < limit; ++i)
    {
        MemberRef const& member = group.members[i];
        if (i)
            ss << ",";
        ss << "{\"g\":" << member.guid << ",\"s\":" << member.sub;
        if (!member.online)
        {
            if (trim < 2 && !member.name.empty())
                ss << ",\"n\":\"" << CommanderTelemetry::JsonEscape(CommanderTelemetry::Clip(member.name, kMaxRaidName)) << "\"";
            ss << ",\"off\":true";
        }
        ss << "}";
    }
    ss << "]";
    if (group.members.size() > limit)
        ss << ",\"more\":" << (group.members.size() - limit);
    if (trim < 1 && !group.combatJson.empty())
        ss << ",\"combat\":" << group.combatJson;
    // G2 detail goes last, after every G0/G1 key, so an older dashboard
    // keeps working while ignoring the tail.
    AppendDetailJson(ss, group, trim);
    if (trim)
        ss << ",\"trimmed\":\"" << TrimSectionName(trim) << "\"";
    ss << "}";
    return ss.str();
  }

  // One serialized payload. `trims` (one level per group) and `memberLimits` (one level-3 cut per group) are optional; a null
// vector writes every group whole.
inline std::string BuildRaidsPayload(int protocolVersion, uint64_t session, uint64_t seq, int64_t ts, int64_t tsMs,
                                     std::vector<GroupInfo const*> const& groups, int part = 0, int parts = 1,
                                     std::vector<uint8_t> const* trims = nullptr,
                                     std::vector<size_t> const* memberLimits = nullptr)
{
    std::ostringstream ss;
    ss << "{\"v\":" << protocolVersion
       << ",\"session\":" << session
       << ",\"seq\":" << seq;
    if (parts > 1)
        ss << ",\"part\":" << part << ",\"parts\":" << parts;
    ss << ",\"ts\":" << ts;
    if (tsMs)
        ss << ",\"ts_ms\":" << tsMs;
    ss << ",\"type\":\"RAIDS\",\"raids\":[";
    for (size_t i = 0; i < groups.size(); ++i)
    {
        if (i)
            ss << ",";
        uint8_t const trim = trims && i < trims->size() ? (*trims)[i] : 0;
        size_t const limit = memberLimits && i < memberLimits->size() ? (*memberLimits)[i] : kMaxMembersPerGroup;
        ss << BuildGroupJson(*groups[i], trim, limit);
    }
    ss << "]}";
    return ss.str();
}

// The groups, in collection order, packed into parts that each serialize at or under partByteCap. `measure` serializes a
// candidate part, because the window and the member rows both count. A group is never split, so one that alone goes past
// the cap sits in a part of its own (BuildRaidsParts trims it inside).
template <typename Measure>
inline std::vector<std::vector<size_t>> PartitionGroups(std::vector<GroupInfo> const& groups, Measure const& measure,
                                                       size_t partByteCap = kRaidPartByteCap)
{
    std::vector<std::vector<size_t>> parts;
    for (size_t i = 0; i < groups.size() && i < kMaxRaidGroupsPerCycle; ++i)
    {
        if (!parts.empty())
        {
            std::vector<size_t> candidate = parts.back();
            candidate.push_back(i);
            if (measure(candidate) <= partByteCap)
            {
                parts.back() = std::move(candidate);
                continue;
            }
        }
        parts.emplace_back(1, i);
    }
    return parts;
}

// Every part of one cycle, in order, with the cycle's seq and timestamp and its own part/parts fields. No group: one empty
// payload (the end marker). A part of one group still over the cap walks the ladder (kMaxRaidTrimLevel); every trim is
// appended to trimLog.
inline std::vector<std::string> BuildRaidsParts(int protocolVersion, uint64_t session, uint64_t seq, int64_t ts, int64_t tsMs,
                                                std::vector<GroupInfo> const& groups,
                                                std::vector<TrimEvent>* trimLog = nullptr,
                                                size_t partByteCap = kRaidPartByteCap)
{
    auto const build = [&](std::vector<size_t> const& slice, int part, int parts, uint8_t trim = 0,
                           size_t memberLimit = kMaxMembersPerGroup)
    {
        std::vector<GroupInfo const*> refs;
        refs.reserve(slice.size());
        for (size_t const index : slice)
            refs.push_back(&groups[index]);
        std::vector<uint8_t> trims(refs.size(), trim);
        std::vector<size_t> limits(refs.size(), memberLimit);
        return BuildRaidsPayload(protocolVersion, session, seq, ts, tsMs, refs, part, parts,
            trim ? &trims : nullptr, &limits);
    };
    // The widest part fields a cycle can write, so the measure never undercounts.
    int const widestPart = static_cast<int>(kMaxRaidGroupsPerCycle) - 1;
    int const widestParts = static_cast<int>(kMaxRaidGroupsPerCycle);
    auto const measure = [&](std::vector<size_t> const& slice)
    {
        return build(slice, widestPart, widestParts).size();
    };
    std::vector<std::vector<size_t>> const parts = PartitionGroups(groups, measure, partByteCap);
    if (parts.empty())
        return { BuildRaidsPayload(protocolVersion, session, seq, ts, tsMs, {} ) };

    std::vector<std::string> out;
    out.reserve(parts.size());
    for (size_t i = 0; i < parts.size(); ++i)
    {
        int const part = static_cast<int>(i);
        int const count = static_cast<int>(parts.size());
        std::string payload = build(parts[i], part, count);
        if (parts[i].size() == 1 && payload.size() > partByteCap)
        {
            GroupInfo const& group = groups[parts[i][0]];
            uint8_t level = 0;
            size_t limit = std::min(group.members.size(), kMaxMembersPerGroup);
            for (level = 1; level <= 2; ++level)
            {
                payload = build(parts[i], part, count, level);
                if (payload.size() <= partByteCap)
                    break;
            }
            if (level > 2)
            {
                // Cut members from the end until the part fits; `more` carries what was left out.
                level = 3;
                while (limit > 0)
                {
                    --limit;
                    payload = build(parts[i], part, count, level, limit);
                    if (payload.size() <= partByteCap)
                        break;
                }
            }
            if (trimLog)
                trimLog->push_back(TrimEvent{group.groupId, FightKey(group), level});
        }
        out.push_back(std::move(payload));
    }
    return out;
}

} // namespace RaidTelemetry
} // namespace TortoiseBots
