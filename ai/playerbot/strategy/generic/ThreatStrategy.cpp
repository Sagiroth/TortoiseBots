
#include "playerbot/playerbot.h"
#include "ThreatStrategy.h"
#include "playerbot/strategy/actions/FankrissFocusActions.h"
#include "playerbot/strategy/values/ThreatValues.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/actions/GenericActions.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
#include "playerbot/ThreatCapPolicy.h"
#include "../../../../runtime/CommandedGroupQuery.h"
#include "../../../../runtime/RemoteCommanderPolicy.h"

using namespace ai;

namespace
{
// The damage the threat multiplier used to exempt: the melee auto-attack, the wand's Shoot and the hunter's Auto Shot.
// The successor cap has to see them (ThreatCapPolicy.h); every other ACTION_THREAT_NONE action keeps the old exemption.
bool AutoAttackAction(Action* action)
{
    return dynamic_cast<MeleeAction*>(action) || dynamic_cast<CastShootAction*>(action) ||
        action->getName() == "auto shot";
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

    // On a `threat_successor` boss the auto-attack, the wand's Shoot and Auto Shot stop at the successor line like any
    // other attack: they used to be exempt, so a damage dealer at 80-99 % of the off-tank kept adding white and wand
    // threat inside Cause Insanity's charm and the boss turned to it. Tanks are never vetoed.
    if (AutoAttackAction(action))
    {
        Unit* const target = AI_VALUE(Unit*, "current target");
        if (SuccessorCapStopsAutoAttack(ThreatSuccessorTarget(target), PlayerbotAI::IsTank(bot),
                AI_VALUE2(uint8, "threat", "current target")))
            return 0.0f;
        if (action->GetThreatType() == ActionThreatType::ACTION_THREAT_NONE)
            return 1.0f;
    }
    else if (action->GetThreatType() == ActionThreatType::ACTION_THREAT_NONE)
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
    if (dynamic_cast<CastHealingSpellAction*>(action))
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
        return 0.0f;
    }

    return 1.0f;
}

void ThreatStrategy::InitCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "successor threat cap",
        NextAction::array(0, new NextAction("stop auto attack", ACTION_HIGH), NULL)));
}

void ThreatStrategy::InitCombatMultipliers(std::list<Multiplier*> &multipliers)
{
    multipliers.push_back(new ThreatMultiplier(ai));
}
