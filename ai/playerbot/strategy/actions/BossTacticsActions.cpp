#include "playerbot/playerbot.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <atomic>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include "BossTacticsActions.h"
#include "playerbot/BossTacticsMgr.h"
#include "playerbot/BossStandMgr.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/policy/combat/AoeOffPolicy.h"
#include "playerbot/policy/raid/UseObjectPolicy.h"
#include "playerbot/GroupMembers.h"
#include "../../../../runtime/PlayerbotAIStorage.h"
#include "ChooseTargetActions.h"
#include "PositionAction.h"
#include "FollowActions.h"
#include "ReachTargetActions.h"
#include "playerbot/strategy/values/NearestGameObjects.h"
#include "EnemyAreaSpell.h"
#include "GenericSpellActions.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "Objects/Totem.h"
#include "Spells/SpellMgr.h"
#include "Database/DBCStores.h"
#include "playerbot/strategy/values/PartyMemberToHeal.h"

using namespace ai;

namespace
{
// Interrupt spells by name; the bot casts the first one it knows and can cast
// on the target right now (range, cooldown, stance and mana are CanCastSpell's).
const char* const kInterruptSpells[] = { "pummel", "shield bash", "kick", "counterspell", "earth shock" };

// The caster's current generic and channelled cast ids, each 0 when that slot is empty. A channel runs in its own
// slot (CURRENT_CHANNELED_SPELL), so a row for a channelled blast never shows up in the generic one.
void CurrentCastIds(Unit* unit, uint32& genericId, uint32& channeledId)
{
    Spell* generic = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    Spell* channeled = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
    genericId = generic && generic->m_spellInfo ? generic->m_spellInfo->Id : 0;
    channeledId = channeled && channeled->m_spellInfo ? channeled->m_spellInfo->Id : 0;
}

// The caster's cast slot a row matched: the same pick as ListedCastRadius, so the logged spell is the one whose radius
// the walk-out used (ties go to the generic slot). nullptr when neither slot is listed, which only a race can reach (the
// cast ended between FindBossCastDanger and here).
Spell* ListedCastSpell(const ActiveBossRows& active, Unit* caster)
{
    uint32 genericId = 0;
    uint32 channeledId = 0;
    CurrentCastIds(caster, genericId, channeledId);
    float const generic = AvoidCastRadius(active, caster->GetEntry(), genericId);
    float const channeled = AvoidCastRadius(active, caster->GetEntry(), channeledId);
    if (generic >= channeled && generic > 0.0f)
        return caster->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    return channeled > 0.0f ? caster->GetCurrentSpell(CURRENT_CHANNELED_SPELL) : nullptr;
}

// A bot on battle duty: not a tank, not a healer. The hold's role gate, for the clauses an explicit attack order may
// lift (IsFreeDps) and for those it may not (the area and attacker-debuff veto).
bool IsBattleDps(Player* bot)
{
    return !PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsHeal(bot);
}

// A bot on tank or healer duty keeps it; an explicit player order wins.
bool IsFreeDps(PlayerbotAI* ai, Player* bot)
{
    if (!IsBattleDps(bot))
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    return AI_VALUE(ObjectGuid, "explicit attack target").IsEmpty();
}

// dps_hold_after's fight-wide state, one entry per map instance, subject and row (BossTacticsPolicy.h DpsHoldState).
// The row's own key columns identify it: two rows of one boss differ in their spell or in their health band, so each
// keeps its own hold. Any bot's read feeds the state, as the linked_death latch does, so a hold one bot sees applies
// to every bot that reads it.
std::mutex g_dpsHoldMutex;
std::map<DpsHoldKey, DpsHoldState> g_dpsHolds;
// The trigger start each bot has logged for each key: one row per trigger and bot, not one per read.
std::mutex g_dpsHoldLogMutex;
std::map<std::pair<std::uint64_t, DpsHoldKey>, std::uint32_t> g_dpsHoldLog;
std::atomic<std::uint32_t> g_dpsHoldPruneMs{0};

DpsHoldKey DpsHoldKeyOf(Player* bot, const BossTacticRow& row, Unit* subject)
{
    return std::make_tuple(bot->GetMapId(), bot->GetInstanceId(), subject->GetObjectGuid().GetRawValue(),
                           row.bossEntry, row.spellId, row.hpMin, row.hpMax);
}

// The row's log row: the subject, the trigger and the hold it started.
void LogDpsHold(PlayerbotAI* ai, Player* bot, Unit* subject, const BossTacticRow& row, DpsHoldTrigger trigger,
                std::uint32_t triggerMs, const DpsHoldKey& key)
{
    if (!sPlayerbotAIConfig.hasLog("bot_events.csv") || trigger == DpsHoldTrigger::None)
        return;
    {
        std::lock_guard<std::mutex> lock(g_dpsHoldLogMutex);
        std::uint32_t& logged = g_dpsHoldLog[std::make_pair(bot->GetObjectGuid().GetRawValue(), key)];
        if (logged == triggerMs)
            return;
        logged = triggerMs;
    }
    char info1[48];
    snprintf(info1, sizeof(info1), "subject=%u", subject->GetEntry());
    char info2[96];
    snprintf(info2, sizeof(info2), "trigger=%s,held_ms=%u", DpsHoldTriggerName(trigger),
             static_cast<std::uint32_t>(row.param * 1000.0f + 0.5f));
    sPlayerbotAIConfig.logEvent(ai, "BossTacticsDpsHold", info1, info2);
}

// Drops the state of a subject that died and of a row nobody has read for kDpsHoldStaleMs; runs at most once a
// second, whatever asks. The dead subject's state is erased at once rather than after the stale timeout, so a
// respawned subject never inherits its previous pull's hold. The telemetry rows follow their holds: each bot's row
// goes with the hold it logged, and any row whose map instance is gone goes too.
void PruneDpsHolds(PlayerbotAI* ai, std::uint32_t nowMs)
{
    // The once-a-second throttle is read unlocked (a target pick asks it per candidate): two threads that both pass
    // the test re-check under the lock, which is what keeps the pass to once a second.
    std::uint32_t const last = g_dpsHoldPruneMs.load(std::memory_order_relaxed);
    if (last != 0 && nowMs - last < 1000)
        return;
    std::lock_guard<std::mutex> lock(g_dpsHoldMutex);
    std::uint32_t const ran = g_dpsHoldPruneMs.load(std::memory_order_relaxed);
    if (ran != 0 && nowMs - ran < 1000)
        return;
    g_dpsHoldPruneMs.store(nowMs, std::memory_order_relaxed);
    for (auto it = g_dpsHolds.begin(); it != g_dpsHolds.end();)
    {
        bool drop = nowMs - it->second.lastReadMs > kDpsHoldStaleMs;
        if (!drop)
        {
            Unit* subject = ai->GetUnit(ObjectGuid(std::get<2>(it->first)));
            drop = subject && !subject->IsAlive();
        }
        it = drop ? g_dpsHolds.erase(it) : std::next(it);
    }
    // Lock order: the hold map, then the log. LogDpsHold takes the log alone, after the hold lock is released.
    std::lock_guard<std::mutex> logLock(g_dpsHoldLogMutex);
    PruneDpsHoldLog(g_dpsHoldLog, g_dpsHolds, [](std::uint32_t mapId, std::uint32_t instanceId)
    {
        return sMapMgr.FindMap(mapId, instanceId) != nullptr;
    });
}

// One read of a dps_hold_after row against its subject: the gate, the cast slot or aura, the victim's role and the
// position, then DpsHoldTick. The state is the fight's, so a trigger one bot sees holds every bot that reads it.
// Returns whether the subject is held after this read. `bossUnit` is the row's boss entry unit, whose health the
// gate reads (never an add's).
bool ReadDpsHold(PlayerbotAI* ai, Player* bot, Unit* subject, const BossTacticRow& row, Unit* bossUnit,
                 DpsHoldTrigger* fired = nullptr)
{
    if (fired)
        *fired = DpsHoldTrigger::None;
    if (!ai || !bot || !subject || !subject->IsAlive() || !sPlayerbotAIConfig.dungeonRunAuraTactics)
        return false;
    int const bossHpPct = static_cast<int>((bossUnit ? bossUnit : subject)->GetHealthPercent());
    bool const botHasAura = row.botAura != 0 && bot->HasAura(row.botAura);
    bool const gateOpen = RowGateOpen(row, true, bossHpPct, botHasAura);
    uint32 genericId = 0;
    uint32 channeledId = 0;
    CurrentCastIds(subject, genericId, channeledId);
    bool const casting = row.spellId != 0 && (genericId == row.spellId || channeledId == row.spellId);
    bool const carrying = row.spellId != 0 && subject->HasAura(row.spellId);
    Unit* const victim = subject->GetVictim();
    bool const victimIsTank = victim && victim->GetTypeId() == TYPEID_PLAYER &&
        PlayerbotAI::IsTank(static_cast<Player*>(victim));
    DpsHoldInputs in;
    in.engaged = true;
    // A spell-less row holds from the first sighting (mode engage, until_tank or the default); a blink row holds only
    // on the jump, and a row with a spell on the cast or aura edge.
    in.engageEnabled = row.spellId == 0 && row.mode != TacticMode::Blink;
    in.gateOpen = gateOpen;
    in.botEligible = row.botAura == 0 || botHasAura;
    in.observer = bot->GetObjectGuid().GetRawValue();
    in.gateEnabled = row.hpMin != 0 || row.hpMax != 100;
    in.castOrAuraSeen = casting || carrying;
    in.castEnabled = row.spellId != 0;
    in.blinkEnabled = row.mode == TacticMode::Blink;
    in.untilTank = row.mode == TacticMode::UntilTank;
    in.victimIsTank = victimIsTank;
    in.holdSec = row.param;
    in.x = subject->GetPositionX();
    in.y = subject->GetPositionY();
    const DpsHoldKey key = DpsHoldKeyOf(bot, row, subject);
    DpsHoldTrigger lastTrigger = DpsHoldTrigger::None;
    std::uint32_t lastTriggerMs = 0;
    bool held = false;
    {
        std::lock_guard<std::mutex> lock(g_dpsHoldMutex);
        DpsHoldState& state = g_dpsHolds[key];
        held = DpsHoldTick(state, in, WorldTimer::getMSTime(), fired);
        lastTrigger = state.lastTrigger;
        lastTriggerMs = state.lastTriggerMs;
    }
    if (held && IsFreeDps(ai, bot))
        LogDpsHold(ai, bot, subject, row, lastTrigger, lastTriggerMs, key);
    return held;
}


// Whether the action damages enemies in an area: the threat mark, the shared action-name set (the thrown consumables,
// the class blasts the mark misses) or the spell's own shape (IsEnemyAreaSpell: every effect enumerating enemy units in
// an area, chains included). The mark alone carries almost no real area spell in this tree, which is why the other two
// are read.
bool IsAreaDamageAction(PlayerbotAI* ai, Action* action)
{
    if (!action)
        return false;
    // Heals and the melee sibling of the attacker debuff carry ACTION_THREAT_AOE (GenericSpellActions.h) but hit no
    // area: the same early returns FocusMultiplier makes before it reads the mark.
    if (dynamic_cast<CastHealingSpellAction*>(action) != nullptr)
        return false;
    if (dynamic_cast<CastMeleeDebuffSpellOnAttackerAction*>(action) != nullptr)
        return false;
    if (action->GetThreatType() == ActionThreatType::ACTION_THREAT_AOE)
        return true;
    if (IsAreaSpellAction(action->GetName()))
        return true;
    CastSpellAction* cast = dynamic_cast<CastSpellAction*>(action);
    if (!cast)
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    const uint32 spellId = AI_VALUE2(uint32, "spell id", cast->GetSpellName());
    return IsEnemyAreaSpell(sServerFacade.LookupSpellInfo(spellId));
}

// A buff that turns the bot's own swings into an area: Sweeping Strikes and Blade Flurry copy each melee hit onto
// nearby enemies, so a held member in melee reach is damaged without the action looking like an area cast.
bool IsCleaveToggleAction(Action* action)
{
    if (!action)
        return false;
    std::string const& name = action->GetName();
    return name == "sweeping strikes" || name == "blade flurry";
}

// A single-target damage action that lands on the bot's current target: a melee swing or an attack spell. Heals, the
// attacker debuff and area casts have their own clauses.
bool IsSingleTargetDamageAction(Action* action)
{
    if (!action)
        return false;
    if (dynamic_cast<CastHealingSpellAction*>(action) != nullptr)
        return false;
    if (action->GetTargetName() != "current target")
        return false;
    return dynamic_cast<AttackAction*>(action) != nullptr || dynamic_cast<CastSpellAction*>(action) != nullptr;
}

// Why every bot leaves `unit` alone right now: a hold_kill row of its entry with its blocker budget over (KillHeld),
// or a one_at_a_time row of its entry whose sole pick is another unit (OneAtATimeHolds). None when it is not held.
enum class HoldReason
{
    None,
    Budget,
    Sole,
    DpsHold,
};

// The subject of an active dps_hold_after row within `maxDistance` of `around`, or null: an area cast that lands
// beside it (or a DoT on it) would damage the creature the hold exists to keep quiet.
bool DpsHoldSubjectWithin(PlayerbotAI* ai, Unit* around, const BossTacticsFight& fight, float maxDistance,
                          Unit** subject)
{
    if (subject)
        *subject = nullptr;
    if (!ai || !around)
        return false;
    for (const BossTacticRow* row : fight.rows)
    {
        if (row->kind != BossTacticKind::DpsHoldAfter)
            continue;
        // Every live unit of the row's subject entry is read, so each one's trigger edge advances on this tick; the veto
        // is any of them held within range. The boss unit feeds the health gate (the subject's own health without one).
        std::vector<Unit*> subjects;
        Unit* bossUnit = nullptr;
        for (Unit* unit : fight.units)
        {
            if (unit->GetEntry() == TacticSubjectEntry(*row))
                subjects.push_back(unit);
            if (unit->GetEntry() == row->bossEntry)
                bossUnit = unit;
        }
        std::vector<bool> held;
        std::vector<float> distance;
        for (Unit* unit : subjects)
        {
            held.push_back(ReadDpsHold(ai, ai->GetBot(), unit, *row, bossUnit));
            distance.push_back(around->GetDistance(unit));
        }
        int const hit = DpsHoldVetoIndex(held, distance, maxDistance);
        if (hit < 0)
            continue;
        if (subject)
            *subject = subjects[hit];
        return true;
    }
    return false;
}

// The held subject within `maxDistance` of `around` (the action's target, or the bot itself), the unit itself and why
// it is held: an area cast that lands there would take it as well, and one area cast that kills several held Mad
// Servants together spawns their Voidwalkers together. The distance is kHoldKillAoeNearYd for the area clause and
// unbounded for the attacker debuff, whose target is held by itself. The units come from the "sole kill candidates"
// value (the fight-wide set of the row's entry, a crowd-controlled one included), never from fight.units, which is built
// from the bot's "attackers" list that "focus rti targets" collapses to the raid mark. The rule that picks the held one
// is BossTacticsPolicy's HeldSubjectIndex; the pick of a one_at_a_time row is computed once per row, not once per unit.
HoldReason HeldSubjectWithin(PlayerbotAI* ai, Unit* around, const BossTacticsFight& fight,
                             const std::function<std::uint32_t(std::uint32_t)>& countNear, float maxDistance,
                             Unit** subject)
{
    if (subject)
        *subject = nullptr;
    if (!ai || !around)
        return HoldReason::None;
    for (const BossTacticRow* row : fight.rows)
    {
        if (row->kind != BossTacticKind::HoldKill && row->kind != BossTacticKind::OneAtATime)
            continue;
        const bool budget = row->kind == BossTacticKind::HoldKill && KillHeld(fight.rows, row->bossEntry, countNear);
        if (row->kind == BossTacticKind::HoldKill && !budget)
            continue;
        const std::vector<SoleCandidate> candidates = SoleKillCandidates(ai, row->bossEntry);
        std::vector<Unit*> units(candidates.size(), nullptr);
        std::vector<float> distance(candidates.size(), std::numeric_limits<float>::infinity());
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            Unit* unit = ai->GetUnit(ObjectGuid(candidates[i].guid));
            if (!unit || !unit->IsAlive() || unit->GetEntry() != row->bossEntry)
                continue;
            units[i] = unit;
            distance[i] = around->GetDistance(unit);
        }
        const int index = HeldSubjectIndex(candidates, distance, maxDistance, budget);
        if (index < 0)
            continue;
        if (subject)
            *subject = units[index];
        return budget ? HoldReason::Budget : HoldReason::Sole;
    }
    // dps_hold_after: the subject of an active hold is left alone too, so an area cast that lands beside it (or a
    // DoT on it) does not damage the boss/creature the hold exists to keep quiet.
    if (sPlayerbotAIConfig.dungeonRunAuraTactics)
    {
        Unit* held = nullptr;
        if (DpsHoldSubjectWithin(ai, around, fight, maxDistance, &held))
        {
            if (subject)
                *subject = held;
            return HoldReason::DpsHold;
        }
    }
    return HoldReason::None;
}

// The held subject within the area radius of `around`.
HoldReason HeldSubjectNear(PlayerbotAI* ai, Unit* around, const BossTacticsFight& fight,
                           const std::function<std::uint32_t(std::uint32_t)>& countNear, Unit** subject)
{
    return HeldSubjectWithin(ai, around, fight, countNear, kHoldKillAoeNearYd, subject);
}

// The held subject at `around` itself, whatever the distance: the attacker debuff casts on this very unit, so its DoT
// ticks on it after the hold.
HoldReason HeldSubjectAt(PlayerbotAI* ai, Unit* around, const BossTacticsFight& fight,
                         const std::function<std::uint32_t(std::uint32_t)>& countNear, Unit** subject)
{
    return HeldSubjectWithin(ai, around, fight, countNear, std::numeric_limits<float>::max(), subject);
}

// The same question for the clauses that only need to know whether the pile is covered.
bool HeldUnitNear(PlayerbotAI* ai, Unit* around, const BossTacticsFight& fight,
                  const std::function<std::uint32_t(std::uint32_t)>& countNear)
{
    return HeldSubjectNear(ai, around, fight, countNear, nullptr) != HoldReason::None;
}

// The held member of a linked_death group within `maxDistance` of `around` (the action's target, or the bot itself),
// or null. While the group is held, an area cast that lands next to the held member takes it as well and a DoT on it
// ticks on after the hold: both damage a member that still has to live to the burn. The reach is the hold_kill veto's
// kHoldKillAoeNearYd; the member pick is BossTacticsPolicy's LinkedDeathHeldIndex.
Unit* LinkedDeathHeldNear(PlayerbotAI* ai, Unit* around, const BossLinkedDeathScan& scan, float maxDistance)
{
    if (!ai || !around || !scan.active)
        return nullptr;
    std::vector<float> distance(scan.members.size(), std::numeric_limits<float>::infinity());
    for (std::size_t i = 0; i < scan.units.size() && i < distance.size(); ++i)
    {
        Unit* unit = scan.units[i];
        if (unit && unit->IsAlive())
            distance[i] = around->GetDistance(unit);
    }
    const int index = LinkedDeathHeldIndex(scan.param, scan.members, distance, maxDistance);
    return index < 0 ? nullptr : scan.units[index];
}

// One bot_events.csv row per hold decision, at most one per bot per kHoldKillLogGapMs: the multiplier runs on every
// action of every tick and the action on every tick a blocker lives, while the walk that reads the rows needs the
// decision, not the repetition. The buckets are per bot AND kind: a bot whose hold-kill action fires every tick must
// not swallow its own veto rows (the two answer different questions). A no-op unless bot_events.csv is in
// AiPlayerbot.AllowedLogFiles, like the hunter telemetry and the avoid_cast row; the throttle is checked before any
// string is built.
constexpr uint32 kHoldKillLogGapMs = 5000;
enum class HoldKillLogKind : std::uint8_t
{
    Action,
    Veto,
};
std::mutex g_holdKillLogMutex;
std::unordered_map<uint64, uint32> g_holdKillLog;

// The linked_death release latch per group and map instance: map, instance, anchor, group id. Its state is the group's
// (a heal or a raise must not re-hold it for one bot while another still burns). Its members are tracked by guid with
// the latest observation from any bot; only an explicit death of a known one latches it, every known member confirmed
// dead clears it, and an evade resets it once every known member's latest observation is idle at full health whichever
// bot saw it (LinkedDeathLatchTick).
std::mutex g_linkedDeathLatchMutex;
std::map<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint8_t>, LinkedDeathLatch>
    g_linkedDeathLatches;

bool HoldKillLogDue(Player* bot, HoldKillLogKind kind)
{
    if (!sPlayerbotAIConfig.hasLog("bot_events.csv"))
        return false;
    const uint32 nowMs = WorldTimer::getMSTime();
    // The key mixes the bot guid with the kind: two kinds of the same bot never share a slot.
    const uint64 guid = bot->GetObjectGuid().GetRawValue();
    const uint64 key = guid ^ ((static_cast<uint64>(kind) + 1) * 0x9E3779B97F4A7C15ull);
    std::lock_guard<std::mutex> lock(g_holdKillLogMutex);
    uint32& last = g_holdKillLog[key];
    if (last != 0 && nowMs - last < kHoldKillLogGapMs)
        return false;
    last = nowMs;
    return true;
}

// The hold-kill action's row: the subject it holds, the blocker counts of the hold_kill rows of its entry and where the
// bot was sent.
//   info1 = "held=<entry>:<guid low>,hp=<pct>"
//   info2 = "blockers=<blocker entry>:<count>,...,to=<entry>:<guid low>|stop"
void LogHoldKillPick(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight, Unit* held, Unit* destination,
                     const std::function<std::uint32_t(std::uint32_t)>& countNear)
{
    if (!HoldKillLogDue(bot, HoldKillLogKind::Action))
        return;
    char info1[64];
    snprintf(info1, sizeof(info1), "held=%u:%u,hp=%u", held->GetEntry(), held->GetGUIDLow(),
             static_cast<uint32>(held->GetHealthPercent()));
    std::string info2 = "blockers=";
    bool first = true;
    for (const BossTacticRow* row : fight.rows)
    {
        if (row->kind != BossTacticKind::HoldKill || row->bossEntry != held->GetEntry())
            continue;
        if (!first)
            info2 += ",";
        first = false;
        info2 += std::to_string(row->targetEntry) + ":" + std::to_string(countNear(row->targetEntry));
    }
    if (first)
        info2 += "none";
    if (destination)
        info2 += ",to=" + std::to_string(destination->GetEntry()) + ":" + std::to_string(destination->GetGUIDLow());
    else
        info2 += ",to=stop";
    sPlayerbotAIConfig.logEvent(ai, "BossTacticsHoldKill", info1, info2);
}

// The multiplier's row: why it stood the action down (`veto` is the action class, or "sole" when the one_at_a_time pick
// rather than a hold_kill budget holds the subject) and the subject when the veto has one.
//   info1 = "veto=<assist|aoe|area|dot|sole>"
//   info2 = "held=<entry>:<guid low>,hp=<pct>" or "held=none"
void LogHoldKillVeto(PlayerbotAI* ai, Player* bot, const char* veto, Unit* subject)
{
    if (!HoldKillLogDue(bot, HoldKillLogKind::Veto))
        return;
    char info2[64];
    if (subject)
        snprintf(info2, sizeof(info2), "held=%u:%u,hp=%u", subject->GetEntry(), subject->GetGUIDLow(),
                 static_cast<uint32>(subject->GetHealthPercent()));
    else
        snprintf(info2, sizeof(info2), "held=none");
    sPlayerbotAIConfig.logEvent(ai, "BossTacticsHoldKill", std::string("veto=") + veto, info2);
}

// One bot_events.csv row per dispel decision (picked, waiting for a clear carrier, or excluded), at most one per bot
// and kind per kDispelLogGapMs: the predicate and the multiplier see the same member every tick, while the walk needs
// the decision, not the repetition. The two kinds never swallow each other's rows. A no-op unless bot_events.csv is in
// AiPlayerbot.AllowedLogFiles, like the hold rows; the throttle is checked before any string is built.
constexpr uint32 kDispelLogGapMs = 5000;
enum class DispelLogKind : std::uint8_t
{
    Pick,
    Excluded,
};
std::mutex g_dispelLogMutex;
std::unordered_map<uint64, uint32> g_dispelLog;

bool DispelLogDue(Player* bot, DispelLogKind kind)
{
    if (!sPlayerbotAIConfig.hasLog("bot_events.csv"))
        return false;
    const uint32 nowMs = WorldTimer::getMSTime();
    const uint64 key = bot->GetObjectGuid().GetRawValue() ^ ((static_cast<uint64>(kind) + 1) * 0x9E3779B97F4A7C15ull);
    std::lock_guard<std::mutex> lock(g_dispelLogMutex);
    uint32& last = g_dispelLog[key];
    if (last != 0 && nowMs - last < kDispelLogGapMs)
        return false;
    last = nowMs;
    return true;
}

const char* DispelModeWord(TacticMode mode)
{
    switch (mode)
    {
        case TacticMode::Clear:
            return "clear";
        case TacticMode::Never:
            return "never";
        default:
            return "now";
    }
}

// The verdict row: the aura that decided, its carrier, the row's mode, the member's rank, the carrier's distance to the
// nearest group member (the `clear` test) and what the cure did about it. A row without a listed aura names mode now
// (the default) and rank 255 (after every listed aura).
void LogDispel(PlayerbotAI* ai, Player* bot, const DispelVerdict& verdict, Unit* carrier, float allyYd,
               DispelLogKind kind, const char* word)
{
    if (!ai || !bot || !carrier || verdict.auraId == 0 || !DispelLogDue(bot, kind))
        return;
    char info1[64];
    snprintf(info1, sizeof(info1), "aura=%u,carrier=%u", verdict.auraId, carrier->GetGUIDLow());
    char info2[96];
    int const ally = allyYd == std::numeric_limits<float>::max() ? -1 : static_cast<int>(allyYd + 0.5f);
    snprintf(info2, sizeof(info2), "mode=%s,rank=%d,ally_yd=%d,verdict=%s", DispelModeWord(verdict.mode),
             verdict.rank, ally, word);
    sPlayerbotAIConfig.logEvent(ai, "BossTacticsDispel", info1, info2);
}
}

// The blocker count of the hold_kill rows ("hold kill blockers" value: one grid search per bot and blocker entry per
// second). Defined outside the anonymous namespace above: it is declared in BossTacticsActions.h.
std::uint32_t ai::HoldKillBlockers(PlayerbotAI* ai, std::uint32_t entry)
{
    if (!ai || !entry)
        return 0;
    AiObjectContext* context = ai->GetAiObjectContext();
    return AI_VALUE2(uint32, "hold kill blockers", static_cast<int32>(entry));
}

// The living, fighting units of a one_at_a_time entry within kHoldKillRangeYd of the bot, crowd-controlled ones included
// (`eligible` false): the "sole kill candidates" value, one cached grid search per bot and entry, the whole fight's units
// of the entry, never the bot's own "attackers" list, which "focus rti targets" collapses to the raid mark. The one
// reader of the value: the pick (SoleKillGuid) and the held-subject veto (HeldSubjectWithin) both go through it.
std::vector<SoleCandidate> ai::SoleKillCandidates(PlayerbotAI* ai, std::uint32_t entry)
{
    if (!ai || !entry)
        return std::vector<SoleCandidate>();
    AiObjectContext* context = ai->GetAiObjectContext();
    return AI_VALUE2(std::vector<SoleCandidate>, "sole kill candidates", static_cast<int32>(entry));
}

// The one living unit of a one_at_a_time entry the fight kills next: the lowest health among the eligible units of the
// entry that fight, the lowest guid on a tie (PickSoleKill). Every bot reads the same set, so they converge without a
// vote and without new state; 0 when nobody of the entry fights yet.
std::uint64_t ai::SoleKillGuid(PlayerbotAI* ai, std::uint32_t entry)
{
    const std::vector<SoleCandidate> candidates = SoleKillCandidates(ai, entry);
    const int pick = PickSoleKill(candidates);
    return pick < 0 ? 0 : candidates[pick].guid;
}

namespace
{
// One bot updates at a time on a thread, and every caller of a scan in that update (the target pick once per
// candidate, the freeze multiplier once per action, the triggers) asks the same question. Keep the last scan of the
// current AI update, keyed by the bot, its tick and the tactic table: a reload replaces the table, so the table's
// identity makes a cached answer from the old one unusable rather than stale. `live` is set only while an update
// runs (BossTacticsScanScope): an explicit command runs between updates where the tick does not move and the maps
// may have changed, so it re-scans and stores nothing.
struct FightScanCache
{
    bool live = false;
    PlayerbotAI* ai = nullptr;
    Player* bot = nullptr;
    std::uint32_t tick = 0;
    const BossTacticTable* table = nullptr;
    BossTacticsFight fight;
};
thread_local FightScanCache t_fightScan;
}

ai::BossTacticsScanScope::BossTacticsScanScope()
{
    t_fightScan.live = true;
}

ai::BossTacticsScanScope::~BossTacticsScanScope()
{
    t_fightScan.live = false;
    t_fightScan.ai = nullptr;
    t_fightScan.bot = nullptr;
    t_fightScan.table = nullptr;
    t_fightScan.fight = BossTacticsFight();
}

BossTacticsFight ai::ScanBossTactics(PlayerbotAI* ai, Player* bot)
{
    const BossTacticTable& table = GetBossTactics();
    std::uint32_t const tick = ai ? ai->UpdateTick() : 0;
    if (ai && FightScanCacheHit(t_fightScan.live, t_fightScan.ai == ai, t_fightScan.bot == bot, t_fightScan.tick,
            tick, t_fightScan.table == &table))
        return t_fightScan.fight;
    BossTacticsFight fight;
    const uint32 mapId = bot->GetMapId();
    if (!table.HasMap(mapId))
        return fight;

    AiObjectContext* context = ai->GetAiObjectContext();
    const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    // The living units of each entry the fight sees, in the order the attackers list showed them; an entry's rows are
    // loaded once, in its first-seen order.
    std::vector<std::uint32_t> seen;
    std::vector<std::vector<Unit*>> entryUnits;
    for (const ObjectGuid& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !sServerFacade.IsAlive(unit))
            continue;
        fight.units.push_back(unit);
        const uint32 entry = unit->GetEntry();
        const std::vector<std::uint32_t>::const_iterator known = std::find(seen.begin(), seen.end(), entry);
        if (known == seen.end())
        {
            seen.push_back(entry);
            entryUnits.push_back(std::vector<Unit*>(1, unit));
        }
        else
            entryUnits[known - seen.begin()].push_back(unit);
    }
    for (std::size_t i = 0; i < seen.size(); ++i)
    {
        const std::vector<BossTacticRow>* rows = table.Rows(mapId, seen[i]);
        if (rows)
        {
            // The band reads one unit of the row's boss entry, never an add's: of the same-entry attackers the one with
            // the highest max health (the real boss over its clones and adds), the highest current %, then the lowest
            // guid. A first-seen pick would let a low-health clone open a phase row.
            std::vector<BossGateUnit> gateUnits;
            for (Unit* unit : entryUnits[i])
            {
                BossGateUnit gateUnit;
                gateUnit.maxHealth = unit->GetMaxHealth();
                gateUnit.hpPct = static_cast<int>(unit->GetHealthPercent());
                gateUnit.guid = unit->GetObjectGuid().GetRawValue();
                gateUnits.push_back(gateUnit);
            }
            const int gatePick = PickBossGateUnit(gateUnits);
            int const bossHpPct = gatePick < 0 ? 0 : gateUnits[gatePick].hpPct;
            bool const keyOn = sPlayerbotAIConfig.dungeonRunAuraTactics;
            bool const useObjectOn = sPlayerbotAIConfig.dungeonRunUseObject;
            for (const BossTacticRow& row : *rows)
            {
                // The six aura kinds are the new key's own: with it off their rows are not read at all. A gated row of
                // an older kind is closed by RowGateOpen instead, so key-off behaviour is exactly the pre-gate one.
                if (!keyOn && IsAuraTacticKind(row.kind))
                    continue;
                // The use_object kind is its own key's: with it off its rows are not read at all, so key-off is
                // exactly today.
                if (!useObjectOn && row.kind == BossTacticKind::UseObject)
                    continue;
                if (!RowGateOpen(row, keyOn, bossHpPct, row.botAura != 0 && bot->HasAura(row.botAura)))
                    continue;
                fight.rows.push_back(&row);
            }
        }
    }
    if (ai && t_fightScan.live)
    {
        t_fightScan.ai = ai;
        t_fightScan.bot = bot;
        t_fightScan.tick = tick;
        t_fightScan.table = &table;
        t_fightScan.fight = fight;
    }
    return fight;
}

void ai::CollectDispelAuras(Unit* member, uint32 dispelType, std::vector<AuraOnMember>& out)
{
    if (!member)
        return;
    for (uint32 type = SPELL_AURA_NONE; type < TOTAL_AURAS; ++type)
    {
        Unit::AuraList const& auras = member->GetAurasByType((AuraType)type);
        for (Unit::AuraList::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
        {
            const SpellEntry* entry = (*itr)->GetSpellProto();
            if (entry && entry->Dispel == dispelType)
            {
                AuraOnMember aura;
                aura.spellId = entry->Id;
                aura.dispelType = dispelType;
                out.push_back(aura);
            }
        }
    }
}

void ai::CollectAllDispelAuras(Unit* member, std::vector<AuraOnMember>& out)
{
    if (!member)
        return;
    for (uint32 type = SPELL_AURA_NONE; type < TOTAL_AURAS; ++type)
    {
        Unit::AuraList const& auras = member->GetAurasByType((AuraType)type);
        for (Unit::AuraList::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
        {
            const SpellEntry* entry = (*itr)->GetSpellProto();
            if (entry && entry->Dispel != DISPEL_NONE)
            {
                AuraOnMember aura;
                aura.spellId = entry->Id;
                aura.dispelType = entry->Dispel;
                out.push_back(aura);
            }
        }
    }
}

float ai::NearestGroupMemberYd(Unit* member)
{
    if (!member)
        return std::numeric_limits<float>::max();
    // A dispelled unit can be a pet: its owner's group is the carrier's group, the distance is the pet's.
    Player* carrier = dynamic_cast<Player*>(member);
    if (!carrier)
    {
        Pet* pet = dynamic_cast<Pet*>(member);
        if (pet)
            carrier = dynamic_cast<Player*>(pet->GetOwner());
    }
    if (!carrier || !carrier->GetGroup())
        return std::numeric_limits<float>::max();
    float nearest = std::numeric_limits<float>::max();
    for (Player* other : LiveGroupMembers(carrier->GetGroup()))
    {
        if (!other || other == member || !other->IsAlive() || other->GetMapId() != member->GetMapId())
            continue;
        nearest = std::min(nearest, sServerFacade.GetDistance2d(member, other));
    }
    return nearest;
}

DispelVerdict ai::DispelMemberState(Unit* member, uint32 dispelType, const ActiveBossRows& rows, float defaultClearYd,
                                    float* allyYdOut)
{
    std::vector<AuraOnMember> auras;
    CollectDispelAuras(member, dispelType, auras);
    float const allyYd = DispelNeedsAllyDistance(rows, auras) ? NearestGroupMemberYd(member)
                                                             : std::numeric_limits<float>::max();
    if (allyYdOut)
        *allyYdOut = allyYd;
    return DispelMemberVerdict(rows, auras, dispelType, allyYd, defaultClearYd);
}

void ai::LogDispelPick(PlayerbotAI* ai, Player* bot, const DispelVerdict& verdict, Unit* carrier, float allyYd)
{
    LogDispel(ai, bot, verdict, carrier, allyYd, DispelLogKind::Pick, "pick");
}

void ai::LogDispelExcluded(PlayerbotAI* ai, Player* bot, const DispelVerdict& verdict, Unit* carrier, float allyYd)
{
    LogDispel(ai, bot, verdict, carrier, allyYd, DispelLogKind::Excluded, verdict.wait ? "wait" : "excluded");
}

BossInterruptScan ai::FindBossInterrupt(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight)
{
    BossInterruptScan scan;
    (void)bot;
    for (Unit* unit : fight.units)
    {
        // Either slot: a channelled rotation runs in the channel slot and never the generic one, and the kick on a
        // channel is the same rule as on a cast. The generic slot is read first, so a listing of the same id in both
        // resolves the same way for every bot.
        uint32 genericId = 0;
        uint32 channeledId = 0;
        CurrentCastIds(unit, genericId, channeledId);
        uint32 spellId = 0;
        if (genericId != 0 && IsInterruptListed(fight.rows, unit->GetEntry(), genericId))
            spellId = genericId;
        else if (channeledId != 0 && IsInterruptListed(fight.rows, unit->GetEntry(), channeledId))
            spellId = channeledId;
        if (spellId == 0)
            continue;
        for (const char* name : kInterruptSpells)
        {
            if (ai->CanCastSpell(std::string(name), unit))
            {
                scan.caster = unit;
                scan.spell = name;
                return scan;
            }
        }
    }
    return scan;
}

Unit* ai::FindBossKillFirst(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight)
{
    if (fight.rows.empty() || !IsFreeDps(ai, bot))
        return nullptr;

    std::vector<TacticCandidate> candidates;
    candidates.reserve(fight.units.size());
    for (Unit* unit : fight.units)
    {
        TacticCandidate c;
        c.entry = unit->GetEntry();
        c.distance = bot->GetDistance(unit);
        c.pickable = PossibleAttackTargetsValue::IsValid(unit, bot);
        candidates.push_back(c);
    }
    const int index = PickKillFirst(fight.rows, candidates);
    return index < 0 ? nullptr : fight.units[index];
}

Unit* ai::FindBossHoldKill(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight)
{
    if (fight.rows.empty() || !IsFreeDps(ai, bot))
        return nullptr;
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* current = AI_VALUE(Unit*, "current target");
    if (!current)
        return nullptr;
    const auto countNear = [ai](std::uint32_t entry) { return HoldKillBlockers(ai, entry); };
    return KillHeld(fight.rows, current->GetEntry(), countNear) ? current : nullptr;
}

bool ai::IsDpsHeld(PlayerbotAI* ai, Player* bot, Unit* unit, DpsHoldTrigger* trigger)
{
    if (trigger)
        *trigger = DpsHoldTrigger::None;
    if (!ai || !bot || !unit || !unit->IsAlive())
        return false;
    // The kind is the new key's own, and the map flag keeps every other map at one lookup.
    if (!sPlayerbotAIConfig.dungeonRunAuraTactics || !GetBossTactics().HasAuraTacticMap(bot->GetMapId()))
        return false;
    if (!ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT) || !IsFreeDps(ai, bot))
        return false;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    PruneDpsHolds(ai, WorldTimer::getMSTime());
    // The rows are read from the table, not from fight.rows: the scan drops a band-closed row, and a band opening
    // is one of the triggers (the edge has to be seen from the closed side too). The row's boss entry has to be in
    // the fight; the subject is the row's target or that boss.
    for (Unit* boss : fight.units)
    {
        const std::vector<BossTacticRow>* rows = GetBossTactics().Rows(bot->GetMapId(), boss->GetEntry());
        if (!rows)
            continue;
        for (const BossTacticRow& row : *rows)
        {
            if (row.kind != BossTacticKind::DpsHoldAfter || TacticSubjectEntry(row) != unit->GetEntry())
                continue;
            Unit* bossUnit = boss;
            if (TacticSubjectEntry(row) != boss->GetEntry())
            {
                bossUnit = nullptr;
                for (Unit* other : fight.units)
                    if (other->GetEntry() == row.bossEntry)
                    {
                        bossUnit = other;
                        break;
                    }
                if (!bossUnit)
                    continue;
            }
            DpsHoldTrigger fired = DpsHoldTrigger::None;
            if (ReadDpsHold(ai, bot, unit, row, bossUnit, &fired))
            {
                if (trigger && fired != DpsHoldTrigger::None)
                    *trigger = fired;
                return true;
            }
        }
    }
    return false;
}

// The linked_death group of the fight: every living, fighting unit of the group's entries within kHoldKillRangeYd of
// the bot, with the health fraction the pick compares and the attackability rule the kill_first pick applies. The
// group comes from the table's map rows and the members from the grid search the "sole kill candidates" value reads,
// never the bot's "attackers" list: "focus rti targets" collapses that one to the raid mark, so a mark on the anchor or
// on a partner would leave the group a single member (or none at all) and the hold released. A map that carries several
// groups: the one with the most living, fighting members is the fight's, the first on a tie.
BossLinkedDeathScan ai::ScanBossLinkedDeath(PlayerbotAI* ai, Player* bot)
{
    BossLinkedDeathScan scan;
    const BossTacticTable& table = GetBossTactics();
    // One anchor can carry several groups (its rows' priorities): each is scanned, so a group the old anchor list
    // hid under the first row's priority is not lost.
    for (const LinkedDeathGroupKey& key : table.LinkedDeathGroups(bot->GetMapId()))
    {
        const std::vector<BossTacticRow>* rows = table.Rows(bot->GetMapId(), key.anchor);
        if (!rows)
            continue;
        ActiveBossRows groupRows;
        groupRows.reserve(rows->size());
        for (const BossTacticRow& row : *rows)
            if (row.kind == BossTacticKind::LinkedDeath && row.priority == key.group)
                groupRows.push_back(&row);
        LinkedDeathGroup group;
        if (!FindLinkedDeathGroup(groupRows, key.anchor, group))
            continue;
        BossLinkedDeathScan groupScan;
        groupScan.active = true;
        groupScan.groupId = group.groupId;
        groupScan.anchor = group.bossEntry;
        groupScan.param = group.param;
        groupScan.order = group.burnOrder;
        // The time this scan is taken, before its reads and before the lock: every sighting it makes is stamped with it
        // (LinkedDeathLatchTick), so the order in which bots reach the lock does not decide which observation is newer.
        const std::uint32_t scanMs = WorldTimer::getMSTime();
        for (std::uint32_t entry : group.entries)
        {
            for (const SoleCandidate& candidate : SoleKillCandidates(ai, entry))
            {
                Unit* unit = ai->GetUnit(ObjectGuid(candidate.guid));
                if (!unit || !unit->IsAlive() || unit->GetEntry() != entry)
                    continue;
                LinkedMember member;
                member.guid = candidate.guid;
                member.entry = entry;
                member.hpPct = static_cast<int>(unit->GetHealthPercent());
                member.attackable = PossibleAttackTargetsValue::IsValid(unit, bot);
                groupScan.members.push_back(member);
                groupScan.units.push_back(unit);
            }
        }
        groupScan.released = LinkedDeathReleased(groupScan.param, groupScan.members);
        // The latch is per group and instance: a known member that dies while the release is armed keeps the burn on for
        // the rest of the group (M2), whichever bot reads it first. The arming is over every known member (the union of
        // all bots' reads, LinkedDeathArmed), never this bot's scan alone: a bot that sees two low members but not a
        // healthy third does not arm it. A known member this bot did not see living is looked up by its own guid: only
        // a unit found dead (or a corpse) is a death. One merely out of this bot's range or cache is not.
        std::lock_guard<std::mutex> lock(g_linkedDeathLatchMutex);
        LinkedDeathLatch& latch = g_linkedDeathLatches[std::make_tuple(bot->GetMapId(), bot->GetInstanceId(),
                                                                      groupScan.anchor, groupScan.groupId)];
        // A known guid not seen fighting is looked up too: found dead (or a corpse) it is a death; found alive it is an
        // idle sighting, read only while no member fights (the encounter-end check: an evade has every one back at full
        // health and out of combat). The reset compares the latest sighting of every known member from any bot, so two
        // observers that each see only part of the idle group still reset it. Never registered as a new member.
        std::vector<std::uint64_t> deadNow;
        std::vector<LinkedMember> idle;
        for (std::uint64_t guid : LinkedDeathUnseen(latch, groupScan.members))
        {
            Unit* known = ai->GetUnit(ObjectGuid(guid));
            if (known && !known->IsAlive())
            {
                deadNow.push_back(guid);
                continue;
            }
            if (!known || !groupScan.members.empty())
                continue;
            LinkedMember member;
            member.guid = guid;
            member.entry = known->GetEntry();
            member.hpPct = static_cast<int>(known->GetHealthPercent());
            member.inCombat = known->IsInCombat();
            idle.push_back(member);
        }
        LinkedDeathWindowEvent windowEvent;
        groupScan.latched = LinkedDeathLatchTick(latch, groupScan.param, groupScan.members, deadNow,
                                                 WorldTimer::getMSTime(), scanMs, idle,
                                                 LinkedDeathWindowMs(group.windowSec), &windowEvent);
        if (windowEvent.fired)
        {
            char info[96];
            snprintf(info, sizeof(info), "first_ms=%u,last_ms=%u,window_ms=%u,inside=%d", windowEvent.firstMs,
                     windowEvent.lastMs, windowEvent.windowMs, windowEvent.inside ? 1 : 0);
            sPlayerbotAIConfig.logEvent(ai, "BossTacticsLinkedWindow", info);
        }
        if (!scan.active || groupScan.members.size() > scan.members.size())
            scan = groupScan;
    }
    return scan;
}

Unit* ai::FindBossLinkedDeath(PlayerbotAI* ai, Player* bot)
{
    if (!IsFreeDps(ai, bot) || !sPlayerbotAIConfig.dungeonRunLinkedDeath ||
        !ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT) ||
        !GetBossTactics().HasLinkedDeathMap(bot->GetMapId()))
        return nullptr;
    const BossLinkedDeathScan scan = ScanBossLinkedDeath(ai, bot);
    if (!scan.active)
        return nullptr;
    // Held: the highest member (the equalise). Released, or latched released with a member back above param: the
    // burn order, every free DPS on the first living member of it, then the next.
    const bool hold = !scan.released && !scan.latched;
    const int pick = hold ? PickLinkedDeath(scan.param, scan.members)
                          : scan.order.empty() ? -1 : PickLinkedDeathBurn(scan.order, scan.members);
    return pick < 0 ? nullptr : scan.units[pick];
}

Unit* ai::FindBossFrontDanger(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight)
{
    (void)ai;
    if (PlayerbotAI::IsTank(bot))
        return nullptr;
    for (const BossTacticRow* row : fight.rows)
    {
        if (row->kind != BossTacticKind::Behind)
            continue;
        for (Unit* unit : fight.units)
        {
            if (unit->GetEntry() != TacticSubjectEntry(*row))
                continue;
            // The one holding the boss stays where it is.
            if (unit->GetVictim() == bot || !unit->GetVictim())
                continue;
            if (InsideBossFront(unit->GetPositionX(), unit->GetPositionY(), unit->GetOrientation(),
                                bot->GetPositionX(), bot->GetPositionY(), row->param))
                return unit;
        }
    }
    return nullptr;
}

float ai::CastDangerOf(const BossTacticsFight& fight, Unit* unit)
{
    if (!unit)
        return 0.0f;
    uint32 genericId = 0;
    uint32 channeledId = 0;
    CurrentCastIds(unit, genericId, channeledId);
    return ListedCastRadius(fight.rows, unit->GetEntry(), genericId, channeledId);
}

namespace
{
// The core hits a player whose centre is within the radius of the blast's source, in 3D and with no size factor
// (Spell.cpp SpellNotifierCreatureAndPlayer, PUSH_SRC_CENTER / PUSH_SELF_CENTER); a self-centred cast's source is
// the caster's position.
float CenterDistance(Unit* caster, float x, float y, float z)
{
    const float dx = caster->GetPositionX() - x;
    const float dy = caster->GetPositionY() - y;
    const float dz = caster->GetPositionZ() - z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// A tank holds the caster's blast when the caster is on it, or — only for a caster whose entry has a tank_hold row —
// when the caster is on a non-tank and this tank's own current target is the caster, so it can take it
// (BossTacticsPolicy.h HoldsCaster). Every other tank leaves like anyone else, or the rule would walk tanks into
// blasts that kill them (the Gurubashi Bat Rider). A non-player victim (a pet, a totem) is not counted: this file has
// no victim-to-player idiom to own one, only the player form below.
bool BotHoldsCaster(PlayerbotAI* ai, Player* bot, Unit* caster, const ActiveBossRows& active)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* victim = caster->GetVictim();
    const bool casterIsOnBot = victim == bot;
    const bool casterIsOnNonTank = victim && victim != bot && victim->GetTypeId() == TYPEID_PLAYER &&
        !PlayerbotAI::IsTank(static_cast<Player*>(victim));
    const bool casterTankHoldRow = HasTankHold(active, caster->GetEntry());
    // "current target" is what TankAssistAction's Attack() sets. A tank that has or picks the caster as its "tank
    // target" is the same tank: BossTacticsCastHoldMultiplier holds that very AttackAction while the caster's blast
    // applies to this bot, so "current target" can lag behind. "tank target" is read ONLY for a caster the row lists,
    // so the reaction engine pays for the value on the one caster whose blast this tank may hold, not every caster.
    const bool botTargetsCaster = AI_VALUE(Unit*, "current target") == caster ||
        (casterTankHoldRow && PlayerbotAI::IsTank(bot) && AI_VALUE(Unit*, "tank target") == caster);
    return HoldsCaster(PlayerbotAI::IsTank(bot), casterIsOnBot, casterIsOnNonTank, botTargetsCaster, casterTankHoldRow);
}
}

std::vector<CastDangerZone> ai::CastDangerZones(PlayerbotAI* ai, const BossTacticsFight& fight, Player* bot)
{
    std::vector<CastDangerZone> zones;
    for (Unit* unit : fight.units)
    {
        const float radius = CastDangerOf(fight, unit);
        if (radius <= 0.0f || (bot && BotHoldsCaster(ai, bot, unit, fight.rows)))
            continue;
        CastDangerZone zone;
        zone.x = unit->GetPositionX();
        zone.y = unit->GetPositionY();
        zone.z = unit->GetPositionZ();
        zone.radius = radius;
        zones.push_back(zone);
    }
    return zones;
}

BossCastDanger ai::FindBossCastDanger(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight)
{
    BossCastDanger danger;
    if (fight.rows.empty())
        return danger;
    float nearest = 0.0f;
    for (Unit* unit : fight.units)
    {
        const float radius = CastDangerOf(fight, unit);
        if (radius <= 0.0f)
            continue;
        const float distance = CenterDistance(unit, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        if (!MustLeaveCast(BotHoldsCaster(ai, bot, unit, fight.rows), radius, distance))
            continue;
        if (!danger.caster || distance < nearest)
        {
            danger.caster = unit;
            danger.radius = radius;
            nearest = distance;
        }
    }
    return danger;
}

namespace
{
// One bot's freeze_on_aura episode: from the rising edge to the aura's end. maxDrift is how far it moved since the
// freeze caught it, the walk's proof that the hold kept it inside Mandokir's 2 yd.
struct FreezeEpisode
{
    bool active = false;
    std::uint32_t aura = 0;
    std::uint32_t startMs = 0;
    float startX = 0.0f;
    float startY = 0.0f;
    float maxDrift = 0.0f;
    bool stopped = false;
};

std::mutex g_freezeMutex;
std::unordered_map<uint64, FreezeEpisode> g_freezeEpisodes;

// The episode's row: the aura, how long it held the bot and how far the bot drifted.
//   info1 = "aura=<id>"
//   info2 = "ms=<duration>,moved_yd=<max drift>"
void LogFreeze(PlayerbotAI* ai, const FreezeEpisode& episode, std::uint32_t nowMs)
{
    char info1[32];
    snprintf(info1, sizeof(info1), "aura=%u", episode.aura);
    char info2[64];
    snprintf(info2, sizeof(info2), "ms=%u,moved_yd=%.2f", nowMs - episode.startMs, episode.maxDrift);
    sPlayerbotAIConfig.logEvent(ai, "BossTacticsFreeze", info1, info2);
}

// The flee action's row, throttled per bot and aura (a walk takes several ticks): where the carrier stood when it
// was sent, the clearance it needs and whether a walk started.
constexpr uint32 kFleeLogGapMs = 2000;
std::mutex g_fleeLogMutex;
std::unordered_map<uint64, uint32> g_fleeLog;

bool FleeLogDue(Player* bot, std::uint32_t aura)
{
    if (!sPlayerbotAIConfig.hasLog("bot_events.csv"))
        return false;
    const uint32 nowMs = WorldTimer::getMSTime();
    const uint64 key = bot->GetObjectGuid().GetRawValue() ^ (static_cast<uint64>(aura) << 32);
    std::lock_guard<std::mutex> lock(g_fleeLogMutex);
    uint32& last = g_fleeLog[key];
    if (last != 0 && nowMs - last < kFleeLogGapMs)
        return false;
    last = nowMs;
    return true;
}

inline const char* FleeModeName(TacticMode mode)
{
    return mode == TacticMode::ToTank ? "to_tank" : "away";
}

//   info1 = "aura=<id>,mode=<away|to_tank>"
//   info2 = "ally_yd=<d|none>,radius=<d>,walk=<started|none>"
void LogFlee(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, float allyYd, float radius, bool started)
{
    if (!FleeLogDue(bot, row.spellId))
        return;
    char info1[64];
    snprintf(info1, sizeof(info1), "aura=%u,mode=%s", row.spellId, FleeModeName(row.mode));
    char info2[96];
    if (allyYd >= 0.0f)
        snprintf(info2, sizeof(info2), "ally_yd=%.1f,radius=%.1f,walk=%s", allyYd, radius, started ? "started" : "none");
    else
        snprintf(info2, sizeof(info2), "ally_yd=none,radius=%.1f,walk=%s", radius, started ? "started" : "none");
    sPlayerbotAIConfig.logEvent(ai, "BossTacticsFlee", info1, info2);
}
}

std::uint32_t ai::FreezeAuraOnBot(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight)
{
    (void)ai;
    for (const BossTacticRow* row : fight.rows)
    {
        if (row->kind == BossTacticKind::FreezeOnAura && row->spellId != 0 && bot->HasAura(row->spellId))
            return row->spellId;
    }
    return 0;
}

void ai::FreezeEpisodeTick(PlayerbotAI* ai, Player* bot, std::uint32_t aura)
{
    const uint32 nowMs = WorldTimer::getMSTime();
    const float x = bot->GetPositionX();
    const float y = bot->GetPositionY();
    const uint64 guid = bot->GetObjectGuid().GetRawValue();
    FreezeEpisode ended;
    bool logEnded = false;
    {
        std::lock_guard<std::mutex> lock(g_freezeMutex);
        auto it = g_freezeEpisodes.find(guid);
        if (aura == 0)
        {
            if (it != g_freezeEpisodes.end())
            {
                if (it->second.active)
                {
                    ended = it->second;
                    logEnded = true;
                }
                g_freezeEpisodes.erase(it);
            }
        }
        else if (it == g_freezeEpisodes.end() || !it->second.active || it->second.aura != aura)
        {
            FreezeEpisode& state = g_freezeEpisodes[guid];
            state = FreezeEpisode();
            state.active = true;
            state.aura = aura;
            state.startMs = nowMs;
            state.startX = x;
            state.startY = y;
        }
        else
        {
            const float dx = x - it->second.startX;
            const float dy = y - it->second.startY;
            const float drift = std::sqrt(dx * dx + dy * dy);
            if (drift > it->second.maxDrift)
                it->second.maxDrift = drift;
        }
    }
    if (logEnded && sPlayerbotAIConfig.hasLog("bot_events.csv"))
        LogFreeze(ai, ended, nowMs);
}

bool ai::FreezeStopDue(PlayerbotAI* ai, Player* bot, std::uint32_t aura)
{
    (void)ai;
    if (aura == 0)
        return false;
    std::lock_guard<std::mutex> lock(g_freezeMutex);
    auto it = g_freezeEpisodes.find(bot->GetObjectGuid().GetRawValue());
    if (it == g_freezeEpisodes.end() || !it->second.active || it->second.aura != aura || it->second.stopped)
        return false;
    it->second.stopped = true;
    return true;
}

const BossTacticRow* ai::ActiveFleeRow(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight)
{
    (void)ai;
    for (const BossTacticRow* row : fight.rows)
    {
        if (row->kind == BossTacticKind::FleeOnAura && row->spellId != 0 && bot->HasAura(row->spellId))
            return row;
    }
    return nullptr;
}

bool ai::FleeRowListsAura(PlayerbotAI* ai, Player* bot, std::uint32_t spellId)
{
    if (!spellId || !sPlayerbotAIConfig.dungeonRunAuraTactics)
        return false;
    if (!GetBossTactics().HasAuraTacticMap(bot->GetMapId()))
        return false;
    // Only a row the tactics actually run wins: without the strategy (the gate off) the hard-coded runout stays the
    // carrier's only mover.
    if (!ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT))
        return false;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    for (const BossTacticRow* row : fight.rows)
    {
        if (row->kind == BossTacticKind::FleeOnAura && row->spellId == spellId)
            return true;
    }
    return false;
}

bool BossTacticsFreezeAction::Execute(Event& event)
{
    (void)event;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    const std::uint32_t aura = FreezeAuraOnBot(ai, bot, fight);
    if (!aura)
        return false;
    FreezeEpisodeTick(ai, bot, aura);
    // Fear, confuse and knockback run their own movement generator and end on their own: the carrier is stood still
    // once they are over, never cut out of them.
    bool const lostControl = bot->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL);
    // Attack and cast once on the rising edge: the multiplier keeps every other action down, so repeating them each
    // tick would only re-break a cast the bot cannot start anyway.
    bool const risingEdge = !lostControl && FreezeStopDue(ai, bot, aura);
    // Read before the clear: a chase's or a follow's Finalize drops the generator and leaves its spline walking, so
    // only this test tells the carrier is still being carried forward.
    bool const splineFinalized = bot->movespline->Finalized();
    const FreezeStill still = FreezeStillTick(risingEdge, lostControl,
        bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == IDLE_MOTION_TYPE, splineFinalized);
    if (still.attackStop)
    {
        bot->AttackStop();
        bot->CastStop();
    }
    if (still.dropGenerator)
    {
        bot->GetMotionMaster()->Clear();
        bot->GetMotionMaster()->MoveIdle();
    }
    if (still.stopSpline)
        ai->StopMoving();
    return true;
}

bool BossTacticsFleeAction::Execute(Event& event)
{
    (void)event;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    const BossTacticRow* row = ActiveFleeRow(ai, bot, fight);
    if (!row)
        return false;
    const bool toTank = row->mode == TacticMode::ToTank;
    const float radius = row->param > 0.0f ? row->param : (toTank ? 5.0f : sPlayerbotAIConfig.bombRunoutDistance);
    float allyYd = -1.0f;
    bool started = false;
    if (toTank)
    {
        // The mark already moves the tank's own add; a tank carrier has nothing to bring to itself.
        if (PlayerbotAI::IsTank(bot))
            return false;
        Player* tank = GetRaidTankRoles(ai).MainTank();
        if (!tank || tank == bot || !sServerFacade.IsAlive(tank) || tank->GetMapId() != bot->GetMapId())
            return false;
        allyYd = bot->GetDistance2d(tank);
        if (allyYd > radius)
            started = MoveTo(tank, radius);
    }
    else
    {
        Group* group = bot->GetGroup();
        if (!group)
            return false;
        for (Player* member : LiveGroupMembers(group))
        {
            if (!member || member == bot || !sServerFacade.IsAlive(member) || member->GetMapId() != bot->GetMapId())
                continue;
            const float distance = bot->GetDistance2d(member);
            if (allyYd < 0.0f || distance < allyYd)
                allyYd = distance;
        }
        started = RunOutFromGroup(*this, ai, bot, radius, true);
    }
    LogFlee(ai, bot, *row, allyYd, radius, started);
    return started;
}

float BossTacticsFreezeMultiplier::GetValue(Action* action)
{
    if (!action || !sPlayerbotAIConfig.dungeonRunAuraTactics)
        return 1.0f;
    if (!GetBossTactics().HasAuraTacticMap(bot->GetMapId()))
        return 1.0f;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    if (fight.rows.empty())
        return 1.0f;
    const std::uint32_t aura = FreezeAuraOnBot(ai, bot, fight);
    const bool isFreeze = dynamic_cast<BossTacticsFreezeAction*>(action) != nullptr;
    return FreezeHoldsAction(aura != 0, isFreeze) ? 1.0f : 0.0f;
}

bool BossTacticsInterruptAction::Execute(Event& event)
{
    (void)event;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    const BossInterruptScan scan = FindBossInterrupt(ai, bot, fight);
    return scan.caster && ai->CastSpell(std::string(scan.spell), scan.caster);
}

bool BossTacticsDispelSweepAction::Execute(Event& event)
{
    (void)event;
    return SweepBossDispelCures(ai, bot, true);
}

bool BossTacticsKillFirstAction::Execute(Event& event)
{
    (void)event;
    Unit* pick = FindBossKillFirst(ai, bot, ScanBossTactics(ai, bot));
    if (!pick)
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* current = AI_VALUE(Unit*, "current target");
    if (current == pick)
        return false;
    return Attack(bot, pick);
}

bool BossTacticsHoldKillAction::Execute(Event& event)
{
    (void)event;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    Unit* held = FindBossHoldKill(ai, bot, fight);
    if (!held)
        return false;
    const auto countNear = [botAi = ai](std::uint32_t entry) { return HoldKillBlockers(botAi, entry); };
    // The kill_first pick moves the bot off a held boss entry (a Voidwalker, the Blood Priest). A pick that is the held
    // entry itself falls through: the hold row's whole point is to leave it.
    if (Unit* pick = FindBossKillFirst(ai, bot, fight))
    {
        if (pick != held)
        {
            LogHoldKillPick(ai, bot, fight, held, pick, countNear);
            return Attack(bot, pick);
        }
    }
    // Else the nearest creature this bot may attack, by the same candidate rules the kill_first pick applies
    // (PossibleAttackTargetsValue::IsValid and kBossTacticsEngageRange), minus the entries a hold_kill row holds.
    std::vector<TacticCandidate> candidates;
    candidates.reserve(fight.units.size());
    for (Unit* unit : fight.units)
    {
        TacticCandidate c;
        c.entry = unit->GetEntry();
        c.distance = bot->GetDistance(unit);
        c.pickable = PossibleAttackTargetsValue::IsValid(unit, bot);
        candidates.push_back(c);
    }
    const int nearestIndex = PickNearestUnheld(fight.rows, candidates, countNear);
    if (nearestIndex >= 0)
    {
        LogHoldKillPick(ai, bot, fight, held, fight.units[nearestIndex], countNear);
        return Attack(bot, fight.units[nearestIndex]);
    }
    // Nothing to switch to: drop the held pick and stop, so the assist rows re-pick once the blocker is dead. Every
    // picker skips a held entry, so the bot has no dps target while the blocker lives and waits the Servant out.
    LogHoldKillPick(ai, bot, fight, held, nullptr, countNear);
    AiObjectContext* context = ai->GetAiObjectContext();
    context->GetValue<Unit*>("current target")->Set(nullptr);
    context->GetValue<ObjectGuid>("attack target")->Set(ObjectGuid());
    bot->AttackStop();
    return true;
}

bool BossTacticsDpsHoldAction::Execute(Event& event)
{
    (void)event;
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* current = AI_VALUE(Unit*, "current target");
    if (!current || !IsDpsHeld(ai, bot, current))
        return false;
    // The kill_first add is still fair game (an add that has to die is not the subject whose damage is held); with
    // none, drop the pick and stop, so every picker (all of which skip a held subject) re-picks once the hold is over.
    if (Unit* pick = FindBossKillFirst(ai, bot, ScanBossTactics(ai, bot)))
    {
        if (pick != current)
            return Attack(bot, pick);
    }
    context->GetValue<Unit*>("current target")->Set(nullptr);
    context->GetValue<ObjectGuid>("attack target")->Set(ObjectGuid());
    bot->AttackStop();
    return true;
}

bool BossTacticsLinkedDeathAction::Execute(Event& event)
{
    (void)event;
    Unit* pick = FindBossLinkedDeath(ai, bot);
    if (!pick)
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    if (AI_VALUE(Unit*, "current target") == pick)
        return false;
    return Attack(bot, pick);
}

float BossTacticsHoldMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;
    const bool dpsAssist = dynamic_cast<DpsAssistAction*>(action) != nullptr;
    const bool tankAssist = dynamic_cast<TankAssistAction*>(action) != nullptr;
    const bool aoe = dynamic_cast<DpsAoeAction*>(action) != nullptr;
    const bool attackerDebuff = dynamic_cast<CastRangedDebuffSpellOnAttackerAction*>(action) != nullptr;
    const bool other = !dpsAssist && !tankAssist && !aoe && !attackerDebuff;
    // The linked_death clause is opt-in like its pick: without the key or the strategy the rows are inert.
    const bool linkedDeath = sPlayerbotAIConfig.dungeonRunLinkedDeath &&
        ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT);
    // dps_hold_after is the aura-tactics key's own: with it off (or the strategy off) its rows are inert here too.
    const bool auraHolds = sPlayerbotAIConfig.dungeonRunAuraTactics &&
        ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT) &&
        GetBossTactics().HasAuraTacticMap(bot->GetMapId());
    // A heal, a buff or a move has only the area clause to answer, and that one needs a hold_kill, one_at_a_time,
    // linked_death or dps_hold_after row: a map with only kill_first or interrupt rows (every other raid) pays for no
    // scan here.
    if (other && !GetBossTactics().HasHoldMap(bot->GetMapId()) &&
        !(linkedDeath && GetBossTactics().HasLinkedDeathMap(bot->GetMapId())) && !auraHolds)
        return 1.0f;
    AiObjectContext* context = ai->GetAiObjectContext();
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    // The area test (a dynamic_cast and a spell lookup) only runs for a fight whose rows can hold a member: on any
    // other fight, and for any other action class, the clause below is dead weight. The linked rows are read from the
    // map, not from the fight's rows: "focus rti targets" gives the fight the raid mark's entry only, so a mark on a
    // partner would hide the group from the veto.
    bool holdsRows = linkedDeath && GetBossTactics().HasLinkedDeathMap(bot->GetMapId());
    bool dpsHoldRows = false;
    for (const BossTacticRow* row : fight.rows)
    {
        if (row->kind == BossTacticKind::HoldKill || row->kind == BossTacticKind::OneAtATime)
            holdsRows = true;
        if (auraHolds && row->kind == BossTacticKind::DpsHoldAfter)
            dpsHoldRows = true;
    }
    const bool area = (holdsRows || dpsHoldRows) && IsAreaDamageAction(ai, action);
    // The linked_death clauses also answer a tank's area action (Thunder Clap, Consecration, Swipe), a passive cleave
    // toggle (Sweeping Strikes, Blade Flurry) and a single-target attack on the current target: none of those is a
    // hold_kill clause, and the area test alone misses the cleave buffs.
    const bool cleaveToggle = linkedDeath && IsCleaveToggleAction(action);
    const bool singleTarget = linkedDeath && !area && IsSingleTargetDamageAction(action);
    if (!dpsAssist && !tankAssist && !aoe && !area && !attackerDebuff && !cleaveToggle && !singleTarget)
        return 1.0f;
    const auto countNear = [botAi = ai](std::uint32_t entry) { return HoldKillBlockers(botAi, entry); };
    // The log row's veto kind: the action class that was stood down, replaced by "sole" wherever the one_at_a_time
    // pick rather than a hold_kill budget is what holds the subject (the two need different fixes).
    const char* const veto = dpsAssist || tankAssist || singleTarget ? "assist"
        : aoe || cleaveToggle ? "aoe"
        : attackerDebuff ? "dot" : "area";
    // A held subject is left alone by every bot that is not a tank or a healer, an explicit attack order included: the
    // bot stands in the same pile, its area cast or grenade would take the held Mad Servants with it, and a DoT on a
    // held Servant ticks on after the hold. Ahead of the free-DPS gates, which such a bot does not reach. The area
    // clause lands on the action's target or on the bot itself (a self-centred blast, a sapper charge); the attacker
    // debuff casts on its "attacker without aura" target, held by itself.
    if (IsBattleDps(bot) && (area || attackerDebuff))
    {
        Unit* subject = nullptr;
        HoldReason held = attackerDebuff
            ? HeldSubjectAt(ai, action->GetTarget(), fight, countNear, &subject)
            : HeldSubjectNear(ai, action->GetTarget(), fight, countNear, &subject);
        if (held == HoldReason::None && area)
            held = HeldSubjectNear(ai, bot, fight, countNear, &subject);
        if (held != HoldReason::None)
        {
            LogHoldKillVeto(ai, bot, held == HoldReason::Sole ? "sole" : veto, subject);
            return 0.0f;
        }
    }
    // The same veto for a linked_death group, on every bot but a healer: an area cast on another member that stands
    // beside a held one takes the held member too, and a DoT on it ticks on after the hold. A tank's Thunder Clap,
    // Consecration or Swipe and a cleave buff have the same reach (M3); so has a melee swing or bolt on a held current
    // target, which a bot left with only held members in reach would otherwise keep doing (M4). The burn phase
    // releases the group, so the casts are allowed again and the members die together. DoTs already ticking on a
    // member keep ticking: the veto only stands down new casts (documented limit).
    if (linkedDeath && !PlayerbotAI::IsHeal(bot) && (area || attackerDebuff || cleaveToggle || singleTarget))
    {
        const BossLinkedDeathScan scan = ScanBossLinkedDeath(ai, bot);
        Unit* linkedHeld = nullptr;
        if (singleTarget)
        {
            // A tank's swing counts too: it kills the held member just as a DPS's does. An explicit attack order still
            // wins, as for the pick; a healer has no damage action here.
            const bool ordered = !context->GetValue<ObjectGuid>("explicit attack target")->Get().IsEmpty();
            Unit* current = ordered ? nullptr : AI_VALUE(Unit*, "current target");
            if (current && LinkedDeathHoldsMember(scan.param, scan.members,
                                                  current->GetObjectGuid().GetRawValue()))
                linkedHeld = current;
        }
        else if (cleaveToggle)
            linkedHeld = LinkedDeathHeldNear(ai, bot, scan, kHoldKillAoeNearYd);
        else
        {
            const float reach = attackerDebuff ? std::numeric_limits<float>::max() : kHoldKillAoeNearYd;
            linkedHeld = LinkedDeathHeldNear(ai, action->GetTarget(), scan, reach);
            if (!linkedHeld && area)
                linkedHeld = LinkedDeathHeldNear(ai, bot, scan, reach);
        }
        if (linkedHeld)
        {
            LogHoldKillVeto(ai, bot, veto, linkedHeld);
            return 0.0f;
        }
    }
    // Nothing but the three target-choosing actions goes further: a heal, an area cast or a DoT has no kill_first
    // clause to answer, and none of them may fall through to the hold_kill clause below (the area and the debuff are
    // already settled above; a heal is never held at all).
    if (!dpsAssist && !tankAssist && !aoe)
        return 1.0f;
    // hold_kill: a free DPS bot (IsFreeDps, the gate HeldDpsTarget uses: no tank, no healer, no explicit attack order)
    // leaves a held boss entry alone (the row's blocker is the kill). The assist rows are stood down on the target they
    // would send the bot to — the dps target for the assist, the dps aoe target for the AoE — as well as on the current
    // one. A tank or healer keeps its own pick, and TankAssistAction never has a kill_first pick to hold.
    const bool freeDps = IsFreeDps(ai, bot);
    if ((dpsAssist || aoe) && freeDps)
    {
        Unit* picked = aoe ? AI_VALUE(Unit*, "dps aoe target") : AI_VALUE(Unit*, "dps target");
        if (picked && KillHeld(fight.rows, picked->GetEntry(), countNear))
        {
            LogHoldKillVeto(ai, bot, veto, picked);
            return 0.0f;
        }
    }
    Unit* current = AI_VALUE(Unit*, "current target");
    if (!current)
        return 1.0f;
    if (FindBossKillFirst(ai, bot, fight) == current)
    {
        LogHoldKillVeto(ai, bot, veto, nullptr);
        return 0.0f;
    }
    if (!dpsAssist && !aoe)
        return 1.0f;
    if (!freeDps)
        return 1.0f;
    if (KillHeld(fight.rows, current->GetEntry(), countNear))
    {
        LogHoldKillVeto(ai, bot, veto, current);
        return 0.0f;
    }
    if (aoe && HeldUnitNear(ai, current, fight, countNear))
    {
        LogHoldKillVeto(ai, bot, veto, current);
        return 0.0f;
    }
    return 1.0f;
}

namespace
{
// How far a member can carry a protected aura and still be hit by an area cure (a totem pulse, Stoneform's own
// removal, an Abolish Poison tick on a nearby ally): the raid's practical radius, the same 30 yd the bomb runout uses.
constexpr float kDispelSafetyYd = 30.0f;

// The area and periodic cures a dispel_priority row must be able to stand down as a whole: one cast removes (or keeps
// removing each tick) auras from more than the chosen target.
bool IsDispelAreaCure(const std::string& spell)
{
    return spell == "abolish disease" || spell == "disease cleansing totem" ||
           spell == "poison cleansing totem" || spell == "stoneform";
}

void AddDispelType(std::vector<uint32>& out, int32 type)
{
    if (type < 0)  // DISPEL_ALL: every type a friendly cure can remove
    {
        for (int32 t = DISPEL_MAGIC; t <= DISPEL_POISON; ++t)
            AddDispelType(out, t);
        return;
    }
    if (type == DISPEL_NONE || std::find(out.begin(), out.end(), static_cast<uint32>(type)) != out.end())
        return;
    out.push_back(static_cast<uint32>(type));
}

// The dispel types a spell removes, read from its effects: a dispel effect names its type, and an immunity that strips
// auras on application (Stoneform) names the types it strips. A periodic trigger is followed one step, so a cleansing
// totem's passive aura yields the type its pulse removes. Empty when the spell removes nothing.
void CollectSpellDispelTypes(SpellEntry const* entry, std::vector<uint32>& out, int depth = 0)
{
    if (!entry)
        return;
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        if (entry->Effect[i] == SPELL_EFFECT_DISPEL)
            AddDispelType(out, entry->EffectMiscValue[i]);
        else if (entry->EffectApplyAuraName[i] == SPELL_AURA_DISPEL_IMMUNITY &&
                 (entry->AttributesEx & SPELL_ATTR_EX_DISPEL_AURAS_ON_IMMUNITY))
            AddDispelType(out, entry->EffectMiscValue[i]);
        else if (depth == 0 && entry->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_TRIGGER_SPELL)
            CollectSpellDispelTypes(sSpellMgr.GetSpellEntry(entry->EffectTriggerSpell[i]), out, depth + 1);
    }
}

// The dispel types the spell an action casts removes, resolved by name the way the action resolves it.
std::vector<uint32> ActionDispelTypes(PlayerbotAI* ai, const std::string& spell)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    uint32 const spellId = AI_VALUE2(uint32, "spell id", spell);
    std::vector<uint32> types;
    CollectSpellDispelTypes(sServerFacade.LookupSpellInfo(spellId), types);
    return types;
}

// The fight's dispel rows only: a map without one has nothing to rank, and the scan is not paid for.
ActiveBossRows DispelRows(const BossTacticsFight& fight)
{
    ActiveBossRows rows;
    for (const BossTacticRow* row : fight.rows)
    {
        if (row->kind == BossTacticKind::DispelPriority)
            rows.push_back(row);
    }
    return rows;
}

// The living group members a cure can reach, the bot included.
std::vector<Unit*> DispelSweepMembers(Player* bot)
{
    std::vector<Unit*> members;
    members.push_back(bot);
    Group* group = bot->GetGroup();
    if (!group)
        return members;
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || member == bot || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
            continue;
        members.push_back(member);
    }
    return members;
}

// The radius a totem's pulse reaches: the dispel effect of the spell it triggers, or the safety radius when the data
// names none.
float TotemDispelRadius(SpellEntry const* totemSpell)
{
    if (!totemSpell)
        return kDispelSafetyYd;
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        if (totemSpell->EffectApplyAuraName[i] != SPELL_AURA_PERIODIC_TRIGGER_SPELL)
            continue;
        SpellEntry const* pulse = sSpellMgr.GetSpellEntry(totemSpell->EffectTriggerSpell[i]);
        if (!pulse)
            continue;
        for (int j = 0; j < MAX_EFFECT_INDEX; ++j)
        {
            if (pulse->Effect[j] != SPELL_EFFECT_DISPEL)
                continue;
            SpellRadiusEntry const* entry = sSpellRadiusStore.LookupEntry(pulse->EffectRadiusIndex[j]);
            if (entry && entry->Radius > 0.0f)
                return entry->Radius;
        }
    }
    return kDispelSafetyYd;
}

// What an already-running area cure would do to a protected aura: the Abolish aura a bot cast on a member (removed
// server-side, only auras cast by our bots), and the bot's own cleansing totem of the type (destroyed). With `apply`
// false the scan only reports whether there is anything to do.
bool SweepDispelCures(PlayerbotAI* ai, Player* bot, const ActiveBossRows& rows, float defaultClearYd, bool apply)
{
    bool swept = false;
    const std::vector<Unit*> members = DispelSweepMembers(bot);
    for (Unit* member : members)
    {
        // Collected first: the removal below mutates the holder map.
        std::vector<std::pair<uint32, ObjectGuid>> doomed;
        for (const auto& held : member->GetSpellAuraHolderMap())
        {
            SpellAuraHolder* aura = held.second;
            if (!aura)
                continue;
            std::vector<uint32> types;
            CollectSpellDispelTypes(aura->GetSpellProto(), types);
            if (types.empty())
                continue;
            ObjectGuid const casterGuid = aura->GetCasterGuid();
            Player* caster = sObjectMgr.GetPlayer(casterGuid);
            if (!caster || !GET_PLAYERBOT_AI(caster))
                continue;
            for (uint32 type : types)
            {
                const DispelVerdict verdict = DispelMemberState(member, type, rows, defaultClearYd);
                // The aura that decided is the protected one itself: never remove that.
                if (!verdict.excluded || verdict.auraId == aura->GetId())
                    continue;
                doomed.push_back(std::make_pair(aura->GetId(), casterGuid));
                break;
            }
        }
        for (const auto& entry : doomed)
        {
            if (apply)
                member->RemoveAurasByCasterSpell(entry.first, entry.second);
            swept = true;
        }
    }

    for (int slot = 0; slot < MAX_TOTEM_SLOT; ++slot)
    {
        Totem* totem = bot->GetTotem(TotemSlot(slot));
        if (!totem || !totem->IsAlive())
            continue;
        SpellEntry const* totemSpell = sSpellMgr.GetSpellEntry(totem->GetSpell());
        std::vector<uint32> types;
        CollectSpellDispelTypes(totemSpell, types);
        if (types.empty())
            continue;
        float const radius = TotemDispelRadius(totemSpell);
        bool destroy = false;
        for (Unit* member : members)
        {
            if (sServerFacade.GetDistance2d(totem, member) > radius)
                continue;
            for (uint32 type : types)
            {
                if (DispelMemberState(member, type, rows, defaultClearYd).excluded)
                {
                    destroy = true;
                    break;
                }
            }
            if (destroy)
                break;
        }
        if (!destroy)
            continue;
        if (apply)
            totem->UnSummon();
        swept = true;
    }
    return swept;
}
}

// The sweep an already-running area cure needs: a protected aura applied after the cure started would be removed by
// its next tick or pulse, so the bot-cast Abolish aura is removed and the bot's own cleansing totem destroyed. Gated
// by AiPlayerbot.DungeonRunAuraTactics, the map's dispel rows and the "boss tactics" strategy.
bool ai::SweepBossDispelCures(PlayerbotAI* ai, Player* bot, bool apply)
{
    if (!ai || !bot || !sPlayerbotAIConfig.dungeonRunAuraTactics)
        return false;
    if (!GetBossTactics().HasDispelMap(bot->GetMapId()))
        return false;
    if (!ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT))
        return false;
    const ActiveBossRows rows = DispelRows(ScanBossTactics(ai, bot));
    if (rows.empty())
        return false;
    return SweepDispelCures(ai, bot, rows, sPlayerbotAIConfig.bombRunoutDistance, apply);
}

// The hold at the last gate: the engine consults the multiplier when it picks the action, but a group member can gain
// a protected aura in between (a debuff lands while the bot walks into range), and the core resolves the dispel type
// of a Cure cast at random, so the cast itself has to be refused rather than merely deprioritised.
bool ai::DispelCastBlocked(PlayerbotAI* ai, Unit* target, uint32 spellId)
{
    if (!ai || !target || !sPlayerbotAIConfig.dungeonRunAuraTactics)
        return false;
    Player* bot = ai->GetBot();
    if (!bot || !target->IsAlive() || target->GetMapId() != bot->GetMapId())
        return false;
    // PartyMemberToDispel offers non-summoned pets as well as players; a hunter pet has no Player, so it is placed by
    // its owner's group and judged like a member. A target that is none of these is unknown, and the cast is refused.
    DispelTargetKind targetKind = DispelTargetKind::Unknown;
    if (target == bot)
    {
        targetKind = DispelTargetKind::Self;
    }
    else if (Player* member = sObjectMgr.GetPlayer(target->GetObjectGuid()))
    {
        if (member->GetGroup() && member->GetGroup() == bot->GetGroup())
            targetKind = DispelTargetKind::GroupMember;
    }
    else if (Pet* pet = dynamic_cast<Pet*>(target))
    {
        Player* owner = dynamic_cast<Player*>(pet->GetOwner());
        if (owner && owner->GetGroup() && owner->GetGroup() == bot->GetGroup())
            targetKind = DispelTargetKind::GroupPet;
    }
    if (!GetBossTactics().HasDispelMap(bot->GetMapId()))
        return false;
    if (!ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT))
        return false;
    std::vector<uint32> types;
    CollectSpellDispelTypes(sSpellMgr.GetSpellEntry(spellId), types);
    if (types.empty())
        return false;
    const ActiveBossRows rows = DispelRows(ScanBossTactics(ai, bot));
    if (rows.empty())
        return false;
    std::vector<AuraOnMember> auras;
    CollectAllDispelAuras(target, auras);
    float const verdictYd = DispelNeedsAllyDistance(rows, auras) ? NearestGroupMemberYd(target)
                                                                : std::numeric_limits<float>::max();
    DispelVerdict const verdict = DispelCastVerdict(targetKind, rows, auras, types, verdictYd,
                                                    sPlayerbotAIConfig.bombRunoutDistance);
    if (!verdict.excluded)
        return false;
    LogDispelExcluded(ai, bot, verdict, target, sServerFacade.GetDistance2d(bot, target));
    return true;
}

float BossTacticsDispelHoldMultiplier::GetValue(Action* action)
{
    if (!action || !sPlayerbotAIConfig.dungeonRunAuraTactics)
        return 1.0f;
    if (!GetBossTactics().HasDispelMap(bot->GetMapId()))
        return 1.0f;
    if (!ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT))
        return 1.0f;

    // A cure action, by its typed target (a party cure) or by the spell it casts (a self, totem or Stoneform cure).
    // The hold judges every type the cast spell removes, not only the one that triggered it: a poison-triggered
    // Cleanse also strips magic and disease, and the core picks one of the spell's types at random.
    CurePartyMemberAction* partyCure = dynamic_cast<CurePartyMemberAction*>(action);
    CastSpellAction* cast = partyCure ? nullptr : dynamic_cast<CastSpellAction*>(action);
    if (!partyCure && !cast)
        return 1.0f;
    const std::string& spell = partyCure ? partyCure->GetSpellName() : cast->GetSpellName();
    const std::vector<uint32> types = ActionDispelTypes(ai, spell);
    if (types.empty())
        return 1.0f;
    bool const areaCure = !partyCure && IsDispelAreaCure(spell);

    const ActiveBossRows rows = DispelRows(ScanBossTactics(ai, bot));
    if (rows.empty())
        return 1.0f;
    const float defaultClearYd = sPlayerbotAIConfig.bombRunoutDistance;

    const auto veto = [&](Unit* member)
    {
        std::vector<AuraOnMember> auras;
        CollectAllDispelAuras(member, auras);
        float const allyYd = DispelNeedsAllyDistance(rows, auras) ? NearestGroupMemberYd(member)
                                                                 : std::numeric_limits<float>::max();
        const DispelVerdict verdict = DispelCureVerdict(rows, auras, types, allyYd, defaultClearYd);
        if (!verdict.excluded)
            return false;
        LogDispelExcluded(ai, bot, verdict, member, allyYd);
        return true;
    };

    if (partyCure)
    {
        // The chosen member: a cure on an excluded one would remove the aura that must stay (the core picks one of
        // the type at random), so the action is stood down and the predicate offers somebody else.
        Unit* target = action->GetTarget();
        return target && veto(target) ? 0.0f : 1.0f;
    }

    // A self cure judges the bot itself; an area or periodic cure judges every living group member it can reach.
    if (!areaCure)
        return veto(bot) ? 0.0f : 1.0f;
    if (veto(bot))
        return 0.0f;
    Group* group = bot->GetGroup();
    if (group)
    {
        for (Player* member : LiveGroupMembers(group))
        {
            if (!member || member == bot || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
                continue;
            if (sServerFacade.GetDistance2d(bot, member) > kDispelSafetyYd)
                continue;
            if (veto(member))
                return 0.0f;
        }
    }
    return 1.0f;
}

float BossTacticsCastHoldMultiplier::GetValue(Action* action)
{
    if (!action || !GetBossTactics().HasAvoidCastMap(bot->GetMapId()))
        return 1.0f;
    MovementAction* move = dynamic_cast<MovementAction*>(action);
    if (!move)
        return 1.0f;
    bool const walkOut = dynamic_cast<BossTacticsAvoidCastAction*>(action) != nullptr;
    if (walkOut)
        return 1.0f;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    if (fight.rows.empty())
        return 1.0f;
    const std::vector<CastDangerZone> zones = CastDangerZones(ai, fight, bot);
    if (zones.empty())
        return 1.0f;
    // A stay walks the bot back to its spot: held while that spot lies in a blast.
    if (dynamic_cast<ReturnToStayPositionAction*>(action))
    {
        AiObjectContext* context = ai->GetAiObjectContext();
        PositionMap& positions = AI_VALUE(PositionMap&, "position");
        auto stay = positions.find("stay");
        bool const has = stay != positions.end() && stay->second.isSet() && stay->second.mapId == bot->GetMapId();
        return has && CastHoldsMove(false, false, true, stay->second.x, stay->second.y, stay->second.z, zones) ? 0.0f
                                                                                                             : 1.0f;
    }
    // Any other move towards a unit (a reach, a chase, a heal walk, a follow): held while that unit stands in a blast.
    // A follow does not name its unit as the action's target (it reads "follow target" in Execute), and it walks to
    // the bot's formation spot around that unit: held while either the unit or that spot stands in a blast.
    Unit* target = nullptr;
    if (dynamic_cast<FollowAction*>(action))
    {
        AiObjectContext* context = ai->GetAiObjectContext();
        target = AI_VALUE(Unit*, "follow target");
        WorldPosition const spot = AI_VALUE(WorldPosition, "formation position");
        if (spot.GetMapId() == bot->GetMapId() &&
            CastHoldsMove(false, false, true, spot.getX(), spot.getY(), spot.getZ(), zones))
            return 0.0f;
    }
    else
        target = action->GetTarget();
    if (!target || !target->IsInWorld() || target->GetMapId() != bot->GetMapId())
        return 1.0f;
    return CastHoldsMove(false, target == bot, true, target->GetPositionX(), target->GetPositionY(),
                         target->GetPositionZ(), zones)
               ? 0.0f
               : 1.0f;
}

float TrapHoldMultiplier::GetValue(Action* action)
{
    if (!action || !dynamic_cast<MovementAction*>(action))
        return 1.0f;
    // An attack picks a target and engages it; the pick-up is not a walk into
    // fire, and holding one leaves the bot unable to fight at all.
    if (dynamic_cast<AttackAction*>(action))
        return 1.0f;
    // Both walk-outs must run: the step out of the ground effect, and the listed
    // cast's own way out (its destination may lie in a fire beside the blast).
    if (dynamic_cast<AvoidAoeAction*>(action) || dynamic_cast<BossTacticsAvoidCastAction*>(action))
        return 1.0f;
    // Combat only: a follow or a return to the stay outside combat is the
    // engine's travel, not a bot walking back into a fight's fire.
    if (!bot->IsInCombat())
        return 1.0f;
    // A stay walks the bot back to its spot: held while that spot lies in a fire.
    if (dynamic_cast<ReturnToStayPositionAction*>(action))
    {
        AiObjectContext* context = ai->GetAiObjectContext();
        PositionMap& positions = AI_VALUE(PositionMap&, "position");
        auto stay = positions.find("stay");
        if (stay == positions.end() || !stay->second.isSet() || stay->second.mapId != bot->GetMapId())
            return 1.0f;
        std::vector<AoeHazardCircle> hazards;
        CollectDamagingTrapHazardsAt(ai, bot, stay->second.x, stay->second.y, stay->second.z, hazards);
        return HazardsHoldMove(false, false, true, stay->second.x, stay->second.y, stay->second.z, hazards)
                   ? 0.0f
                   : 1.0f;
    }
    // A follow walks to its formation spot around the unit, not to the unit:
    // that spot is the destination, so the unit's own position is not judged.
    if (dynamic_cast<FollowAction*>(action))
    {
        AiObjectContext* context = ai->GetAiObjectContext();
        WorldPosition const spot = AI_VALUE(WorldPosition, "formation position");
        if (spot.GetMapId() != bot->GetMapId())
            return 1.0f;
        std::vector<AoeHazardCircle> hazards;
        CollectDamagingTrapHazardsAt(ai, bot, spot.getX(), spot.getY(), spot.getZ(), hazards);
        return HazardsHoldMove(false, false, true, spot.getX(), spot.getY(), spot.getZ(), hazards) ? 0.0f : 1.0f;
    }
    // Any other move towards a unit (a reach, a chase, a heal walk): judge the
    // point where the walk stops. A reach stops short of its target by the
    // action's stop range, so a target standing in fire with its near edge
    // clear does not hold the reach.
    Unit* target = action->GetTarget();
    if (!target || target == bot || !target->IsInWorld() || target->GetMapId() != bot->GetMapId())
        return 1.0f;
    float stopRange = 0.0f;
    if (ReachTargetAction* reach = dynamic_cast<ReachTargetAction*>(action))
    {
        stopRange = reach->GetStopRange();
        if (stopRange <= 0.0f)
            stopRange = bot->GetCombatReach() + target->GetCombatReach();
    }
    AoePoint const endpoint = ReachEndpoint(target->GetPositionX(), target->GetPositionY(),
        target->GetPositionZ(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), stopRange);
    std::vector<AoeHazardCircle> hazards;
    CollectDamagingTrapHazardsAt(ai, bot, endpoint.x, endpoint.y, endpoint.z, hazards);
    // A hostile ground cloud holds a reach or chase the same way: the melee,
    // the tank included, wait at the cloud's edge while the tank's own step-out
    // drags the boss out. Holding the tank's reach is what stops it walking
    // back under a boss that never left the cloud; the walk-out itself is never
    // held (both early returns above). Creature-cast clouds only: the open
    // world and PvP against players are unchanged.
    std::vector<AoeHazardCircle> dynObjHazards;
    CollectDamagingDynObjHazardsAt(ai, bot, endpoint.x, endpoint.y, endpoint.z, dynObjHazards, true);
    hazards.insert(hazards.end(), dynObjHazards.begin(), dynObjHazards.end());
    return HazardsHoldMove(false, false, true, endpoint.x, endpoint.y, endpoint.z, hazards) ? 0.0f : 1.0f;
}

// Not MovementAction::isUseful: that one is false on a stay, and the bots of a run fight on one (a pull's anchor hold,
// a healer's stand). The blast kills in one hit; the stay walks the bot back once the cast is over.
bool BossTacticsAvoidCastAction::isUseful()
{
    return true;
}

namespace
{
// The walk-out's candidate points: the clear ring points (AvoidCastEscapes, shortest walk first), each on the ground
// and checked against every blast in 3D and for sight. Writes up to kAvoidCastPoints points; returns how many.
struct AvoidCastSpot
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

int AvoidCastSpots(Player* bot, Unit* caster, float radius, const std::vector<CastDangerZone>& zones, AvoidCastSpot* out)
{
    AvoidCastPoint points[kAvoidCastPoints];
    const int count = AvoidCastEscapes(caster->GetPositionX(), caster->GetPositionY(), bot->GetPositionX(),
                                       bot->GetPositionY(), radius, bot->GetOrientation() + M_PI_F, zones, points);
    int n = 0;
    for (int i = 0; i < count; ++i)
    {
        float z = std::max(caster->GetPositionZ(), bot->GetPositionZ()) + 2.0f;
        bot->UpdateGroundPositionZ(points[i].x, points[i].y, z);
        // The ground under a ring point can lie on another floor (a ledge, the stairs below) and so inside the 3D
        // radius again; another caster's blast is no way out either.
        if (InsideAnyCastZone(zones, points[i].x, points[i].y, z))
            continue;
        if (!bot->IsWithinLOS(points[i].x, points[i].y, z + bot->GetCollisionHeight()))
            continue;
        out[n].x = points[i].x;
        out[n].y = points[i].y;
        out[n].z = z;
        ++n;
    }
    return n;
}
}

// Possible only with a usable way out, so the reaction engine does not pick the walk-out for a bot that has none (it
// would keep its cast; see ShouldReactionInterruptCast).
bool BossTacticsAvoidCastAction::isPossible()
{
    if (!MovementAction::isPossible() || !ai->CanMove())
        return false;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    const BossCastDanger danger = FindBossCastDanger(ai, bot, fight);
    if (!danger.caster)
        return false;
    AvoidCastSpot spots[kAvoidCastPoints];
    return AvoidCastSpots(bot, danger.caster, danger.radius, CastDangerZones(ai, fight, bot), spots) > 0;
}

namespace
{
// Each bot's last walk-out (BossTacticsPolicy.h AvoidCastLogState), by bot guid, so one cast logs one line per bot.
// Only touched while bot_events.csv is on; a mutex guards it because every bot's engine reaches it.
std::mutex g_avoidCastLogMutex;
std::unordered_map<uint64, AvoidCastLogState> g_avoidCastLog;

// One bot_events.csv row per bot per cast (the audit of the boss tactics' avoid_cast walk-out; the walk-6/7 sampler of
// the bots' current action missed a walk that lasts less than its poll). Row: event BossTacticsAvoidCast, then
//   info1 = "caster=<entry>:<guid low>,spell=<id>"
//   info2 = "dist=<bot to caster centre before the move>,radius=<row radius>,dest=<distance of the point MoveTo took,
//            or none>,stopped=<1 when the walk-out broke the bot's own cast>,walk=<started|deferred|none>"
// walk: started = a walk is under way; deferred = MoveTo took the point but no walk started (a collapsed path, a
// transport or teleport wait); none = no usable point. A no-op unless bot_events.csv is in AiPlayerbot.AllowedLogFiles,
// like the hunter telemetry; the throttle is checked before any string is built.
void LogAvoidCast(PlayerbotAI* ai, Player* bot, Unit* caster, Spell* spell, uint32 spellId, float radius, float distBefore,
                  float destDistance, bool stopped, AvoidCastWalk walk)
{
    uint32 const nowMs = WorldTimer::getMSTime();
    {
        std::lock_guard<std::mutex> lock(g_avoidCastLogMutex);
        if (!AvoidCastIsNewCast(g_avoidCastLog[bot->GetObjectGuid().GetRawValue()], caster->GetObjectGuid().GetRawValue(),
                                spellId, static_cast<uint64>(reinterpret_cast<std::uintptr_t>(spell)), nowMs))
            return;
    }
    char info1[64];
    snprintf(info1, sizeof(info1), "caster=%u:%u,spell=%u", caster->GetEntry(), caster->GetGUIDLow(), spellId);
    char info2[112];
    if (destDistance >= 0.0f)
        snprintf(info2, sizeof(info2), "dist=%.1f,radius=%.1f,dest=%.1f,stopped=%d,walk=%s", distBefore, radius,
                 destDistance, stopped ? 1 : 0, AvoidCastWalkName(walk));
    else
        snprintf(info2, sizeof(info2), "dist=%.1f,radius=%.1f,dest=none,stopped=%d,walk=%s", distBefore, radius,
                 stopped ? 1 : 0, AvoidCastWalkName(walk));
    sPlayerbotAIConfig.logEvent(ai, "BossTacticsAvoidCast", info1, info2);
}

bool WalkUnderWay(Player* bot)
{
    return sServerFacade.isMoving(bot) || !bot->movespline->Finalized();
}
}

bool BossTacticsAvoidCastAction::Execute(Event& event)
{
    (void)event;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    const BossCastDanger danger = FindBossCastDanger(ai, bot, fight);
    if (!danger.caster)
        return false;
    Unit* caster = danger.caster;
    const bool logIt = sPlayerbotAIConfig.hasLog("bot_events.csv");
    const float distBefore = logIt ? CenterDistance(caster, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ())
                                   : 0.0f;
    AvoidCastSpot spots[kAvoidCastPoints];
    const int count = AvoidCastSpots(bot, caster, danger.radius, CastDangerZones(ai, fight, bot), spots);
    bool stopped = false;
    float destDistance = -1.0f;
    AvoidCastWalk walk = AvoidCastWalk::None;
    for (int i = 0; i < count && walk != AvoidCastWalk::Started; ++i)
    {
        // A cast of the bot's own would hold it in the blast. MoveTo breaks a running cast itself once it launches the
        // walk (MoveTo2); what is left (a wand's auto-repeat) is stopped only once a walk is under way, so a bot with
        // no way out keeps its heal or bolt.
        bool const casting = bot->IsNonMeleeSpellCasted(true);
        if (!MoveTo(bot->GetMapId(), spots[i].x, spots[i].y, spots[i].z, false, false, false, true))
            continue;
        // MoveTo can say yes without a walk (a collapsed path rebuilt next tick, a transport or teleport wait): only a
        // walk under way counts; otherwise the next point is tried.
        if (!WalkUnderWay(bot))
        {
            walk = AvoidCastWalk::Deferred;
            continue;
        }
        walk = AvoidCastWalk::Started;
        if (casting)
        {
            if (bot->IsNonMeleeSpellCasted(true))
                bot->CastStop();
            stopped = true;
        }
        if (logIt)
            destDistance = CenterDistance(caster, spots[i].x, spots[i].y, spots[i].z);
    }
    if (logIt)
    {
        // The slot the row matched, the same pick the walk-out's radius used (ListedCastRadius).
        Spell* const spell = ListedCastSpell(fight.rows, caster);
        LogAvoidCast(ai, bot, caster, spell, spell && spell->m_spellInfo ? spell->m_spellInfo->Id : 0, danger.radius,
                     distBefore, destDistance, stopped, walk);
    }
    return walk == AvoidCastWalk::Started;
}

bool BossTacticsStepBehindAction::isPossible()
{
    return MovementAction::isPossible() && ai->CanMove();
}

bool BossTacticsStepBehindAction::Execute(Event& event)
{
    (void)event;
    Unit* boss = FindBossFrontDanger(ai, bot, ScanBossTactics(ai, bot));
    if (!boss)
        return false;
    float x = 0.0f, y = 0.0f;
    BossFlankDestination(boss->GetPositionX(), boss->GetPositionY(), boss->GetOrientation(),
                         bot->GetPositionX(), bot->GetPositionY(), 3.0f, x, y);
    float z = boss->GetPositionZ();
    bot->UpdateGroundPositionZ(x, y, z);
    return MoveTo(bot->GetMapId(), x, y, z);
}

namespace
{
// What each bot remembers of the window it was seated in (BossStandPolicy.h StandHold), by guid. A mutex guards it:
// the plan is asked for from the trigger, the action, the reach and the dodge of every bot.
std::mutex g_standHoldMutex;
std::unordered_map<uint64, StandHold> g_standHolds;
// Seats the mover refused, by bot guid (StandFailure): three in a row for one seat and the bot gets no seat.
std::unordered_map<uint64, StandFailure> g_standFailures;
// Entries in the two maps: a bot with nothing to forget costs one atomic read, not a lock.
std::atomic<size_t> g_standStateCount{0};

// The seat plan of an open window (PlanStandSeats, then grown by ExtendStandPlan), by boss instance (BossStandPolicy.h
// StandPlanKey), anchor and role: built by the first bot that sees the window and read by every bot of the role, so all
// of them walk to one plan and nobody's seat flips while bots move. A plan nobody read for kBossStandPlanIdleMs is
// stale: the sweep below drops it, whatever its key, and the next window builds a new one. Guarded by g_standHoldMutex.
struct StandPlan
{
    std::vector<StandSeat> seats;
    uint32 lastReadMs = 0;
};
std::unordered_map<StandPlanKey, StandPlan, StandPlanKeyHash> g_standPlans;

// Drops what is remembered of a bot (it died, left, or is on a map without stand rows).
void ForgetStand(uint64 key)
{
    if (g_standStateCount.load(std::memory_order_relaxed) == 0)
        return;
    std::lock_guard<std::mutex> lock(g_standHoldMutex);
    g_standHolds.erase(key);
    g_standFailures.erase(key);
    g_standStateCount.store(g_standHolds.size() + g_standFailures.size(), std::memory_order_relaxed);
}
}

BossStandTarget ai::FindBossStand(PlayerbotAI* ai, Player* bot)
{
    BossStandTarget target;
    if (!ai || !bot)
        return target;
    const uint64 selfKey = bot->GetObjectGuid().GetRawValue();
    // A bot that is dead, gone or teleporting keeps nothing of its last window.
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
    {
        ForgetStand(selfKey);
        return target;
    }
    // A map without rows costs one lookup and nothing else.
    const BossStandTable& table = GetBossStands();
    const uint32 mapId = bot->GetMapId();
    if (!table.HasMap(mapId))
    {
        ForgetStand(selfKey);
        return target;
    }
    // The rows only act for a bot whose tactics are armed (the same gate as BossTacticsTriggers.cpp Ready).
    if (!ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT))
    {
        ForgetStand(selfKey);
        return target;
    }

    const StandRole role = StandRoleOf(PlayerbotAI::IsTank(bot), PlayerbotAI::IsHeal(bot), ai->IsRanged(bot));

    AiObjectContext* context = ai->GetAiObjectContext();
    const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    const uint32 nowMs = WorldTimer::getMSTime();
    StandHold hold;
    {
        std::lock_guard<std::mutex> lock(g_standHoldMutex);
        auto it = g_standHolds.find(selfKey);
        if (it != g_standHolds.end())
            hold = it->second;
    }
    // What the other bots can see of this one: its hold as of the last tick, before this tick arms it again.
    const StandHold heldBefore = hold;
    bool sawBoss = false;
    Unit* boss = nullptr;
    const std::vector<BossStandRow>* bossRows = nullptr;
    std::vector<const BossStandRow*> points;
    for (const ObjectGuid& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !sServerFacade.IsAlive(unit))
            continue;
        // While a hold is active only its own boss is read: another boss with rows must not clear it.
        if (hold.armed && hold.bossEntry != 0 && unit->GetEntry() != hold.bossEntry)
            continue;
        const std::vector<BossStandRow>* rows = table.Rows(mapId, unit->GetEntry());
        if (!rows)
            continue;
        // The spell the boss is casting now (a cast with a cast time, as FindBossInterrupt reads it).
        uint32 casting = 0;
        if (Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        {
            if (spell->m_spellInfo)
                casting = spell->m_spellInfo->Id;
        }
        // A spell cast triggered (a breath) never shows as the current spell: the state the cast leaves opens the
        // window of a row whose signal says so (the boss stands still and has no target; the row also names the aura
        // it has to wear and the anchor it has to be at).
        const bool stoppedNoTarget = unit->IsStopped() && unit->GetTargetGuid().IsEmpty();
        // The open window's points; after the cast the same seats for up to the row's hold time, until the boss
        // is at the release anchor (StandPointsNow arms, keeps and clears the hold).
        sawBoss = true;
        points = StandPointsNow(*rows, role, unit->GetPositionX(), unit->GetPositionY(), casting,
                                [unit](uint32 aura) { return unit->HasAura(aura); }, hold, nowMs, stoppedNoTarget);
        if (!points.empty())
        {
            boss = unit;
            bossRows = rows;
            break;
        }
    }

    // The bots that share the seats: this group's bots of the same role that take part, by one rule for every bot that
    // evaluates the plan (StandMemberCounts: in the fight with THIS boss within range of the anchor, or already
    // holding a stand for this boss and anchor), so all of them compute the same plan. A bot elsewhere in the
    // instance, or one the boss is not fighting, reserves no seat.
    bool selfCounts = false;
    std::vector<StandBot> bots;
    std::vector<std::uint64_t> dead;  // group members that died: their plan entry frees its spot for the living
    const ObjectGuid selfGuid = bot->GetObjectGuid();
    if (boss)
    {
        const float anchorX = points[0]->anchorX;
        const float anchorY = points[0]->anchorY;
        const uint32 bossEntry = boss->GetEntry();
        const uint32 anchor = points[0]->anchor;
        const auto candidateOf = [&](Player* member, const StandHold& memberHold)
        {
            StandCandidate candidate;
            candidate.id = member->GetObjectGuid().GetRawValue();
            candidate.x = member->GetPositionX();
            candidate.y = member->GetPositionY();
            // On the boss's threat list (as AttackersValue.cpp:367 reads it), or the boss's victim.
            candidate.engaged = boss->GetThreatManager().getThreat(member) > 0.0f || boss->GetVictim() == member;
            candidate.holdsSameAnchor = memberHold.armed && memberHold.bossEntry == bossEntry && memberHold.anchor == anchor;
            return candidate;
        };
        std::vector<StandCandidate> candidates;
        const StandCandidate self = candidateOf(bot, heldBefore);
        selfCounts = StandMemberCounts(self, anchorX, anchorY);
        candidates.push_back(self);
        if (Group* group = bot->GetGroup())
        {
            for (Player* member : LiveGroupMembers(group))
            {
                if (member == bot || !member->IsInWorld() || member->GetMapId() != mapId)
                    continue;
                // A member that died frees the seat its plan entry holds; one that only left the fight keeps it.
                if (!sServerFacade.IsAlive(member))
                {
                    dead.push_back(member->GetObjectGuid().GetRawValue());
                    continue;
                }
                PlayerbotAI* memberAi = PlayerbotAIStorage::Instance().GetAI(member);
                if (!memberAi || !memberAi->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT))
                    continue;
                if (StandRoleOf(PlayerbotAI::IsTank(member), PlayerbotAI::IsHeal(member), ai->IsRanged(member)) != role)
                    continue;
                StandHold memberHold;
                {
                    std::lock_guard<std::mutex> lock(g_standHoldMutex);
                    auto it = g_standHolds.find(member->GetObjectGuid().GetRawValue());
                    if (it != g_standHolds.end())
                        memberHold = it->second;
                }
                candidates.push_back(candidateOf(member, memberHold));
            }
        }
        bots = SelectStandBots(candidates, anchorX, anchorY);
    }

    {
        std::lock_guard<std::mutex> lock(g_standHoldMutex);
        // The hold is kept for a bot that takes part (it is what the others read); a bot that does not count leaves
        // none behind. A tick that sees no boss with rows (it dropped off the attacker list for a moment) keeps the
        // hold until its time is up; StandPointsNow clears it itself when a boss is seen and the hold is over.
        const bool kept = hold.armed &&
            (boss ? selfCounts : (sawBoss || static_cast<uint32>(nowMs - hold.lastOpenMs) < hold.holdMs));
        if (kept)
            g_standHolds[selfKey] = hold;
        else
        {
            g_standHolds.erase(selfKey);
            g_standFailures.erase(selfKey);  // the window is over: the next one starts afresh
        }
        g_standStateCount.store(g_standHolds.size() + g_standFailures.size(), std::memory_order_relaxed);
    }
    if (points.empty())
        return target;

    // The side is the side of the lane (anchor -> release anchor) the bot stands on. The seats come from the window's
    // one plan (PlanStandSeats), built when the window opens, grown by the bots that join the fight later and by the
    // seat a dead bot frees; a seat once given never moves while its bot lives.
    const StandLane lane = bossRows ? StandLaneOf(*bossRows, *points[0]) : StandLane();
    StandSeat seat;
    {
        std::lock_guard<std::mutex> lock(g_standHoldMutex);
        Group* group = bot->GetGroup();
        // a bot with no group plans alone: its own guid keeps two groupless bots from sharing one plan
        const uint64 groupGuid = group ? group->GetLeaderGuid().GetRawValue() : bot->GetObjectGuid().GetRawValue();
        const StandPlanKey key{ boss->GetObjectGuid().GetRawValue(), boss->GetMapId(), boss->GetInstanceId(),
                                points[0]->anchor, static_cast<uint8>(role), points[0]->windowSpell, groupGuid };
        auto it = g_standPlans.find(key);
        if (it == g_standPlans.end() || StandPlanStale(nowMs, it->second.lastReadMs))
        {
            for (auto old = g_standPlans.begin(); old != g_standPlans.end();)
                old = StandPlanStale(nowMs, old->second.lastReadMs) ? g_standPlans.erase(old) : std::next(old);
            StandPlan plan;
            plan.seats = PlanStandSeats(points, bots, lane);
            it = g_standPlans.insert_or_assign(key, std::move(plan)).first;
        }
        else
            // a bot engaged after the window opened, or one whose seat a death freed, gets a free seat
            ExtendStandPlan(it->second.seats, points, bots, lane, dead);
        it->second.lastReadMs = nowMs;
        for (const StandSeat& planned : it->second.seats)
        {
            if (planned.id == selfGuid.GetRawValue())
            {
                seat = planned;
                break;
            }
        }
    }
    // A bot the plan did not seat (its side was full, or it was not in the fight when the window opened) keeps the
    // reactive dodge.
    if (!seat.valid)
        return target;
    // A seat the mover refused three times in a row is given up on: the reach and the dodge take over.
    {
        std::lock_guard<std::mutex> lock(g_standHoldMutex);
        auto failure = g_standFailures.find(selfKey);
        if (failure != g_standFailures.end() && StandSeatGivenUp(failure->second, seat.x, seat.y))
            return target;
    }
    target.valid = true;
    target.x = seat.x;
    target.y = seat.y;
    target.z = seat.z;
    return target;
}

bool BossTacticsStandAction::isPossible()
{
    return MovementAction::isPossible() && ai->CanMove();
}

bool BossTacticsStandAction::Execute(Event& event)
{
    (void)event;
    const BossStandTarget stand = FindBossStand(ai, bot);
    if (!stand.valid || StandArrived(bot->GetPositionX(), bot->GetPositionY(), stand.x, stand.y))
    {
        hasLast = false;
        return false;
    }
    // Already walking to this seat: leave the spline alone.
    if (hasLast && !bot->IsStopped() && !bot->movespline->Finalized() &&
        std::fabs(lastX - stand.x) < 1.0f && std::fabs(lastY - stand.y) < 1.0f)
        return true;

    // The mover ignores a destination whose z is off the floor, so the seat is put back on the ground. A far seat's
    // floor can lie above the row's z, so the probe starts above both the row's and the bot's own; a probe that
    // comes back unchanged found nothing and the row's z stays.
    const float probeZ = StandProbeZ(stand.z, bot->GetPositionZ());
    float z = probeZ;
    bot->UpdateGroundPositionZ(stand.x, stand.y, z);
    z = StandSnappedZ(stand.z, probeZ, z);
    bot->AttackStop();
    // The cast of a bot on its way out is of no use: stop it, as the reactive dodge does - unless it is already close
    // to its seat, where a cast in progress is let finish.
    if (StandShouldStopCast(bot->GetPositionX(), bot->GetPositionY(), stand.x, stand.y))
        bot->CastStop();
    const uint64 key = bot->GetObjectGuid().GetRawValue();
    if (!MoveTo(bot->GetMapId(), stand.x, stand.y, z, false, false, false, true))
    {
        // A seat the mover keeps refusing is given up on after three tries (FindBossStand returns invalid for it).
        std::lock_guard<std::mutex> lock(g_standHoldMutex);
        StandNoteMoveFailure(g_standFailures[key], stand.x, stand.y);
        g_standStateCount.store(g_standHolds.size() + g_standFailures.size(), std::memory_order_relaxed);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_standHoldMutex);
        auto failure = g_standFailures.find(key);
        if (failure != g_standFailures.end())
        {
            g_standFailures.erase(failure);
            g_standStateCount.store(g_standHolds.size() + g_standFailures.size(), std::memory_order_relaxed);
        }
    }
    lastX = stand.x;
    lastY = stand.y;
    hasLast = true;
    return true;
}

namespace
{
// The use_object record: one per (map instance, boss, row), shared by every bot of the instance (the g_standHolds
// pattern). It holds the one clicker claim, the click clock and the per-object attempts, so every bot's decision is
// made from the same state. A mutex guards it: the trigger and the action of every bot reach it.
struct UseObjectRecord
{
    std::uint32_t firstSeenMs = 0;  // the row's timer starts here before the first click
    std::uint32_t lastSeenMs = 0;
    std::uint32_t lastClickMs = 0;
    bool everClicked = false;
    std::uint64_t clicker = 0;  // 0: unclaimed
    std::uint32_t clickerSinceMs = 0;
    bool claimLogged = false;
    UseObjectAttempts attempts;
    std::uint64_t objectGuid = 0;
    std::uint32_t objectScanMs = 0;
    // The anchor walk's episode: the moment it started and how far the boss was, so one walk logs one pair of lines.
    std::uint32_t anchorStartMs = 0;
    float anchorFromYd = 0.0f;
    bool anchorDone = false;  // this cycle's walk is over (arrived), no second one until the row is due again
};

std::mutex g_useObjectMutex;
std::map<std::tuple<std::uint32_t, std::uint64_t, const BossTacticRow*>, UseObjectRecord> g_useObjectRecords;

// The tank's walk point is close enough: the anchor is over and the tank goes back to the boss.
const float kUseObjectArriveYd = 3.0f;
// The object search around the boss: a used crystal's replacement spawns 40-80 yd from it.
const float kUseObjectSearchYd = 100.0f;
// The record's object is searched at most this often: one grid walk per record per second, never per bot per tick.
const std::uint32_t kUseObjectObjectScanMs = 1000;
// A record nobody evaluated for this long is dropped (the fight is over or the boss left).
const std::uint32_t kUseObjectRecordIdleMs = 90000;
const std::uint32_t kUseObjectSkipLogGapMs = 5000;

using UseObjectKey = std::tuple<std::uint32_t, std::uint64_t, const BossTacticRow*>;

UseObjectKey UseObjectRecordKey(Unit* boss, const BossTacticRow* row)
{
    return UseObjectKey(boss->GetInstanceId(), boss->GetObjectGuid().GetRawValue(), row);
}

// Drops the records of fights that stopped being evaluated. Called with g_useObjectMutex held; the sweep itself runs
// at most once per idle window.
void SweepUseObjectRecords(std::uint32_t nowMs)
{
    static std::uint32_t lastSweepMs = 0;
    if (lastSweepMs != 0 && nowMs - lastSweepMs < kUseObjectRecordIdleMs)
        return;
    lastSweepMs = nowMs;
    for (auto it = g_useObjectRecords.begin(); it != g_useObjectRecords.end();)
        it = nowMs - it->second.lastSeenMs > kUseObjectRecordIdleMs ? g_useObjectRecords.erase(it) : std::next(it);
}

// The record's object: the ready object of the row's entry nearest the boss (not the bot: both the clicker and the
// anchoring tank go where the boss is), cached for a second. An object the record spent its two attempts on is skipped
// so the next one of the pair takes over.
GameObject* ReadyUseObject(PlayerbotAI* ai, UseObjectRecord& record, Unit* boss, const BossTacticRow& row,
                           std::uint32_t nowMs)
{
    const auto ready = [&record](GameObject* go)
    {
        return go && sServerFacade.isSpawned(go) && go->GetGoState() == GO_STATE_READY &&
            !go->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_IN_USE) && !go->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT) &&
            record.attempts.AttemptsFor(go->getObjectGuid().GetRawValue()) < kUseObjectAttemptsPerObject;
    };
    if (record.objectGuid != 0 && nowMs - record.objectScanMs < kUseObjectObjectScanMs)
    {
        GameObject* cached = ai->GetGameObject(ObjectGuid(record.objectGuid));
        if (ready(cached))
            return cached;
        // Used or gone: look again now instead of after the cache's second.
        record.objectScanMs = 0;
    }
    std::list<GameObject*> objects;
    boss->GetGameObjectListWithEntryInGrid(objects, row.targetEntry, kUseObjectSearchYd);
    GameObject* best = nullptr;
    float bestDistance = 0.0f;
    for (GameObject* go : objects)
    {
        if (!ready(go))
            continue;
        const float distance = boss->GetDistance3dToCenter(go);
        if (best && (distance > bestDistance || (distance == bestDistance &&
                go->getObjectGuid().GetRawValue() > best->getObjectGuid().GetRawValue())))
            continue;
        best = go;
        bestDistance = distance;
    }
    record.objectGuid = best ? best->getObjectGuid().GetRawValue() : 0;
    record.objectScanMs = nowMs;
    return best;
}

struct UseObjectEval
{
    GameObject* object = nullptr;
    float bossToObject = 0.0f;
    bool bossMoving = false;
    bool due = false;
    std::uint32_t msToDue = 0;
    std::uint32_t sinceMs = 0;
};

// The row's state for its record: the object, the boss's range to it and the clock (UseObjectPolicy.h owns the rules).
UseObjectEval EvaluateUseObject(PlayerbotAI* ai, UseObjectRecord& record, Unit* boss, const BossTacticRow& row,
                                std::uint32_t nowMs)
{
    UseObjectEval eval;
    const bool bossHasAura = row.spellId != 0 && boss->HasAura(row.spellId);
    const std::uint32_t sinceClickMs = record.everClicked ? nowMs - record.lastClickMs : 0;
    const std::uint32_t sinceFirstSeenMs = nowMs - record.firstSeenMs;
    eval.sinceMs = record.everClicked ? sinceClickMs : sinceFirstSeenMs;
    eval.due = UseObjectDue(row.spellId != 0, bossHasAura, row.windowSec, record.everClicked, sinceClickMs,
                            sinceFirstSeenMs);
    eval.msToDue = UseObjectMsToDue(row.spellId != 0, bossHasAura, row.windowSec, record.everClicked, sinceClickMs,
                                    sinceFirstSeenMs);
    eval.object = ReadyUseObject(ai, record, boss, row, nowMs);
    if (eval.object)
    {
        eval.bossToObject = boss->GetDistance3dToCenter(eval.object);
        // The weakness lands 5 s after the click, so the boss has to be still now (the click gate's rule).
        eval.bossMoving = sServerFacade.isMoving(boss) || !boss->movespline->Finalized();
    }
    return eval;
}

// The clicker roles the row's `priority` names: 0 free DPS, 1 healer, 2 any non-tank.
bool UseObjectRoleAllows(Player* member, std::uint8_t priority)
{
    const bool tank = PlayerbotAI::IsTank(member);
    const bool healer = PlayerbotAI::IsHeal(member);
    if (priority == 0)
        return !tank && !healer;
    if (priority == 1)
        return healer;
    return !tank;
}

// A bot that may take the click: in the group, alive, on the boss's map and instance, armed with the tactics, of the
// row's role, able to move, in control and not on an explicit stay. The row's `bot_aura` gate is a fact every bot can
// see, so the pick stays the same in every bot's evaluation.
bool UseObjectCanClick(PlayerbotAI* self, Player* bot, Player* member, Unit* boss, const BossTacticRow& row)
{
    if (!member || !member->IsInWorld() || member->IsBeingTeleported() || !sServerFacade.IsAlive(member))
        return false;
    if (member->GetMapId() != boss->GetMapId() || member->GetInstanceId() != boss->GetInstanceId())
        return false;
    PlayerbotAI* memberAi = member == bot ? self : PlayerbotAIStorage::Instance().GetAI(member);
    if (!memberAi || !memberAi->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT))
        return false;
    if (!UseObjectRoleAllows(member, row.priority))
        return false;
    if (member->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL) || !memberAi->CanMove())
        return false;
    if (memberAi->HasStrategy("stay", BotState::BOT_STATE_COMBAT))
        return false;
    return row.botAura == 0 || member->HasAura(row.botAura);
}

// The group roster as the pick sees it, this bot included: 2D distance to the object and the eligibility gates.
std::vector<UseObjectCandidate> UseObjectCandidates(PlayerbotAI* ai, Player* bot, Unit* boss, GameObject* object,
                                                    const BossTacticRow& row)
{
    std::vector<UseObjectCandidate> candidates;
    const auto add = [&](Player* member)
    {
        UseObjectCandidate candidate;
        candidate.guid = member->GetObjectGuid().GetRawValue();
        candidate.distanceYd = member->GetDistance2dToCenter(object);
        candidate.eligible = UseObjectCanClick(ai, bot, member, boss, row);
        candidates.push_back(candidate);
    };
    if (Group* group = bot->GetGroup())
    {
        for (Player* member : LiveGroupMembers(group))
            if (member)
                add(member);
    }
    else
        add(bot);
    return candidates;
}

std::mutex g_useObjectLogMutex;
std::unordered_map<std::uint64_t, std::uint32_t> g_useObjectSkipLog;

// The clicker's decisions, one bot_events.csv row each: claim, walk, click, and the skip (throttled to one per 5 s per
// bot). A no-op unless bot_events.csv is in AiPlayerbot.AllowedLogFiles, like the hold-kill and avoid-cast rows; the
// throttle is checked before any string is built.
void LogUseObject(PlayerbotAI* ai, Player* bot, const char* phase, const UseObjectTask& task, std::uint32_t nowMs,
                  std::uint32_t tries, const char* skip)
{
    if (!sPlayerbotAIConfig.hasLog("bot_events.csv"))
        return;
    if (skip)
    {
        std::lock_guard<std::mutex> lock(g_useObjectLogMutex);
        std::uint32_t& last = g_useObjectSkipLog[bot->GetObjectGuid().GetRawValue()];
        if (last != 0 && nowMs - last < kUseObjectSkipLogGapMs)
            return;
        last = nowMs;
    }
    const bool auraDue = task.row && task.row->spellId != 0 && task.boss && task.boss->HasAura(task.row->spellId);
    char info1[192];
    if (skip)
        snprintf(info1, sizeof(info1), "phase=%s,go=%u,boss=%u,boss_yd=%.1f,due=%s,since_ms=%u,try=%u,reason=%s", phase,
                 task.row ? task.row->targetEntry : 0, task.row ? task.row->bossEntry : 0, task.bossToObject,
                 auraDue ? "aura" : "timer", task.sinceMs, tries, skip);
    else
        snprintf(info1, sizeof(info1), "phase=%s,go=%u,boss=%u,boss_yd=%.1f,due=%s,since_ms=%u,try=%u", phase,
                 task.row ? task.row->targetEntry : 0, task.row ? task.row->bossEntry : 0, task.bossToObject,
                 auraDue ? "aura" : "timer", task.sinceMs, tries);
    sPlayerbotAIConfig.logEvent(ai, "BossTacticsUseObject", info1, "");
}

// The anchor walk's two lines (start and arrival), from the finder both the trigger and the action call.
void LogUseObjectAnchor(PlayerbotAI* ai, const BossTacticRow& row, float fromYd, bool arrived,
                        std::uint32_t arrivedMs)
{
    if (!sPlayerbotAIConfig.hasLog("bot_events.csv"))
        return;
    char info1[128];
    if (arrived)
        snprintf(info1, sizeof(info1), "boss=%u,go=%u,from_yd=%.1f,arrived_ms=%u", row.bossEntry, row.targetEntry,
                 fromYd, arrivedMs);
    else
        snprintf(info1, sizeof(info1), "boss=%u,go=%u,from_yd=%.1f,arrived_ms=none", row.bossEntry, row.targetEntry,
                 fromYd);
    sPlayerbotAIConfig.logEvent(ai, "BossTacticsAnchor", info1, "");
}

// Takes the record's next attempt for the claimed clicker: at most two per object, 8 s apart, and a taken attempt
// restarts the row's timer and frees the claim, so the next cycle picks again.
bool TakeUseObjectAttempt(Player* bot, const UseObjectTask& task, std::uint32_t nowMs, std::uint32_t& tries)
{
    if (!task.object || !task.boss || !task.row)
        return false;
    const std::uint64_t objectGuid = task.object->getObjectGuid().GetRawValue();
    std::lock_guard<std::mutex> lock(g_useObjectMutex);
    auto it = g_useObjectRecords.find(UseObjectRecordKey(task.boss, task.row));
    if (it == g_useObjectRecords.end())
        return false;
    UseObjectRecord& record = it->second;
    if (!record.attempts.Take(objectGuid, bot->GetObjectGuid().GetRawValue(), record.clicker, nowMs))
        return false;
    record.lastClickMs = nowMs;
    record.everClicked = true;
    record.clicker = 0;
    record.clickerSinceMs = nowMs;
    record.claimLogged = false;
    tries = record.attempts.AttemptsFor(objectGuid);
    return true;
}

// The boss a use_object row is about, among the fight's units: alive (the scan's list) and fighting.
Unit* UseObjectBoss(const BossTacticsFight& fight, const BossTacticRow& row)
{
    for (Unit* unit : fight.units)
    {
        if (unit->GetEntry() == row.bossEntry && unit->IsInCombat())
            return unit;
    }
    return nullptr;
}
}  // namespace

UseObjectTask ai::FindBossUseObject(PlayerbotAI* ai, Player* bot)
{
    UseObjectTask task;
    if (!ai || !bot || !sPlayerbotAIConfig.dungeonRunUseObject)
        return task;
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
        return task;
    if (!ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT) || !ai->CanMove())
        return task;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    const std::uint64_t self = bot->GetObjectGuid().GetRawValue();
    const std::uint32_t nowMs = WorldTimer::getMSTime();
    for (const BossTacticRow* row : fight.rows)
    {
        if (row->kind != BossTacticKind::UseObject)
            continue;
        Unit* boss = UseObjectBoss(fight, *row);
        if (!boss)
            continue;
        std::lock_guard<std::mutex> lock(g_useObjectMutex);
        SweepUseObjectRecords(nowMs);
        UseObjectRecord& record = g_useObjectRecords[UseObjectRecordKey(boss, row)];
        if (record.firstSeenMs == 0)
            record.firstSeenMs = nowMs;
        record.lastSeenMs = nowMs;
        const UseObjectEval eval = EvaluateUseObject(ai, record, boss, *row, nowMs);
        if (eval.object)
        {
            const std::vector<UseObjectCandidate> candidates = UseObjectCandidates(ai, bot, boss, eval.object, *row);
            const int pick = PickUseObjectClicker(candidates);
            const std::uint64_t pickGuid = pick < 0 ? 0 : candidates[pick].guid;
            // A claim nobody acted on for 30 s is given up so the roster is looked at again (a path the mover cannot
            // find, a bot that died mid-walk).
            const bool lapsed = record.clicker != 0 && nowMs - record.clickerSinceMs > kUseObjectClaimTimeoutMs;
            if (pickGuid != record.clicker)
            {
                record.clicker = pickGuid;
                record.clickerSinceMs = nowMs;
                record.claimLogged = false;
            }
            else if (lapsed)
            {
                record.clickerSinceMs = nowMs;
                record.claimLogged = false;
            }
        }
        if (record.clicker != self)
            continue;
        if (!(eval.due || eval.msToDue <= kUseObjectLeadMs))
            continue;
        task.valid = true;
        task.logClaim = !record.claimLogged;
        task.boss = boss;
        task.object = eval.object;
        task.row = row;
        task.due = eval.due;
        task.bossMoving = eval.bossMoving;
        task.bossToObject = eval.bossToObject;
        task.msToDue = eval.msToDue;
        task.sinceMs = eval.sinceMs;
        task.attemptReady = eval.object && record.attempts.Ready(record.objectGuid, nowMs);
        return task;
    }
    return task;
}

void ai::NoteBossUseObjectClaimLogged(Unit* boss, const BossTacticRow* row)
{
    if (!boss || !row)
        return;
    std::lock_guard<std::mutex> lock(g_useObjectMutex);
    auto it = g_useObjectRecords.find(UseObjectRecordKey(boss, row));
    if (it != g_useObjectRecords.end())
        it->second.claimLogged = true;
}

UseObjectAnchor ai::FindBossUseObjectAnchor(PlayerbotAI* ai, Player* bot)
{
    UseObjectAnchor anchor;
    if (!ai || !bot || !sPlayerbotAIConfig.dungeonRunUseObject)
        return anchor;
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
        return anchor;
    if (!PlayerbotAI::IsTank(bot) || !ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT) || !ai->CanMove())
        return anchor;
    if (bot->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL))
        return anchor;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    const std::uint32_t nowMs = WorldTimer::getMSTime();
    for (const BossTacticRow* row : fight.rows)
    {
        if (row->kind != BossTacticKind::UseObject || row->mode != TacticMode::TankAnchor)
            continue;
        Unit* boss = UseObjectBoss(fight, *row);
        // Only the tank the boss is on moves it: while Enveloping Winds dropped its threat the boss is on someone else
        // and the anchor waits (the timer has the margin).
        if (!boss || boss->GetVictim() != bot)
            continue;
        bool want = false;
        bool started = false;
        bool arrived = false;
        std::uint32_t arrivedMs = 0;
        float fromYd = 0.0f;
        UseObjectEval eval;
        float x = 0.0f;
        float y = 0.0f;
        float tankDistance = 0.0f;
        {
            std::lock_guard<std::mutex> lock(g_useObjectMutex);
            SweepUseObjectRecords(nowMs);
            UseObjectRecord& record = g_useObjectRecords[UseObjectRecordKey(boss, row)];
            if (record.firstSeenMs == 0)
                record.firstSeenMs = nowMs;
            record.lastSeenMs = nowMs;
            eval = EvaluateUseObject(ai, record, boss, *row, nowMs);
            if (!eval.object || !TankAnchorWanted(true, eval.msToDue, eval.bossToObject, row->param))
            {
                // The row is not asking for the walk: the next cycle starts a new episode.
                record.anchorStartMs = 0;
                record.anchorDone = false;
            }
            else
            {
                TankAnchorPoint(eval.object->GetPositionX(), eval.object->GetPositionY(), bot->GetPositionX(),
                                bot->GetPositionY(), row->param * 0.4f, x, y);
                tankDistance = bot->GetDistance2dToCenter(x, y);
                if (tankDistance <= kUseObjectArriveYd)
                {
                    if (record.anchorStartMs != 0)
                    {
                        arrived = true;
                        arrivedMs = nowMs - record.anchorStartMs;
                        fromYd = record.anchorFromYd;
                        record.anchorStartMs = 0;
                        record.anchorDone = true;
                    }
                }
                else if (!record.anchorDone)
                {
                    if (record.anchorStartMs == 0)
                    {
                        record.anchorStartMs = nowMs;
                        record.anchorFromYd = eval.bossToObject;
                        started = true;
                    }
                    want = true;
                }
            }
        }
        if (started)
            LogUseObjectAnchor(ai, *row, eval.bossToObject, false, 0);
        if (arrived)
            LogUseObjectAnchor(ai, *row, fromYd, true, arrivedMs);
        if (!want)
            continue;
        anchor.valid = true;
        anchor.boss = boss;
        anchor.object = eval.object;
        anchor.row = row;
        anchor.bossToObject = eval.bossToObject;
        anchor.tankDistance = tankDistance;
        anchor.x = x;
        anchor.y = y;
        return anchor;
    }
    return anchor;
}

bool BossTacticsUseObjectAction::isPossible()
{
    return MovementAction::isPossible() && ai->CanMove();
}

bool BossTacticsUseObjectAction::Execute(Event& event)
{
    (void)event;
    const UseObjectTask task = FindBossUseObject(ai, bot);
    if (!task.valid)
    {
        hasLast = false;
        return false;
    }
    const std::uint32_t nowMs = WorldTimer::getMSTime();
    const bool logIt = sPlayerbotAIConfig.hasLog("bot_events.csv");
    if (logIt && task.logClaim)
    {
        LogUseObject(ai, bot, "claim", task, nowMs, 0, nullptr);
        NoteBossUseObjectClaimLogged(task.boss, task.row);
    }
    if (!task.object)
    {
        // The pair is spent or between spawns: nothing to click this tick.
        if (logIt)
            LogUseObject(ai, bot, "skip", task, nowMs, 0, "no_object");
        return false;
    }
    if (bot->GetDistance(task.object) > INTERACTION_DISTANCE - 1.0f)
    {
        const float x = task.object->GetPositionX();
        const float y = task.object->GetPositionY();
        // Already walking to this object: leave the spline alone.
        if (hasLast && !bot->IsStopped() && !bot->movespline->Finalized() && std::fabs(lastX - x) < 1.0f &&
            std::fabs(lastY - y) < 1.0f)
            return true;
        const float z = task.object->GetPositionZ();
        if (logIt)
            LogUseObject(ai, bot, "walk", task, nowMs, 0, nullptr);
        if (!MoveTo(bot->GetMapId(), x, y, z, false, false, false, true))
            return false;
        lastX = x;
        lastY = y;
        hasLast = true;
        return true;
    }
    hasLast = false;
    // At the object: hold until the click is allowed (returning true keeps combat moves from dragging the bot back).
    ai->StopMoving();
    if (!UseObjectClickReady(task.due, task.bossToObject, task.row->param, task.bossMoving, task.attemptReady))
    {
        if (logIt)
        {
            const char* reason = !task.due || !task.attemptReady ? "window" : (task.bossMoving ? "moving" : "far");
            LogUseObject(ai, bot, "skip", task, nowMs, 0, reason);
        }
        return true;
    }
    std::uint32_t tries = 0;
    if (!TakeUseObjectAttempt(bot, task, nowMs, tries))
    {
        if (logIt)
            LogUseObject(ai, bot, "skip", task, nowMs, 0, "window");
        return true;
    }
    if (logIt)
        LogUseObject(ai, bot, "click", task, nowMs, tries, nullptr);
    // The core re-checks the use; the packet is the module's established path to a gameobject use.
    if (!bot->GetGameObjectIfCanInteractWith(task.object->getObjectGuid()))
        return false;
    std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_GAMEOBJ_USE));
    *packet << task.object->getObjectGuid();
    bot->GetSession()->QueuePacket(packet.release());
    return true;
}

bool BossTacticsObjectAnchorAction::isPossible()
{
    return MovementAction::isPossible() && ai->CanMove();
}

bool BossTacticsObjectAnchorAction::Execute(Event& event)
{
    (void)event;
    const UseObjectAnchor anchor = FindBossUseObjectAnchor(ai, bot);
    if (!anchor.valid)
        return false;
    // The walk point sits between the object and the tank; its z comes from the ground under it.
    float z = anchor.object->GetPositionZ();
    bot->UpdateGroundPositionZ(anchor.x, anchor.y, z);
    return MoveTo(bot->GetMapId(), anchor.x, anchor.y, z);
}
