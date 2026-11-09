#include "playerbot/playerbot.h"
#include "VictimEscapeActions.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/DungeonPlanMgr.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/VictimEscapePolicy.h"
#include "playerbot/strategy/actions/BossTacticsActions.h"
#include "playerbot/strategy/values/ThreatValues.h"
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <unordered_map>

using namespace ai;

namespace
{
// Every rank of each escape: readiness is read off the member itself, so a real player's victim reads the same way as
// a bot's. The ranks share their cooldown; the cast goes through the bot's own "spell id" lookup (its highest rank).
const uint32 kFeignDeathIds[] = { 5384 };
const uint32 kVanishIds[] = { 1857, 1856 };
const uint32 kIceBlockIds[] = { 11958 };
const uint32 kDivineShieldIds[] = { 1020, 642 };
const uint32 kForbearanceId = 25771;

std::mutex g_victimEscapeMutex;
// victim guid -> the last escape (own or a protection) cast on it
std::unordered_map<std::uint64_t, std::uint32_t> g_victimEscapeLast;
// victim guid -> the last refused row
std::unordered_map<std::uint64_t, std::uint32_t> g_victimEscapeRefusedLog;

template <std::size_t N>
bool AnyRankReady(Player* member, const uint32 (&ids)[N])
{
    for (uint32 id : ids)
        if (member->HasSpell(id))
            return sServerFacade.IsSpellReady(member, id);
    return false;
}

bool IsBossCreature(Unit* unit)
{
    if (!unit || !unit->IsCreature())
        return false;
    Creature* const creature = static_cast<Creature*>(unit);
    return creature->IsWorldBoss() || GetDungeonPlans().IsBossEntry(creature->GetMapId(), creature->GetEntry());
}

VictimEscapeReady ReadyOf(Player* member)
{
    VictimEscapeReady ready;
    // Ice Block and Divine Shield go out while stunned; Feign Death and Vanish do not.
    const bool inControl = !member->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL);
    switch (member->GetClass())
    {
        case CLASS_HUNTER: ready.feignDeath = inControl && AnyRankReady(member, kFeignDeathIds); break;
        case CLASS_ROGUE: ready.vanish = inControl && AnyRankReady(member, kVanishIds); break;
        case CLASS_MAGE: ready.iceBlock = AnyRankReady(member, kIceBlockIds); break;
        case CLASS_PALADIN:
            ready.divineShield = !member->HasAura(kForbearanceId) && AnyRankReady(member, kDivineShieldIds);
            break;
        default: break;
    }
    return ready;
}

// Another living non-tank stands on the boss's threat list (a charmed member is hostile and does not count).
bool OtherNonTankTarget(Unit* boss, Player* victim)
{
    for (HostileReference const* ref : boss->GetThreatManager().getThreatList())
    {
        Unit* const unit = ref ? ref->getTarget() : nullptr;
        Player* const player = unit ? unit->ToPlayer() : nullptr;
        if (!player || player == victim || !player->IsInWorld() || !sServerFacade.IsAlive(player))
            continue;
        if (!player->GetCharmerGuid().IsEmpty() || PlayerbotAI::IsTank(player))
            continue;
        return true;
    }
    return false;
}

// The walk-out puller and a member holding a pulled Son for its meet keep their duty: an escape would drop it.
bool HoldsBossDuty(Player* victim)
{
    if (IsLiveWalkoutPuller(victim))
        return true;
    return SeekMeetHealPuller(victim->GetInstanceId()) == victim->GetObjectGuid().GetRawValue();
}

VictimEscapeGuards GuardsFor(Unit* boss, Player* victim, std::uint32_t nowMs)
{
    VictimEscapeGuards guards;
    {
        std::lock_guard<std::mutex> lock(g_victimEscapeMutex);
        auto it = g_victimEscapeLast.find(victim->GetObjectGuid().GetRawValue());
        guards.escapedRecently = it != g_victimEscapeLast.end() && VictimEscapeRecent(true, nowMs - it->second);
    }
    guards.otherNonTankTarget = OtherNonTankTarget(boss, victim);
    guards.holdsDuty = HoldsBossDuty(victim);
    return guards;
}

VictimEscape DecideFor(Unit* boss, Player* victim, std::uint32_t nowMs, VictimEscapeGuards& guards)
{
    guards = GuardsFor(boss, victim, nowMs);
    return VictimEscapeFor(sPlayerbotAIConfig.dungeonRunImmuneBossVictimEscape, PlayerbotAI::IsTank(victim),
                           boss->GetVictim() == victim, TargetTauntImmune(boss), victim->GetClass(), ReadyOf(victim),
                           guards);
}

void NoteEscape(Player* victim, std::uint32_t nowMs)
{
    std::lock_guard<std::mutex> lock(g_victimEscapeMutex);
    if (g_victimEscapeLast.size() > 512)
        g_victimEscapeLast.clear();
    g_victimEscapeLast[victim->GetObjectGuid().GetRawValue()] = nowMs;
}

const char* ClassWord(uint8 cls)
{
    switch (cls)
    {
        case CLASS_WARRIOR: return "warrior";
        case CLASS_PALADIN: return "paladin";
        case CLASS_HUNTER: return "hunter";
        case CLASS_ROGUE: return "rogue";
        case CLASS_PRIEST: return "priest";
        case CLASS_SHAMAN: return "shaman";
        case CLASS_MAGE: return "mage";
        case CLASS_WARLOCK: return "warlock";
        case CLASS_DRUID: return "druid";
        default: return "other";
    }
}

// One `ok` row per cast (the escape itself is throttled to one per victim per 6 s); a `refused` row at most once per
// victim per 6 s.
void LogVictimEscape(PlayerbotAI* ai, Unit* boss, Player* victim, uint32 spellId, bool ok, const char* reason,
                     std::uint32_t nowMs)
{
    if (!sPlayerbotAIConfig.hasLog("bot_events.csv"))
        return;
    if (!ok)
    {
        std::lock_guard<std::mutex> lock(g_victimEscapeMutex);
        if (g_victimEscapeRefusedLog.size() > 512)
            g_victimEscapeRefusedLog.clear();
        std::uint32_t& last = g_victimEscapeRefusedLog[victim->GetObjectGuid().GetRawValue()];
        if (last != 0 && nowMs - last < kVictimEscapeGapMs)
            return;
        last = nowMs;
    }
    char info1[160];
    snprintf(info1, sizeof(info1), "boss=%u,victim=%s,class=%s,spell=%u,result=%s",
             boss->GetEntry(), victim->GetName(), ClassWord(victim->GetClass()), spellId, ok ? "ok" : "refused");
    char info2[48];
    snprintf(info2, sizeof(info2), "reason=%s", reason);
    sPlayerbotAIConfig.logEvent(ai, "BossTacticsVictimEscape", info1, info2);
}

// A refusal worth a row: the victim had an escape (or asked for one) and a guard other than the throttle held it.
const char* GuardWord(const VictimEscapeGuards& guards)
{
    if (guards.holdsDuty)
        return "duty";
    if (!guards.otherNonTankTarget)
        return "only_target";
    return nullptr;
}

void StopOwnCast(Player* bot)
{
    // A cast or a channel in progress would refuse the escape (PlayerbotAI::CastSpell's in-progress gate).
    if (bot->IsNonMeleeSpellCasted(false))
        bot->InterruptNonMeleeSpells(false);
}

struct EscapePlan
{
    Unit* boss = nullptr;
    Player* victim = nullptr;
    VictimEscape escape = VictimEscape::None;
};

bool IsOwnEscape(VictimEscape e)
{
    return e != VictimEscape::None && e != VictimEscape::AskProtection;
}

// The victim's own escape, if one is due now. A duty or the only-target guard that holds back an escape the victim
// had ready writes the refused row (the throttle is the escape's own pace, not a refusal).
bool PlanOwnEscape(PlayerbotAI* ai, Player* bot, EscapePlan& plan)
{
    Unit* const boss = ImmuneBossOnVictim(bot);
    if (!boss)
        return false;
    const std::uint32_t nowMs = WorldTimer::getMSTime();
    VictimEscapeGuards guards;
    const VictimEscape escape = DecideFor(boss, bot, nowMs, guards);
    if (escape == VictimEscape::None)
    {
        const char* const why = guards.escapedRecently ? nullptr : GuardWord(guards);
        if (why)
        {
            const VictimEscape wanted = VictimEscapeFor(true, PlayerbotAI::IsTank(bot), true, true, bot->GetClass(),
                                                        ReadyOf(bot), VictimEscapeGuards());
            if (IsOwnEscape(wanted))
                LogVictimEscape(ai, boss, bot,
                                ai->GetAiObjectContext()->GetValue<uint32>("spell id", VictimEscapeWord(wanted))->Get(),
                                false, why, nowMs);
        }
        return false;
    }
    if (!IsOwnEscape(escape))
        return false;
    plan.boss = boss;
    plan.victim = bot;
    plan.escape = escape;
    return true;
}

bool PaladinProtectionReady(PlayerbotAI* ai, Player* bot, uint32& spellId)
{
    spellId = ai->GetAiObjectContext()->GetValue<uint32>("spell id", VictimEscapeWord(VictimEscape::AskProtection))->Get();
    return spellId && bot->HasSpell(spellId) && sServerFacade.IsSpellReady(bot, spellId) &&
        !bot->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL);
}

// The first group member within 30 yd this paladin protects now.
bool PlanProtection(PlayerbotAI* ai, Player* bot, EscapePlan& plan)
{
    if (bot->GetClass() != CLASS_PALADIN || !bot->GetGroup() || !sServerFacade.IsAlive(bot))
        return false;
    uint32 spellId = 0;
    const bool bopReady = PaladinProtectionReady(ai, bot, spellId);
    if (!bopReady)
        return false;
    const std::uint32_t nowMs = WorldTimer::getMSTime();
    for (Player* member : LiveGroupMembers(bot->GetGroup()))
    {
        if (!member || member == bot || !member->IsInWorld() || member->GetMapId() != bot->GetMapId() ||
            !sServerFacade.IsAlive(member))
            continue;
        const float distYd = bot->GetDistance(member);
        if (distYd > kVictimEscapeBopYd)
            continue;
        Unit* const boss = ImmuneBossOnVictim(member);
        if (!boss)
            continue;
        VictimEscapeGuards guards;
        const VictimEscape victimEscape = DecideFor(boss, member, nowMs, guards);
        if (ProtectorShouldBop(true, bopReady && !member->HasAura(kForbearanceId), false, victimEscape, distYd))
        {
            plan.boss = boss;
            plan.victim = member;
            plan.escape = VictimEscape::AskProtection;
            return true;
        }
        const char* const why = guards.escapedRecently ? nullptr : GuardWord(guards);
        if (why && victimEscape == VictimEscape::None && !PlayerbotAI::IsTank(member))
            LogVictimEscape(ai, boss, member, spellId, false, why, nowMs);
    }
    return false;
}

// Casts the planned escape on the planned victim and writes the row.
bool CastEscape(PlayerbotAI* ai, Player* bot, const EscapePlan& plan)
{
    const char* const spell = VictimEscapeWord(plan.escape);
    const uint32 spellId = ai->GetAiObjectContext()->GetValue<uint32>("spell id", spell)->Get();
    const std::uint32_t nowMs = WorldTimer::getMSTime();
    const char* const how = plan.victim == bot ? "own" : "protect";
    StopOwnCast(bot);
    if (!spellId || !ai->CastSpell(spell, plan.victim))
    {
        LogVictimEscape(ai, plan.boss, plan.victim, spellId, false, "cast", nowMs);
        return false;
    }
    NoteEscape(plan.victim, nowMs);
    LogVictimEscape(ai, plan.boss, plan.victim, spellId, true, how, nowMs);
    return true;
}
}

Unit* ai::ImmuneBossOnVictim(Unit* member)
{
    if (!member)
        return nullptr;
    for (Unit* attacker : member->GetAttackers())
    {
        if (!attacker || !attacker->IsInWorld() || !sServerFacade.IsAlive(attacker) || attacker->GetVictim() != member)
            continue;
        if (IsBossCreature(attacker) && TargetTauntImmune(attacker))
            return attacker;
    }
    return nullptr;
}

bool ImmuneBossVictimTrigger::IsActive()
{
    if (!sPlayerbotAIConfig.dungeonRunImmuneBossVictimEscape)
        return false;
    if (!sServerFacade.IsAlive(bot) || PlayerbotAI::IsTank(bot))
        return false;
    EscapePlan plan;
    return PlanOwnEscape(ai, bot, plan);
}

bool ImmuneBossVictimProtectTrigger::IsActive()
{
    if (!sPlayerbotAIConfig.dungeonRunImmuneBossVictimEscape || bot->GetClass() != CLASS_PALADIN)
        return false;
    EscapePlan plan;
    return PlanProtection(ai, bot, plan);
}

bool ImmuneBossVictimEscapeAction::Execute(Event& event)
{
    (void)event;
    EscapePlan plan;
    if (!sPlayerbotAIConfig.dungeonRunImmuneBossVictimEscape || !PlanOwnEscape(ai, bot, plan))
        return false;
    return CastEscape(ai, bot, plan);
}

bool ImmuneBossVictimProtectAction::Execute(Event& event)
{
    (void)event;
    EscapePlan plan;
    if (!sPlayerbotAIConfig.dungeonRunImmuneBossVictimEscape || !PlanProtection(ai, bot, plan))
        return false;
    return CastEscape(ai, bot, plan);
}
