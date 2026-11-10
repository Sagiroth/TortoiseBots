
#include "playerbot/playerbot.h"
#include "ThreatStrategy.h"
#include "playerbot/strategy/actions/FankrissFocusActions.h"
#include "playerbot/strategy/actions/BossTacticsActions.h"
#include "playerbot/strategy/values/ThreatValues.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/BossTacticsMgr.h"
#include "playerbot/strategy/actions/GenericActions.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
#include "playerbot/ThreatCapPolicy.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/BotActionLog.h"
#include "Spells/SpellMgr.h"
#include "../../../../runtime/CommandedGroupQuery.h"
#include "../../../../runtime/RemoteCommanderPolicy.h"

#include <cmath>
#include <cstdio>
#include <iterator>
#include <mutex>
#include <unordered_map>

using namespace ai;

namespace
{
// The white damage the threat multiplier exempts: the melee auto-attack, the wand's Shoot and the hunter's Auto Shot.
// The successor white stop has to see them (AiPlayerbot.DungeonRunThreatSuccessorStrict, ThreatCapPolicy.h).
bool AutoAttackAction(Action* action)
{
    return dynamic_cast<MeleeAction*>(action) || dynamic_cast<CastShootAction*>(action) ||
        action->getName() == "auto shot";
}

// One BossTacticsThreatReset row per bot per reset window: a walk reads whether the hold fired when it should. The key
// packs the reset and which of the three gap windows it is, so one reset can log up to three windows.
std::mutex g_threatResetLogMutex;
std::unordered_map<std::uint64_t, std::uint64_t> g_threatResetLog;

void LogThreatResetHold(PlayerbotAI* ai, Player* bot, std::uint8_t pct, float ref, std::uint32_t sinceMs,
                        std::uint32_t lastResetMs, std::uint32_t windowIndex)
{
    if (!sPlayerbotAIConfig.hasLog("bot_events.csv"))
        return;
    const std::uint64_t windowKey = (static_cast<std::uint64_t>(lastResetMs) << 8) | windowIndex;
    {
        std::lock_guard<std::mutex> lock(g_threatResetLogMutex);
        auto const it = g_threatResetLog.find(bot->GetObjectGuid().GetRawValue());
        if (it != g_threatResetLog.end() && it->second == windowKey)
            return;
        g_threatResetLog[bot->GetObjectGuid().GetRawValue()] = windowKey;
        if (g_threatResetLog.size() > 4096)
            g_threatResetLog.clear();
    }
    char info1[96];
    snprintf(info1, sizeof(info1), "reset hold: pct=%u ref=%.0f since_ms=%u", static_cast<unsigned>(pct), ref,
             sinceMs);
    sPlayerbotAIConfig.logEvent(ai, "BossTacticsThreatReset", info1, "");
}

// One row per bot and second while an interrupt is vetoed with AiPlayerbot.InterruptIgnoresThreat on: the bot's
// threat, the tank's, the interrupt's flat threat and the pull line it was measured against, so a live run shows why
// a Kick or Counterspell still waited (the engine's own `MULT threat x0.000` line carries no numbers).
constexpr uint32 kInterruptVetoLogGapMs = 1000;

std::mutex g_interruptVetoLogMutex;
std::unordered_map<uint64, uint32> g_interruptVetoLog;

void LogInterruptVeto(PlayerbotAI* ai, Player* bot, CastInterruptSpellAction const* action, float botThreat,
                      float tankThreat, float flatThreat, bool ranged)
{
    uint32 const nowMs = WorldTimer::getMSTime();
    {
        std::lock_guard<std::mutex> lock(g_interruptVetoLogMutex);
        uint32& last = g_interruptVetoLog[bot->GetObjectGuid().GetRawValue()];
        if (last != 0 && nowMs - last < kInterruptVetoLogGapMs)
            return;
        last = nowMs;
        if (g_interruptVetoLog.size() > 4096)
        {
            for (auto it = g_interruptVetoLog.begin(); it != g_interruptVetoLog.end();)
                it = nowMs - it->second > kInterruptVetoLogGapMs * 600 ? g_interruptVetoLog.erase(it) : std::next(it);
        }
    }
    char info[192];
    snprintf(info, sizeof(info), "ThreatCap interrupt spell=%s pct=%.0f flat=%.0f line=%.2f bot=%.0f tank=%.0f",
             action->GetSpellName().c_str(), ThreatCapPercentOf(botThreat + flatThreat, tankThreat), flatThreat,
             AggroPullFactor(ranged), botThreat, tankThreat);
    ai::botdiag::BotActionLog::Write(ai, "THREAT", "%s", info);
    if (sPlayerbotAIConfig.hasLog("bot_events.csv"))
        sPlayerbotAIConfig.logEvent(ai, "ThreatCapInterrupt", info, "");
}
}

bool ai::ThreatResetHolds(PlayerbotAI* ai, Player* bot, Unit* target)
{
    if (!sPlayerbotAIConfig.dungeonRunThreatResetWindow)
        return false;
    if (!target || target->IsPlayer() || !bot->GetGroup())
        return false;
    if (PlayerbotAI::IsTank(bot) || PlayerbotAI::IsHeal(bot))
        return false;
    // The row is read before the fight's seek snapshot: the snapshot costs a lock.
    const ThreatResetWindow* const win = GetBossTactics().ThreatResetWindowOf(target->GetMapId(), target->GetEntry());
    if (!win)
    {
        ai->ThreatResetCap().Update(false, false, 0.0f, 0.0f, 0);
        return false;
    }
    SeekCycleView view;
    if (!SeekCycleSnapshot(bot->GetInstanceId(), target->GetObjectGuid().GetRawValue(), view) || !view.valid)
    {
        ai->ThreatResetCap().Update(false, false, 0.0f, 0.0f, 0);
        return false;
    }
    const std::uint32_t nowMs = WorldTimer::getMSTime();
    const std::uint32_t sinceMs = ThreatResetSince(nowMs, view.lastResetMs, view.engageMs);
    const std::uint32_t windowIndex = ThreatResetWindowIndex(sinceMs, win->minGapMs, win->maxGapMs);
    // A shut window holds nothing: drop the latch before the tank list is built, so the common read costs no scan.
    if (windowIndex == 0)
    {
        ai->ThreatResetCap().Update(false, false, 0.0f, 0.0f, 0);
        return false;
    }
    const float botThreat = sServerFacade.GetThreatManager(target).getThreat(bot);
    const float ref = ThreatValue::GetResetSuccessorThreat(ai, target);
    const bool held = ai->ThreatResetCap().Update(true, true, botThreat, ref, win->stopPct);
    if (held)
        LogThreatResetHold(ai, bot, ThreatPctSaturated(botThreat, ref), ref, sinceMs, view.lastResetMs, windowIndex);
    return held;
}
// The threat one cast of `spellId` adds on `target` when it lands, as an average (AiPlayerbot.ThreatCapCountsCastThreat):
// for every direct school-damage effect of that rank, the middle of the core's damage roll with level growth
// (ThreatCapAverageEffectPoints, WorldObject::CalculateSpellDamage without the roll), the caster's all-effects spell
// mods and the level-damage attribute, then spell power and damage modifiers (SpellDamageBonusDone); on the sum the
// spell's threat mods and the caster's threat modifiers, as ThreatCalcHelper::CalcThreat applies them to the hit.
// Left out, so the estimate stays an average and on the low side: crits, the target's damage-taken modifiers, damage
// over time and the spell threat table. 0 for a spell without direct damage. The mod reads pass no Spell, so no spell
// mod charge (Clearcasting, Presence of Mind) is used up by the estimate (Player::DropModCharge).
float ExpectedCastThreat(Player* bot, Unit* target, uint32 spellId)
{
    SpellEntry const* info = spellId ? sServerFacade.LookupSpellInfo(spellId) : nullptr;
    if (!info || !target)
        return 0.0f;
    int32 level = int32(bot->GetLevel());
    if (level > int32(info->maxLevel) && info->maxLevel > 0)
        level = int32(info->maxLevel);
    else if (level < int32(info->baseLevel))
        level = int32(info->baseLevel);
    level -= int32(info->spellLevel);
    float damage = 0.0f;
    for (uint32 effect = 0; effect < MAX_EFFECT_INDEX; ++effect)
    {
        if (info->Effect[effect] != SPELL_EFFECT_SCHOOL_DAMAGE)
            continue;
        float points = ThreatCapAverageEffectPoints(info->EffectBasePoints[effect], int(info->EffectBaseDice[effect]),
                                                    info->EffectDieSides[effect], info->EffectRealPointsPerLevel[effect],
                                                    info->EffectDicePerLevel[effect], level);
        bot->ApplySpellMod(info->Id, SPELLMOD_ALL_EFFECTS, points);
        if ((info->Attributes & SPELL_ATTR_LEVEL_DAMAGE_CALCULATION) && info->spellLevel)
            points *= 0.25f * std::exp(bot->GetLevel() * (70 - int32(info->spellLevel)) / 1000.0f);
        if (points < 1.0f)
            continue;
        damage += float(bot->SpellDamageBonusDone(target, info, SpellEffectIndex(effect), uint32(points),
                                                  SPELL_DIRECT_DAMAGE));
    }
    if (damage <= 0.0f)
        return 0.0f;
    bot->ApplySpellMod(info->Id, SPELLMOD_THREAT, damage);
    return bot->ApplyTotalThreatModifier(damage, info->GetSpellSchoolMask());
}

// One row per bot and 2 s while a damage cast is held by AiPlayerbot.ThreatCapCountsCastThreat: the bot's threat, the
// cast's expected threat and the tank's, so a live run shows why a Fireball waited (the engine's own
// `MULT threat x0.000` line carries no numbers).
constexpr uint32 kCastVetoLogGapMs = 2000;

std::mutex g_castVetoLogMutex;
std::unordered_map<uint64, uint32> g_castVetoLog;

void LogCastVeto(PlayerbotAI* ai, Player* bot, CastSpellAction const* action, float botThreat, float castThreat,
                 float tankThreat)
{
    uint32 const nowMs = WorldTimer::getMSTime();
    {
        std::lock_guard<std::mutex> lock(g_castVetoLogMutex);
        uint32& last = g_castVetoLog[bot->GetObjectGuid().GetRawValue()];
        if (last != 0 && nowMs - last < kCastVetoLogGapMs)
            return;
        last = nowMs;
        if (g_castVetoLog.size() > 4096)
        {
            for (auto it = g_castVetoLog.begin(); it != g_castVetoLog.end();)
                it = nowMs - it->second > kCastVetoLogGapMs * 600 ? g_castVetoLog.erase(it) : std::next(it);
        }
    }
    char info[192];
    snprintf(info, sizeof(info), "ThreatCap cast spell=%s pct=%.0f cast=%.0f bot=%.0f tank=%.0f",
             action->GetSpellName().c_str(), (botThreat + castThreat) * 100.0f / tankThreat, castThreat, botThreat,
             tankThreat);
    ai::botdiag::BotActionLog::Write(ai, "THREAT", "%s", info);
    if (sPlayerbotAIConfig.hasLog("bot_events.csv"))
        sPlayerbotAIConfig.logEvent(ai, "ThreatCapCast", info, "");
}
}

float ThreatMultiplier::GetValue(Action* action)
{
    // Per-fight suppression (mod-playerbots "neglect threat"): a boss
    // fight strategy that owns positioning/targeting sets this while
    // engaged so threat gating does not veto its scripted target choice.
    if (AI_VALUE(bool, "neglect threat"))
        return 1.0f;

    if (action == NULL)
        return 1.0f;

    // AiPlayerbot.DungeonRunThreatResetWindow: around each expected Cause Insanity, a damage dealer at or above the
    // row's stop percent of the best tank that does not hold Hakkar stops every damage action on him, white damage
    // included, so the reset hands him to a tank. Heals and cures are never held, and an action aimed at another
    // target keeps its damage.
    if (sPlayerbotAIConfig.dungeonRunThreatResetWindow)
    {
        Unit* const resetTarget = AI_VALUE(Unit*, "current target");
        if (!ThreatCapExempt(dynamic_cast<CastHealingSpellAction*>(action) != nullptr,
                             dynamic_cast<CurePartyMemberAction*>(action) != nullptr,
                             sPlayerbotAIConfig.dungeonRunCureIgnoresThreat) &&
            (AutoAttackAction(action) || action->GetThreatType() != ActionThreatType::ACTION_THREAT_NONE) &&
            (!action->GetTarget() || action->GetTarget() == resetTarget) &&
            ThreatResetHolds(ai, bot, resetTarget))
            return 0.0f;
    }

    // AiPlayerbot.DungeonRunThreatSuccessorStrict: on a boss whose `threat_successor` row carries `strict` (a plain
    // successor row clears the latch and keeps the old exemption) the auto-attack, the wand's Shoot and
    // Auto Shot do not start again while this bot's white stop holds (92 % stop, 85 % resume; tanks never; a 0 reference
    // never stops them). The row decides before the percentage is read; a target without it clears the latch. Auto Shot
    // is SINGLE-threat, so the 80 % veto below still gates its restart. Key off: the old exemption below.
    if (sPlayerbotAIConfig.dungeonRunThreatSuccessorStrict && AutoAttackAction(action))
    {
        Unit* const target = AI_VALUE(Unit*, "current target");
        if (!StrictSuccessorTarget(sPlayerbotAIConfig.dungeonRunThreatSuccessorStrict, StrictThreatSuccessorRow(target)))
            ai->SuccessorWhiteCap().Update(false, false, 0);
        else if (ai->SuccessorWhiteCap().Update(true, PlayerbotAI::IsTank(bot),
                     AI_VALUE2(uint8, "threat", "current target"),
                     AI_VALUE2(float, "tank threat", "current target") == 0.0f))
            return 0.0f;
    }

    if (action->GetThreatType() == ActionThreatType::ACTION_THREAT_NONE)
        return 1.0f;

    // Burn the raid's timed Fankriss Spawn before its enrage. Single-target
    // only; boss damage and unrelated AoE keep their threat gates.
    if (action->GetThreatType() == ActionThreatType::ACTION_THREAT_SINGLE &&
        FankrissBurnSpawn(ai, action->GetTarget()))
        return 1.0f;

    // Heals never back off. CastHealingSpellAction carries the AoE threat
    // type, and the threat value reads 100 for any mob in combat that the
    // tank has not touched yet, so while the tank was mounted, idle or on
    // another mob the veto held every heal and shield and the healer
    // watched the party die at full mana. The back-off is for damage that
    // pulls a mob off the tank; a heal that keeps the mob's victim alive is
    // never the worse choice. mod-playerbots does not give its healers this
    // multiplier by default, and its FocusMultiplier exempts heals the same
    // way.
    // A party cure or dispel joins the heals behind AiPlayerbot.DungeonRunCureIgnoresThreat: it is a SINGLE-threat
    // CastSpellAction, so a healer whose own threat on its target sat at the high line had every cure vetoed and
    // Jin'do's Hex stayed on the tank for its full 5 s (ThreatCapPolicy.h ThreatCapExempt).
    // An interrupt joins the heals behind AiPlayerbot.InterruptIgnoresThreat: Kick and the other pure interrupts are
    // SINGLE-threat spell actions, so a rogue at the high line had every Kick vetoed and stood idle at full energy
    // while the caster finished. It is measured on the interrupt's own target (the "interrupt target" pick, which can
    // be another mob than the current target) against the core's pull line: the bot's threat plus the interrupt's flat
    // threat (spell threat table) under 110 % of the tank's when the mob can reach the bot in melee, 130 % otherwise
    // (ThreatCapPolicy.h ThreatCapInterruptExempt). Earth Shock and Death Coil rows stay gated.
    bool const heal = dynamic_cast<CastHealingSpellAction*>(action) != nullptr;
    bool const cure = dynamic_cast<CurePartyMemberAction*>(action) != nullptr;
    CastInterruptSpellAction const* interruptAction = dynamic_cast<CastInterruptSpellAction*>(action);
    bool const interrupt = interruptAction != nullptr && interruptAction->IgnoresThreatCap();
    bool const interruptKey = interrupt && sPlayerbotAIConfig.interruptIgnoresThreat;
    float interruptBotThreat = 0.0f;
    float interruptTankThreat = 0.0f;
    float interruptFlatThreat = 0.0f;
    bool interruptRanged = false;
    if (interruptKey)
    {
        Unit* const interruptTarget = action->GetTarget();
        interruptBotThreat = ThreatValue::GetThreat(bot, interruptTarget);
        interruptTankThreat = ThreatValue::GetTankThreat(ai, interruptTarget);
        if (SpellThreatEntry const* threatEntry = sSpellMgr.GetSpellThreatEntry(interruptAction->GetSpellID()))
            interruptFlatThreat = threatEntry->threat;
        // The core's own test for the 110 % rule: can the mob reach this attacker with a melee swing.
        interruptRanged = !interruptTarget || !interruptTarget->CanReachWithMeleeAutoAttack(bot);
    }
    if (ThreatCapExempt(heal, cure, sPlayerbotAIConfig.dungeonRunCureIgnoresThreat) ||
        ThreatCapInterruptExempt(interrupt, sPlayerbotAIConfig.interruptIgnoresThreat, interruptBotThreat,
                                 interruptTankThreat, interruptFlatThreat, interruptRanged))
        return 1.0f;

    if (!AI_VALUE(bool, "group"))
        return 1.0f;

    // AiPlayerbot.ThreatCapCountsCastThreat: the high-threat veto below reads only the threat the bot has now, so a
    // slow nuke started under its 80 % line (a Fireball at 79 %) landed as one big hit past the core's pull line. A
    // single-target damage spell from a non-tank at a mob whose victim is a tank is also held while the bot's threat
    // plus the cast's expected threat reaches the top tank's (ThreatCapPolicy.h ThreatCapCastOvershoots). Measured on
    // the action's own target. The key is read first: off, nothing more is computed.
    if (sPlayerbotAIConfig.threatCapCountsCastThreat &&
        action->GetThreatType() == ActionThreatType::ACTION_THREAT_SINGLE && !PlayerbotAI::IsTank(bot))
    {
        if (CastSpellAction const* castAction = dynamic_cast<CastSpellAction*>(action))
        {
            Unit* const castTarget = action->GetTarget();
            Unit* const castVictim = castTarget ? castTarget->GetVictim() : nullptr;
            Player* const castVictimPlayer = castVictim ? castVictim->ToPlayer() : nullptr;
            if (castTarget && !sServerFacade.IsFriendlyTo(castTarget, bot) && castVictimPlayer &&
                castVictimPlayer != bot && PlayerbotAI::IsTank(castVictimPlayer))
            {
                float const castTankThreat = ThreatValue::GetTankThreat(ai, castTarget);
                float const castBotThreat = ThreatValue::GetThreat(bot, castTarget);
                float const castThreat = ExpectedCastThreat(bot, castTarget, castAction->GetSpellID());
                if (ThreatCapCastOvershoots(sPlayerbotAIConfig.threatCapCountsCastThreat, castBotThreat, castThreat,
                                            castTankThreat))
                {
                    LogCastVeto(ai, bot, castAction, castBotThreat, castThreat, castTankThreat);
                    return 0.0f;
                }
            }
        }
    }

    if (action->GetThreatType() == ActionThreatType::ACTION_THREAT_AOE)
    {
        uint8 threat = AI_VALUE2(uint8, "threat", "aoe");
        if (threat >= 50)
            return 0.0f;
    }


    if (ai->HasStrategy("debug threat", BotState::BOT_STATE_COMBAT))
    {
        if (ai->GetMaster())
        {
            if (AI_VALUE2(bool, "trigger active", "high threat"))
            {
                ai->GetBot()->SendPlaySpellVisual(6372);
            }
            else if (AI_VALUE2(bool, "trigger active", "medium threat"))
            {
                ai->GetBot()->SendPlaySpellVisual(5036);
            }
        }
    }

    if (AI_VALUE2(bool, "trigger active", "high threat"))
    {
        // In a commanded party a mob already hitting the healer or a damage
        // bot is off the tank: backing off it leaves it there
        // (RemoteCommander::CommandedBackoffYields). On a taunt-immune target
        // the back-off is never yielded: the tank cannot take it back, so the
        // damage has to stop for the tanks' threat to pass the member.
        Unit* target = AI_VALUE(Unit*, "current target");
        Unit* victim = target ? target->GetVictim() : nullptr;
        Player* victimPlayer = victim ? victim->ToPlayer() : nullptr;
        bool const hitsMember = victimPlayer && bot->GetGroup() && bot->GetGroup()->IsMember(victimPlayer->GetObjectGuid());
        if (hitsMember && TortoiseBots::RemoteCommander::CommandedBackoffYields(
                TortoiseBots::BotCommands::IsInCommandedGroup(bot), true, PlayerbotAI::IsTank(victimPlayer),
                TargetTauntImmune(target)))
            return 1.0f;
        if (interruptKey)
            LogInterruptVeto(ai, bot, interruptAction, interruptBotThreat, interruptTankThreat, interruptFlatThreat,
                             interruptRanged);
        return 0.0f;
    }

    return 1.0f;
}

// The successor white stop (AiPlayerbot.DungeonRunThreatSuccessorStrict): the trigger stops the swing and the auto-repeat
// itself in the trigger pass and is never active, so its handler is never queued. The handler is a no-op at
// ACTION_PASSTROUGH only so the node's first relevance is 100: Engine::ProcessTriggers(minimal) checks just the nodes at
// 100 or more, and an empty node (-1) was skipped on every minimal tick, letting a swing run on past the line. Key off:
// the trigger returns at its first line.
void ThreatStrategy::InitCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode("successor white cap",
        NextAction::array(0, new NextAction("successor white cap noop", ACTION_PASSTROUGH), NULL)));
    // AiPlayerbot.DungeonRunThreatResetWindow's white-damage stop: the trigger stops the swing and auto-repeat in the
    // trigger pass and is never active, so its handler is never queued (the same shape as the successor stop).
    triggers.push_back(new TriggerNode("threat reset white stop",
        NextAction::array(0, new NextAction("threat reset white stop noop", ACTION_PASSTROUGH), NULL)));
}

void ThreatStrategy::InitCombatMultipliers(std::list<Multiplier*> &multipliers)
{
    multipliers.push_back(new ThreatMultiplier(ai));
}
