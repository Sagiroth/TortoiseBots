
#include "playerbot/playerbot.h"
#include "ThreatStrategy.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
#include "../../../../runtime/CommandedGroupQuery.h"
#include "../../../../runtime/RemoteCommanderPolicy.h"

using namespace ai;

float ThreatMultiplier::GetValue(Action* action)
{
    // Per-fight suppression (mod-playerbots "neglect threat"): a boss
    // fight strategy that owns positioning/targeting sets this while
    // engaged so threat gating does not veto its scripted target choice.
    if (AI_VALUE(bool, "neglect threat"))
        return 1.0f;

    if (action == NULL || action->GetThreatType() == ActionThreatType::ACTION_THREAT_NONE)
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
        // (RemoteCommander::CommandedBackoffYields).
        Unit* target = AI_VALUE(Unit*, "current target");
        Unit* victim = target ? target->GetVictim() : nullptr;
        Player* victimPlayer = victim ? victim->ToPlayer() : nullptr;
        bool const hitsMember = victimPlayer && bot->GetGroup() && bot->GetGroup()->IsMember(victimPlayer->GetObjectGuid());
        if (hitsMember && TortoiseBots::RemoteCommander::CommandedBackoffYields(
                TortoiseBots::BotCommands::IsInCommandedGroup(bot), true, PlayerbotAI::IsTank(victimPlayer)))
            return 1.0f;
        return 0.0;
    }

    return 1.0f;
}

void ThreatStrategy::InitCombatMultipliers(std::list<Multiplier*> &multipliers)
{
    multipliers.push_back(new ThreatMultiplier(ai));
}
