
#include "playerbot/GroupMembers.h"
#include "playerbot/playerbot.h"
#include "playerbot/BossTacticsMgr.h"
#include "playerbot/strategy/values/PartyMemberToHeal.h"
#include "ThreatValues.h"

#include "Objects/Creature.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/ThreatCapPolicy.h"
#include "Threat/ThreatManager.h"

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <mutex>
#include <unordered_map>

using namespace ai;

float MyThreatValue::Calculate()
{
    Unit* target = AI_VALUE(Unit*, qualifier);

    ObjectGuid targetGuid = target ? target->getObjectGuid() : ObjectGuid();
    if (targetGuid != lastTarget) //Reset history if we switched target.
        LogCalculatedValue::Reset();

    lastTarget = targetGuid;

    return ThreatValue::GetThreat(bot, target);
}

float TankThreatValue::Calculate()
{
    Unit* target = AI_VALUE(Unit*, qualifier);

    return ThreatValue::GetTankThreat(ai, target);
}

uint8 ThreatValue::Calculate()
{
    if (qualifier == "aoe")
    {
        uint8 maxThreat = 0;
        std::list<ObjectGuid> attackers = context->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
        for (std::list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); i++)
        {
            Unit* unit = ai->GetUnit(*i);
            if (!unit || !sServerFacade.IsAlive(unit))
                continue;

            uint8 threat = Calculate(unit);
            if (!maxThreat || threat > maxThreat)
                maxThreat = threat;
        }

        return maxThreat;
    }

    Unit* target = AI_VALUE(Unit*, qualifier);

    if (target && sServerFacade.IsFriendlyTo(target, bot))
        target = target->GetVictim();

    return Calculate(target);
}

namespace
{
// One threat-cap row per damage dealer and 10 s: which tank the damage dealers of a `threat_successor` fight are
// measured against and that tank's threat on the boss. Nothing else logged the reference, so a cap that sat on the
// main tank instead of the off-tank was invisible until the boss turned to a damage dealer after a threat reset. The
// throttle is checked before any string is built; one entry per bot guid, pruned with the bot count.
constexpr std::uint32_t kThreatCapLogGapMs = 10000;

std::mutex g_threatCapLogMutex;
std::unordered_map<std::uint64_t, std::uint32_t> g_threatCapLog;

void LogThreatCap(PlayerbotAI* ai, Player* bot, const ThreatCapPick& pick, Unit* offTankTarget)
{
    if (!sPlayerbotAIConfig.hasLog("bot_events.csv") || PlayerbotAI::IsTank(bot) || PlayerbotAI::IsHeal(bot))
        return;
    const std::uint32_t nowMs = WorldTimer::getMSTime();
    {
        std::lock_guard<std::mutex> lock(g_threatCapLogMutex);
        std::uint32_t& last = g_threatCapLog[bot->GetObjectGuid().GetRawValue()];
        if (last != 0 && nowMs - last < kThreatCapLogGapMs)
            return;
        last = nowMs;
        if (g_threatCapLog.size() > 4096)
        {
            for (auto it = g_threatCapLog.begin(); it != g_threatCapLog.end();)
                it = nowMs - it->second > kThreatCapLogGapMs * 60 ? g_threatCapLog.erase(it) : std::next(it);
        }
    }
    // The first off-tank's current target: the cap reference stalls when that tank is stuck on a Son instead of the boss.
    // holder= is the tank the boss is hitting right now (none: a damage dealer, or nobody).
    char info1[160];
    snprintf(info1, sizeof(info1), "threat cap: ref=%s %.0f offtank_target=%s holder=%s", pick.offTank ? "offtank" : "main",
             pick.threat, offTankTarget ? offTankTarget->GetName() : "none", ThreatCapHolderWord(pick.holder));
    sPlayerbotAIConfig.logEvent(ai, "BossTacticsThreatCap", info1, "");
}

// BossTacticsHex: one row per Jin'do Hex (17172) on a member of the fight, so a walk can see who the boss turned to after
// it. Hex lands on the boss's current victim and wipes that member's threat; the row is the state right after:
//   info1 = "holder=<main|off|dps> ot_threat=<n> mt_threat=<n> top_dps=<name>:<threat>"
// `holder` is the member who WORE the Hex (the walk-report parser reads the field name; it is not the boss's victim).
// ot_threat is the wearer's threat when an off-tank wore it, else the first off-tank's; mt_threat the main tank's; -1 when
// the raid's tank order has no such tank (a dead tank reads what the boss still holds for it). top_dps is the highest
// non-tank, non-healer member.
// The wearer is found by its aura among the tanks and then the group's members, once per second per boss (HexScanDue):
// the boss has turned to someone else by the time this runs, so the current victim is no clue. The aura-watch hook of
// the combat timeline (host/CombatMonitorAdapter.cpp OnAuraApply) runs on a map thread behind AiPlayerbot.CombatTimeline
// and has no bot to log under, so the snapshot is taken here, on the world thread, at the next read of the boss's tank
// threat (sub-second while any bot with the threat value is in combat). One row per member and Hex (HexSnapshotDue); the
// throttle maps are pruned past 256 / 64 entries. Nothing runs without the log file, and with no wearer the scan is one
// aura lookup per member once a second.
constexpr std::size_t kHexLogMaxEntries = 256;
constexpr std::size_t kHexScanMaxBosses = 64;
std::mutex g_hexLogMutex;
std::unordered_map<std::uint64_t, std::uint32_t> g_hexLog;
std::unordered_map<std::uint64_t, std::uint32_t> g_hexScan;

void LogHexSnapshot(PlayerbotAI* ai, Unit* boss, const RaidTankRoles& roles, Group* group)
{
    if (!sPlayerbotAIConfig.hasLog("bot_events.csv"))
        return;
    const std::uint32_t nowMs = WorldTimer::getMSTime();
    {
        std::lock_guard<std::mutex> lock(g_hexLogMutex);
        std::uint32_t& lastScan = g_hexScan[boss->GetObjectGuid().GetRawValue()];
        if (!HexScanDue(lastScan, nowMs))
            return;
        lastScan = nowMs;
        if (g_hexScan.size() > kHexScanMaxBosses)
            g_hexScan.clear();
    }
    const auto tankIndexOf = [&roles](Player* player) -> int
    {
        for (std::size_t index = 0; index < roles.tanks.size(); ++index)
            if (roles.tanks[index] == player)
                return static_cast<int>(index);
        return -1;
    };
    std::vector<HexMember> wearers;
    std::vector<Player*> wearerPlayers;
    const auto consider = [&](Player* player)
    {
        if (!player || !player->IsInWorld() || !ai->HasAura(kJindoHexSpellId, player))
            return;
        for (Player* known : wearerPlayers)
            if (known == player)
                return;
        HexMember member;
        member.key = player->GetObjectGuid().GetRawValue();
        member.tankIndex = tankIndexOf(player);
        wearers.push_back(member);
        wearerPlayers.push_back(player);
    };
    for (Player* tank : roles.tanks)
        consider(tank);
    if (group)
        for (Player* member : LiveGroupMembers(group))
            consider(member);
    if (wearers.empty())
        return;
    int pickedIndex;
    {
        std::lock_guard<std::mutex> lock(g_hexLogMutex);
        for (HexMember& member : wearers)
        {
            auto const known = g_hexLog.find(member.key);
            member.lastLoggedMs = known == g_hexLog.end() ? 0 : known->second;
        }
        pickedIndex = HexWearerToLog(wearers, nowMs);
        if (pickedIndex < 0)
            return;
        g_hexLog[wearers[pickedIndex].key] = nowMs;
        if (g_hexLog.size() > kHexLogMaxEntries)
        {
            for (auto it = g_hexLog.begin(); it != g_hexLog.end();)
                it = nowMs - it->second > kHexSnapshotGapMs * 8 ? g_hexLog.erase(it) : std::next(it);
        }
    }
    const int wearerTankIndex = wearers[pickedIndex].tankIndex;
    const std::size_t offIndex = static_cast<std::size_t>(HexOffTankIndex(wearerTankIndex));
    auto const threatOf = [boss](Player* player) -> float
    {
        return player ? sServerFacade.GetThreatManager(boss).getThreat(player) : -1.0f;
    };
    Player* const mainTank = roles.tanks.size() >= 1 ? roles.tanks[0] : nullptr;
    Player* const offTank = roles.tanks.size() > offIndex ? roles.tanks[offIndex] : nullptr;
    Player* topDps = nullptr;
    float topDpsThreat = 0.0f;
    if (group)
    {
        for (Player* member : LiveGroupMembers(group))
        {
            if (!member || !sServerFacade.IsAlive(member) || ai->IsTank(member) || PlayerbotAI::IsHeal(member))
                continue;
            const float threat = sServerFacade.GetThreatManager(boss).getThreat(member);
            if (!topDps || threat > topDpsThreat)
            {
                topDps = member;
                topDpsThreat = threat;
            }
        }
    }
    char info1[160];
    snprintf(info1, sizeof(info1), "holder=%s ot_threat=%.0f mt_threat=%.0f top_dps=%s:%.0f",
             HexHolderWord(HexHolderOf(true, wearerTankIndex)), threatOf(offTank), threatOf(mainTank),
             topDps ? topDps->GetName() : "none", topDpsThreat);
    sPlayerbotAIConfig.logEvent(ai, "BossTacticsHex", info1, "");
}
}

float ThreatValue::GetThreat(Player* player, Unit* target)
{
    if (!target || target->GetMapId() != player->GetMapId())
        return 0;

    if (dynamic_cast<Player*>(target) && (dynamic_cast<Player*>(target))->IsBeingTeleported())
        return 0;

    if (sServerFacade.IsFriendlyTo(target, player))
        target = target->GetVictim();

    if (!target || target->getObjectGuid().IsPlayer())
        return 0;

    float botThreat = sServerFacade.GetThreatManager(target).getThreat(player);

    return botThreat;
}

float ThreatValue::GetTankThreat(PlayerbotAI* ai, Unit* target)
{
    if (!target)
        return 0;

    if (sServerFacade.IsFriendlyTo(target, ai->GetBot()))
        target = target->GetVictim();

    if (!target || target->getObjectGuid().IsPlayer())
        return 0;

    Group* group = ai->GetBot()->GetGroup();
    if (!group)
        return 0;

    // The off-tank cap is the boss data's call (ai_boss_tactic kind `threat_successor`), never the creature's taunt
    // immunity: only a target with that row reads the raid's tank order. Every other target keeps the plain IsTank
    // max exactly as before, whatever its taunt immunity.
    if (!ai::ThreatSuccessorTarget(target))
    {
        float maxThreat = -1.0f;
        for (Player* player : LiveGroupMembers(group))
        {
            if (!player || !sServerFacade.IsAlive(player) || !ai->IsSafe(player) || player == ai->GetBot())
                continue;
            if (ai->IsTank(player))
            {
                float threat = sServerFacade.GetThreatManager(target).getThreat(player);
                if (maxThreat < threat)
                    maxThreat = threat;
            }
        }
        return maxThreat;
    }

    // The raid's own tank order: [0] the main tank, [1..] the assigned off-tanks (GetRaidTankRoles). A charmed tank is
    // hostile and its threat is gone (the charm handler deletes it), so it cannot be the boss's successor.
    const RaidTankRoles roles = GetRaidTankRoles(ai);
    std::vector<ThreatCapTank> tanks;
    tanks.reserve(roles.tanks.size());
    for (std::size_t index = 0; index < roles.tanks.size(); ++index)
    {
        Player* player = roles.tanks[index];
        if (!player || !sServerFacade.IsAlive(player) || !ai->IsSafe(player) || player == ai->GetBot())
            continue;
        ObjectGuid const charmer = player->GetCharmerGuid();
        if (!charmer.IsEmpty() && !charmer.IsPlayer())
            continue;
        // onBoss: this tank hits the boss. holdsBoss: the boss hits this tank (AiPlayerbot.DungeonRunThreatSuccessorNotHolder).
        tanks.push_back(ThreatCapTank{sServerFacade.GetThreatManager(target).getThreat(player), index >= 1,
            player->GetVictim() == target, target->GetVictim() == player});
    }

    const ThreatCapPick pick = ThreatCapPickReference(tanks, true, sPlayerbotAIConfig.dungeonRunThreatSuccessorNotHolder);
    LogHexSnapshot(ai, target, roles, group);
    // roles.tanks[1] can be dead or out of the world: read its victim only when it stands in the world.
    Player* const offTankPlayer = roles.tanks.size() >= 2 ? roles.tanks[1] : nullptr;
    Unit* const offTankVictim = offTankPlayer && offTankPlayer->IsInWorld() ? offTankPlayer->GetVictim() : nullptr;
    Unit* const offTankTarget = offTankVictim && offTankVictim->IsInWorld() ? offTankVictim : nullptr;
    LogThreatCap(ai, ai->GetBot(), pick, offTankTarget);
    return pick.threat;
}

bool ai::TargetTauntImmune(Unit* target)
{
    Creature* const creature = target ? dynamic_cast<Creature*>(target) : nullptr;
    return creature && creature->HasImmunityFlag(CREATURE_IMMUNITY_TAUNT);
}

bool ai::ThreatSuccessorTarget(Unit* target)
{
    if (!target || target->getObjectGuid().IsPlayer())
        return false;
    return GetBossTactics().HasThreatSuccessor(target->GetMapId(), target->GetEntry());
}

uint8 ThreatValue::Calculate(Unit* target)
{
    if (!target)
        return 0;

    if (sServerFacade.IsFriendlyTo(target, bot))
        target = target->GetVictim();

    if (!target)
        return 0;

    if (target->getObjectGuid().IsPlayer())
        return 0;

    Group* group = bot->GetGroup();
    if (!group)
        return 0;

    float botThreat, maxThreat;
    if (qualifier == "aoe")
    {
        botThreat = GetThreat(bot, target);
        maxThreat = GetTankThreat(ai, target);
    }
    else
    {
        botThreat = AI_VALUE2(float, "my threat", qualifier);
        maxThreat = AI_VALUE2(float, "tank threat", qualifier);
    }

    if (maxThreat < 0)
        return 0;

    // calculate normal threat for fleeing targets
    // (the flee-for-assistance walk and its pause count too: Creature::DoFleeToGetAssistance)
    MovementGeneratorType const motion = target->GetMotionMaster()->GetCurrentMovementGeneratorType();
    bool fleeing = motion == FLEEING_MOTION_TYPE || motion == TIMED_FLEEING_MOTION_TYPE ||
        motion == ASSISTANCE_MOTION_TYPE || motion == ASSISTANCE_DISTRACT_MOTION_TYPE;

    // A reference of 0 on an engaged target reads 100 ("the tank has no threat": hold), see ThreatPctOf.
    return ThreatPctOf(botThreat, maxThreat, target->IsInCombat(), fleeing);
}
