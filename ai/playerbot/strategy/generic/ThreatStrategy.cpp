
#include "playerbot/playerbot.h"
#include "ThreatStrategy.h"
#include "playerbot/strategy/actions/FankrissFocusActions.h"
#include "playerbot/strategy/values/ThreatValues.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
#include "playerbot/ThreatCapPolicy.h"
#include "playerbot/BotActionLog.h"
#include "Spells/SpellMgr.h"
#include "../../../../runtime/CommandedGroupQuery.h"
#include "../../../../runtime/RemoteCommanderPolicy.h"

#include <cstdio>
#include <iterator>
#include <mutex>
#include <unordered_map>

using namespace ai;

namespace
{
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

float ThreatMultiplier::GetValue(Action* action)
{
    // Per-fight suppression (mod-playerbots "neglect threat"): a boss
    // fight strategy that owns positioning/targeting sets this while
    // engaged so threat gating does not veto its scripted target choice.
    if (AI_VALUE(bool, "neglect threat"))
        return 1.0f;

    if (action == NULL || action->GetThreatType() == ActionThreatType::ACTION_THREAT_NONE)
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
    // An interrupt joins the heals behind AiPlayerbot.InterruptIgnoresThreat: Kick and the other pure interrupts are
    // SINGLE-threat spell actions, so a rogue at the high line had every Kick vetoed and stood idle at full energy
    // while the caster finished. It is measured on the interrupt's own target (the "interrupt target" pick, which can
    // be another mob than the current target) against the core's pull line: the bot's threat plus the interrupt's flat
    // threat (spell threat table) under 110 % of the tank's when the mob can reach the bot in melee, 130 % otherwise
    // (ThreatCapPolicy.h ThreatCapInterruptExempt). Earth Shock and Death Coil rows stay gated.
    bool const heal = dynamic_cast<CastHealingSpellAction*>(action) != nullptr;
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
    if (heal || ThreatCapInterruptExempt(interrupt, sPlayerbotAIConfig.interruptIgnoresThreat, interruptBotThreat,
                                         interruptTankThreat, interruptFlatThreat, interruptRanged))
        return 1.0f;

    if (!AI_VALUE(bool, "group"))
        return 1.0f;

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

void ThreatStrategy::InitCombatMultipliers(std::list<Multiplier*> &multipliers)
{
    multipliers.push_back(new ThreatMultiplier(ai));
}
