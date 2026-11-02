#include "BotCommands.h"

// The Raids tab (spec dash-raids-tab): the raid groups that stand without a commander binding. CollectObservedRaids runs
// inside the 2 s snapshot cycle and names the groups the RAIDS datagram lists; UpdateObservedRaids feeds their combat books
// at 1 Hz, so an unbound raid gets the same live window and finished fights a bound group gets. Both read the world on the
// world thread only.

#include "../runtime/BoundFightPolicy.h"
#include "../runtime/CombatMonitorService.h"
#include "../runtime/ObservabilityEmitter.h"
#include "../runtime/ObservedRaidPolicy.h"
#include "../runtime/PlayerbotAIStorage.h"
#include "../runtime/RaidTelemetry.h"
#include "../host/ModuleLog.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "Group.h"
#include "Log.h"
#include "Maps/Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "World.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace TortoiseBots {
namespace BotCommands {

namespace {

struct SelectedCopy
{
    uint32_t map = 0;
    uint32_t instance = 0;
};

// The groups the last RAIDS collection found (group id -> its instance copy). The sampler never walks the world itself.
std::unordered_map<uint32_t, SelectedCopy>& RaidSet()
{
    static std::unordered_map<uint32_t, SelectedCopy> set;
    return set;
}

// The clock of each observed group that stopped qualifying or vanished, kept until the grace closes its open fight.
std::unordered_map<uint32_t, BoundFight::LeadAway>& RaidAway()
{
    static std::unordered_map<uint32_t, BoundFight::LeadAway> away;
    return away;
}

// Whether a unit is on a raid map copy: in the world, outside a battleground, on a raid map (the core's MapEntry instance
// type), inside an instance copy.
bool UnitQualifies(Player* unit)
{
    if (!unit || !unit->IsInWorld() || unit->InBattleGround())
        return false;
    Map* map = unit->GetMap();
    return RaidTelemetry::Qualifies(unit->IsInWorld(), unit->InBattleGround(), map && map->IsRaid(),
        map ? map->GetInstanceId() : 0);
}

// The bound sampler's rule: the member or its living pet is in combat.
bool MemberFighting(Player* member)
{
    Pet* const pet = member->GetPet();
    return member->IsInCombat() || (pet && pet->IsAlive() && pet->IsInWorld() && pet->IsInCombat());
}

// The sampler's flag as the last tick read it: the tick that sees it off after an on tick closes what it fed.
ObservedRaid::FlagTurn& SamplerFlag()
{
    static ObservedRaid::FlagTurn flag;
    return flag;
}

// The books the sampler holds: its collected set and its gone-grace clocks, each once, without a bound group's.
std::vector<uint32_t> HeldGroupIds()
{
    std::vector<uint32_t> collected;
    collected.reserve(RaidSet().size());
    for (auto const& entry : RaidSet())
        collected.push_back(entry.first);
    std::vector<uint32_t> away;
    away.reserve(RaidAway().size());
    for (auto const& entry : RaidAway())
        away.push_back(entry.first);
    return ObservedRaid::HeldGroups(collected, away, [](uint32_t groupId) { return GroupIsBound(groupId); });
}

} // namespace

void CollectObservedRaids(std::vector<Player*> const& bots, std::vector<Player*> const& players,
                          std::vector<RaidTelemetry::GroupInfo>& out)
{
    using namespace RaidTelemetry;
    out.clear();
    RaidSet().clear();

    // The cycle's own lists are already in hand: a unit on a raid map copy names its group. No extra session walk.
    std::unordered_set<uint32_t> candidateGroups;
    std::unordered_set<uint32_t> walked;
    auto note = [&](Player* unit)
    {
        if (!UnitQualifies(unit) || !walked.insert(unit->GetGUIDLow()).second)
            return;
        if (Group* group = unit->GetGroup())
            candidateGroups.insert(group->GetId());
    };
    for (Player* bot : bots)
        note(bot);
    for (Player* player : players)
        note(player);

    std::vector<GroupInfo> found;
    for (uint32_t const groupId : candidateGroups)
    {
        Group* group = sObjectMgr.GetGroupById(groupId);
        if (!group)
            continue;
        ObjectGuid const leaderGuid = group->GetLeaderGuid();
        Player* const leader = ObjectAccessor::FindPlayer(leaderGuid);
        // The copy is a bot's: the leader's when the leader is a qualifying bot, else the first qualifying bot's. A human
        // standing in a raid copy never names the group, so a group whose only bot is elsewhere is not emitted under it.
        std::vector<ObservedRaid::CopyMember> facts;
        facts.reserve(group->GetMemberSlots().size());
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            Player* member = ObjectAccessor::FindPlayer(slot.guid);
            if (!member || !member->IsInWorld())
                continue;
            bool const qualifies = UnitQualifies(member);
            ObservedRaid::CopyMember fact;
            fact.leader = slot.guid == leaderGuid;
            fact.bot = PlayerbotAIStorage::Instance().GetAI(member) != nullptr;
            fact.qualifies = qualifies;
            fact.map = member->GetMapId();
            fact.instance = qualifies ? member->GetMap()->GetInstanceId() : 0;
            facts.push_back(fact);
        }
        ObservedRaid::CopyChoice const copy = ObservedRaid::SelectCopy(facts);
        if (!copy.found)
            continue;
        uint32_t const mapId = copy.map;
        uint32_t const instance = copy.instance;

        GroupInfo info;
        info.groupId = groupId;
        info.map = mapId;
        info.instance = instance;
        info.raidGroup = group->isRaidGroup();
        info.leaderGuid = leaderGuid.GetCounter();
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
            if (slot.guid == leaderGuid)
                info.leader = leader ? leader->GetName() : slot.name;
        info.commander = BoundCommanderNameOfGroup(groupId);
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            MemberRef member;
            member.guid = slot.guid.GetCounter();
            member.sub = slot.group;
            Player* online = ObjectAccessor::FindPlayer(slot.guid);
            member.online = online && online->IsInWorld();
            if (!member.online)
                member.name = slot.name;
            info.members.push_back(std::move(member));
        }
        if (CombatMonitorService::Instance().InFight(groupId))
            info.combatJson = CombatMonitorService::Instance().WindowJson(groupId);
        found.push_back(std::move(info));
    }

    std::sort(found.begin(), found.end(), [](GroupInfo const& a, GroupInfo const& b)
    {
        if (a.map != b.map)
            return a.map < b.map;
        if (a.instance != b.instance)
            return a.instance < b.instance;
        return a.groupId < b.groupId;
    });
    for (GroupInfo const& info : found)
        RaidSet()[info.groupId] = SelectedCopy{info.map, info.instance};
    out.swap(found);
}

bool IsObservedRaidGroup(uint32_t groupId)
{
    return RaidSet().count(groupId) != 0;
}

void UpdateObservedRaids(uint32_t diff)
{
    bool const enabled = ObservabilityEmitter::Instance().RaidsEnabled();
    if (SamplerFlag().TurnedOff(enabled))
    {
        // `.reload config` turned the flag off under the sampler: the fights it fed end at the current time and their books
        // go, so no window stays open on the tab and the next on turn starts from an empty set.
        uint32_t const now = WorldTimer::getMSTime();
        ObservedRaid::ReleaseBooks(HeldGroupIds(), now,
            [](uint32_t groupId, uint32_t atMs) { CombatMonitorService::Instance().CloseUnfinished(groupId, atMs); },
            [](uint32_t groupId) { CombatMonitorService::Instance().Forget(groupId); });
        RaidSet().clear();
        RaidAway().clear();
    }
    if (!enabled)
        return;
    CombatMonitorService::Instance().SyncConfig(sPlayerbotAIConfig.combatMonitor &&
        ObservabilityEmitter::Instance().IsEnabled());
    if (!CombatMonitorService::Instance().Enabled())
        return;

    static uint32_t s_sampleMs = 0;
    static uint64_t s_samples = 0, s_sumUs = 0, s_maxUs = 0, s_maxGroups = 0;
    static uint32_t s_loggedAt = 0;
    s_sampleMs += diff;
    if (s_sampleMs < BoundFight::kSampleMs)
        return;
    s_sampleMs = 0;
    std::chrono::steady_clock::time_point const started = std::chrono::steady_clock::now();
    uint32_t const now = WorldTimer::getMSTime();

    // The set the collection found, plus the groups still on a gone-grace clock (the collection dropped them already).
    std::vector<uint32_t> groups;
    groups.reserve(RaidSet().size() + RaidAway().size());
    for (auto const& entry : RaidSet())
        groups.push_back(entry.first);
    for (auto const& entry : RaidAway())
        if (!RaidSet().count(entry.first))
            groups.push_back(entry.first);

    for (uint32_t const groupId : groups)
    {
        // A bound group's book belongs to the bound path: no second Refresh, no context fight. A group the last collection
        // no longer found is on its gone-grace clock, not sampled.
        bool const bound = GroupIsBound(groupId);
        bool const observed = RaidSet().count(groupId) != 0;
        Group* group = bound || !observed ? nullptr : sObjectMgr.GetGroupById(groupId);
        bool const usable = bound || group;
        if (RaidAway()[groupId].Overdue(usable, now))
        {
            CombatMonitorService::Instance().CloseUnfinished(groupId, now);
            CombatMonitorService::Instance().Forget(groupId);
            RaidSet().erase(groupId);
            RaidAway().erase(groupId);
            continue;
        }
        if (bound || !group)
            continue;

        // Only the members standing on the copy the collection selected for the group: a member in another instance, on
        // another map or outdoors is not in the raid, so its combat never opens or extends the fight and its being alive
        // never keeps the fight from closing.
        SelectedCopy const copy = RaidSet()[groupId];
        std::vector<Player*> members;
        bool anyBotOnCopy = false;
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            Player* member = ObjectAccessor::FindPlayer(slot.guid);
            if (!member || !member->IsInWorld())
                continue;
            Map* const memberMap = member->GetMap();
            if (!memberMap || !ObservedRaid::OnSelectedCopy(copy.map, copy.instance, member->GetMapId(),
                    memberMap->GetInstanceId()))
                continue;
            members.push_back(member);
            anyBotOnCopy = anyBotOnCopy || PlayerbotAIStorage::Instance().GetAI(member) != nullptr;
        }
        // A copy holding only humans is no run: the group's bot left it since the collection (the 2 s cycle has not run
        // yet), so its book is left alone until the next collection files the group under the bot's own copy.
        if (!ObservedRaid::SampleHoldsBot(copy.instance, anyBotOnCopy))
            continue;
        bool anyInCombat = false;
        bool allDead = !members.empty();
        for (Player* member : members)
        {
            anyInCombat = anyInCombat || MemberFighting(member);
            allDead = allDead && !member->IsAlive();
        }
        CombatMonitor::FightContext ctx;
        ctx.kind = "raid";
        uint32_t const mapId = copy.map;
        // The book is filed under "raid#<group id>", the key the RAIDS row carries until a binding stands on the group.
        CombatMonitorService::Instance().NoteBinding(groupId, "raid#" + std::to_string(groupId), 0, mapId);
        CombatMonitorService::Instance().Refresh(groupId, CombatMonitorMembersOf(members), anyInCombat, allDead, ctx,
            {}, now);
        if (CombatMonitorService::Instance().InFight(groupId) && !members.empty())
            NoteFightBoss(groupId, members.front());
    }

    uint64_t const us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started).count());
    ++s_samples;
    s_sumUs += us;
    s_maxUs = std::max(s_maxUs, us);
    s_maxGroups = std::max<uint64_t>(s_maxGroups, groups.size());
    if (now - s_loggedAt >= 60 * BoundFight::kSampleMs)
    {
        TB_LOG_DEBUG("TortoiseBots: observed raids 60s: %u samples, %u groups max, avg %llu us max %llu us",
            static_cast<uint32_t>(s_samples), static_cast<uint32_t>(s_maxGroups),
            static_cast<unsigned long long>(s_sumUs / std::max<uint64_t>(s_samples, 1)),
            static_cast<unsigned long long>(s_maxUs));
        s_loggedAt = now;
        s_samples = 0;
        s_sumUs = 0;
        s_maxUs = 0;
        s_maxGroups = 0;
    }
}

void ClearObservedRaids()
{
    RaidSet().clear();
    RaidAway().clear();
}

} // namespace BotCommands
} // namespace TortoiseBots
