#include "playerbot/playerbot.h"
#include "VictimEscapeActions.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/DungeonPlanMgr.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/VictimEscapePolicy.h"
#include "playerbot/CastCancelLog.h"
#include "playerbot/strategy/actions/BossTacticsActions.h"
#include "playerbot/strategy/values/ThreatValues.h"
#include "../../../../runtime/PlayerbotAIStorage.h"
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <unordered_map>
#include <utility>

using namespace ai;

namespace
{
const uint32 kForbearanceId = 25771;
// The throttle maps keep at most this many victims; the oldest entry goes first.
const std::size_t kVictimEscapeMapCap = 512;

// One escape per victim: when it was cast, and whether a later read saw it land.
struct EscapeRecord
{
    std::uint32_t castMs = 0;
    bool landed = false;
};

std::mutex g_victimEscapeMutex;
std::unordered_map<std::uint64_t, EscapeRecord> g_victimEscapeLast;
// (victim guid, writer guid) -> the last refused row: the victim's own refusal and each paladin's have their own slot.
std::map<std::pair<std::uint64_t, std::uint64_t>, std::uint32_t> g_victimEscapeRefusedLog;

template <typename Map, typename Stamp>
void EvictOldest(Map& map, Stamp stamp)
{
    if (map.size() < kVictimEscapeMapCap || map.empty())
        return;
    auto oldest = map.begin();
    for (auto it = map.begin(); it != map.end(); ++it)
        if (stamp(it->second) < stamp(oldest->second))
            oldest = it;
    map.erase(oldest);
}

bool IsBossCreature(Unit* unit)
{
    if (!unit || !unit->IsCreature())
        return false;
    Creature* const creature = static_cast<Creature*>(unit);
    return creature->IsWorldBoss() || GetDungeonPlans().IsBossEntry(creature->GetMapId(), creature->GetEntry());
}

// The core's own cast check on its own (Spell::CheckCast: stun, silence, pacify, reagents, cooldowns, range and sight)
// plus the power cost, without PlayerbotAI::CanCastSpell's in-progress gate: used only when that gate alone refused,
// because the escape breaks the cast it saw. The cost is priced without spell mods (a probe must not spend a charge).
bool CoreCastCheckOk(Player* caster, uint32 spellId, Unit* target)
{
    SpellEntry const* const info = sServerFacade.LookupSpellInfo(spellId);
    if (!info)
        return false;
    if (info->powerType < MAX_POWERS &&
        caster->GetPower(Powers(info->powerType)) < Spell::CalculatePowerCost(info, caster))
        return false;
    Spell* const spell = new Spell(caster, info, false);
    spell->m_targets.setUnitTarget(target);
    SpellCastResult result = spell->CheckCast(true);
    // The own cast's global cooldown is no refusal: breaking a preparing cast cancels it (Spell::cancel).
    Spell* const own = caster->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (VictimEscapeGcdOnlyRefusal(result == SPELL_FAILED_NOT_READY, own && own->getState() == SPELL_STATE_PREPARING,
                                   caster->HasSpellCooldown(spellId), caster->HasSpellCategoryCooldown(info->Category)))
        result = spell->CheckCast(false);
    delete spell;
    return result == SPELL_CAST_OK;
}

// Whether `caster` can cast `spell` on `target` now: the bot's full cast check, the path the real cast takes
// (PlayerbotAI::CanCastSpell), its "spell id" lookup choosing the rank. Read before anything is interrupted.
bool EscapeSpellReady(PlayerbotAI* casterAi, Player* caster, const char* spell, Unit* target)
{
    const uint32 spellId = casterAi->GetAiObjectContext()->GetValue<uint32>("spell id", spell)->Get();
    if (!spellId)
        return false;
    SpellCastResult check = SPELL_CAST_OK;
    const bool fullCheckOk = casterAi->CanCastSpell(spellId, target, 0, true, nullptr, false, false, false, &check);
    const bool refusedOnlyByOwnCast = !fullCheckOk && check == SPELL_FAILED_SPELL_IN_PROGRESS &&
        caster->IsNonMeleeSpellCasted(false);
    const bool coreCheckOk = refusedOnlyByOwnCast && CoreCastCheckOk(caster, spellId, target);
    return VictimEscapeCastReady(fullCheckOk, refusedOnlyByOwnCast, coreCheckOk);
}

// A real player (no bot AI) has no own escape here: the paladin protects them.
VictimEscapeReady ReadyOf(Player* member)
{
    VictimEscapeReady ready;
    PlayerbotAI* const memberAi = PlayerbotAIStorage::Instance().GetAI(member);
    if (!memberAi)
        return ready;
    // Ice Block and Divine Shield go out while stunned; Feign Death and Vanish do not.
    const bool inControl = !member->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL);
    switch (member->GetClass())
    {
        case CLASS_HUNTER:
            ready.feignDeath = inControl && EscapeSpellReady(memberAi, member, VictimEscapeWord(VictimEscape::FeignDeath), member);
            break;
        case CLASS_ROGUE:
            ready.vanish = inControl && EscapeSpellReady(memberAi, member, VictimEscapeWord(VictimEscape::Vanish), member);
            break;
        case CLASS_MAGE:
            ready.iceBlock = EscapeSpellReady(memberAi, member, VictimEscapeWord(VictimEscape::IceBlock), member);
            break;
        case CLASS_PALADIN:
            ready.divineShield = !member->HasAura(kForbearanceId) &&
                EscapeSpellReady(memberAi, member, VictimEscapeWord(VictimEscape::DivineShield), member);
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

// Live, engaged hostile creatures other than a boss whose current victim is this member (a Son, an add).
int OtherCreaturesOnVictim(Unit* boss, Player* victim)
{
    int count = 0;
    for (Unit* attacker : victim->GetAttackers())
    {
        if (!attacker || !attacker->IsInWorld())
            continue;
        if (CountsAsDutyAttacker(attacker == boss, attacker->IsCreature(), sServerFacade.IsAlive(attacker),
                                 attacker->IsInCombat(), attacker->GetVictim() == victim, IsBossCreature(attacker)))
            ++count;
    }
    return count;
}

bool HoldsBossDuty(Unit* boss, Player* victim)
{
    return VictimHoldsDuty(IsLiveWalkoutPuller(victim),
                           SeekMeetHealPuller(victim->GetInstanceId()) == victim->GetObjectGuid().GetRawValue(),
                           OtherCreaturesOnVictim(boss, victim));
}

// The escape landed: the feign-death state, the boss on another victim, or the victim immune to the boss's melee.
bool EscapeLanded(Unit* boss, Player* victim)
{
    return victim->HasUnitState(UNIT_STAT_FEIGN_DEATH) || boss->GetVictim() != victim ||
        victim->IsImmuneToDamage(boss->GetMeleeDamageSchoolMask());
}

VictimEscapeGuards GuardsFor(Unit* boss, Player* victim, std::uint32_t nowMs)
{
    VictimEscapeGuards guards;
    {
        std::lock_guard<std::mutex> lock(g_victimEscapeMutex);
        auto it = g_victimEscapeLast.find(victim->GetObjectGuid().GetRawValue());
        if (it != g_victimEscapeLast.end())
        {
            EscapeRecord& record = it->second;
            if (!record.landed && EscapeLanded(boss, victim))
                record.landed = true;
            guards.escapedRecently = VictimEscapeRecent(true, record.landed, nowMs - record.castMs);
        }
    }
    guards.otherNonTankTarget = OtherNonTankTarget(boss, victim);
    guards.holdsDuty = HoldsBossDuty(boss, victim);
    return guards;
}

bool BossInScope(Unit* boss)
{
    return VictimEscapeBossInScope(TargetTauntImmune(boss), ThreatSuccessorTarget(boss));
}

VictimEscape DecideFor(Unit* boss, Player* victim, std::uint32_t nowMs, VictimEscapeGuards& guards)
{
    guards = GuardsFor(boss, victim, nowMs);
    return VictimEscapeFor(sPlayerbotAIConfig.dungeonRunImmuneBossVictimEscape, PlayerbotAI::IsTank(victim),
                           boss->GetVictim() == victim, BossInScope(boss), victim->GetClass(), ReadyOf(victim),
                           guards);
}

// The cast went out; whether it landed is read on a later tick (GuardsFor).
void NoteEscape(Player* victim, std::uint32_t nowMs)
{
    std::lock_guard<std::mutex> lock(g_victimEscapeMutex);
    const std::uint64_t key = victim->GetObjectGuid().GetRawValue();
    if (g_victimEscapeLast.find(key) == g_victimEscapeLast.end())
        EvictOldest(g_victimEscapeLast, [](const EscapeRecord& r) { return r.castMs; });
    EscapeRecord& record = g_victimEscapeLast[key];
    record.castMs = nowMs;
    record.landed = false;
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
// victim and writer per 6 s.
void LogVictimEscape(PlayerbotAI* ai, Unit* boss, Player* victim, uint32 spellId, bool ok, const char* reason,
                     std::uint32_t nowMs)
{
    if (!sPlayerbotAIConfig.hasLog("bot_events.csv"))
        return;
    if (!ok)
    {
        std::lock_guard<std::mutex> lock(g_victimEscapeMutex);
        const auto key = std::make_pair(victim->GetObjectGuid().GetRawValue(), ai->GetBot()->GetObjectGuid().GetRawValue());
        if (g_victimEscapeRefusedLog.find(key) == g_victimEscapeRefusedLog.end())
            EvictOldest(g_victimEscapeRefusedLog, [](std::uint32_t ms) { return ms; });
        std::uint32_t& last = g_victimEscapeRefusedLog[key];
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
    {
        // The registry lookup takes a lock, so the diagnostics key gates it first.
        if (sPlayerbotAIConfig.raidBossDiagnostics)
            if (PlayerbotAI* ai = GET_PLAYERBOT_AI(bot))
                ai::LogCastCancelNonMelee(ai, "victim_escape", false);
        bot->InterruptNonMeleeSpells(false);
    }
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

uint32 SpellIdOf(PlayerbotAI* ai, VictimEscape e)
{
    return ai->GetAiObjectContext()->GetValue<uint32>("spell id", VictimEscapeWord(e))->Get();
}

// The victim's own escape, if one is due now (its full cast check already passed in ReadyOf). A duty or the
// only-target guard that holds back an escape the victim had ready writes the refused row (the throttle is the
// escape's own pace, not a refusal).
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
                LogVictimEscape(ai, boss, bot, SpellIdOf(ai, wanted), false, why, nowMs);
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

// The first group member within 30 yd this paladin protects now: its own full cast check of Blessing of Protection
// on that member (range, sight, silence, cooldown) passes, and the member carries no Forbearance.
bool PlanProtection(PlayerbotAI* ai, Player* bot, EscapePlan& plan)
{
    if (bot->GetClass() != CLASS_PALADIN || !bot->GetGroup() || !sServerFacade.IsAlive(bot))
        return false;
    const char* const bop = VictimEscapeWord(VictimEscape::AskProtection);
    const uint32 bopId = SpellIdOf(ai, VictimEscape::AskProtection);
    if (!bopId || !bot->HasSpell(bopId) || !sServerFacade.IsSpellReady(bot, bopId))
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
        if (victimEscape == VictimEscape::AskProtection)
        {
            const bool bopReady = !member->HasAura(kForbearanceId) && EscapeSpellReady(ai, bot, bop, member);
            if (ProtectorShouldBop(true, bopReady, false, victimEscape, distYd))
            {
                plan.boss = boss;
                plan.victim = member;
                plan.escape = VictimEscape::AskProtection;
                return true;
            }
            continue;
        }
        const char* const why = guards.escapedRecently ? nullptr : GuardWord(guards);
        if (why && victimEscape == VictimEscape::None && !PlayerbotAI::IsTank(member))
            LogVictimEscape(ai, boss, member, bopId, false, why, nowMs);
    }
    return false;
}

// Casts the planned escape on the planned victim and writes the row. The plan's cast check already passed, so the
// bot's own cast is broken only for an escape that can go out.
bool CastEscape(PlayerbotAI* ai, Player* bot, const EscapePlan& plan)
{
    const char* const spell = VictimEscapeWord(plan.escape);
    const uint32 spellId = ai->GetAiObjectContext()->GetValue<uint32>("spell id", spell)->Get();
    const std::uint32_t nowMs = WorldTimer::getMSTime();
    const char* const how = plan.victim == bot ? "own" : "protect";
    if (!spellId)
        return false;
    StopOwnCast(bot);
    if (!ai->CastSpell(spell, plan.victim))
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
        if (IsBossCreature(attacker) && BossInScope(attacker))
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
