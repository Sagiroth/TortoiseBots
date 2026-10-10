#include "playerbot/playerbot.h"
#include "DungeonMultipliers.h"
#include "playerbot/strategy/actions/DungeonActions.h"
#include "playerbot/strategy/actions/ReachTargetActions.h"
#include "playerbot/strategy/actions/ChooseTargetActions.h"
#include "playerbot/strategy/actions/GenericActions.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/GolemaggPolicy.h"

using namespace ai;

float PreventMoveAwayFromCreatureOnReachToCastMultiplier::GetValue(Action* action)
{
    MoveAwayFromCreature* moveAwayAction = dynamic_cast<MoveAwayFromCreature*>(action);
    if (moveAwayAction)
    {
        const Action* lastExecutedAction = ai->GetLastExecutedAction(BotState::BOT_STATE_COMBAT);
        if (lastExecutedAction)
        {
            const ReachTargetAction* reachAction = dynamic_cast<const ReachTargetAction*>(lastExecutedAction);
            if (reachAction && !reachAction->GetSpellName().empty())
            {
                return 0.0f;
            }
        }
    }

    return 1.0f;
}

float GolemaggFightMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;
    if (!ai->HasStrategy("golemagg", BotState::BOT_STATE_COMBAT))
        return 1.0f;
    AiObjectContext* context = ai->GetAiObjectContext();
    const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    Unit* boss = nullptr;
    for (const ObjectGuid& attackerGuid : attackers)
    {
        Unit* attacker = ai->GetUnit(attackerGuid);
        if (attacker && attacker->GetEntry() == kGolemaggEntry)
        {
            boss = attacker;
            break;
        }
    }
    if (!boss)
        return 1.0f;
    bool burnPhase = boss->GetHealthPercent() <= kGolemaggBurnPct;
    // Living tanks in the group (slot order; ld-8 value pending).
    unsigned livingTanks = 0;
    if (Group* group = bot->GetGroup())
    {
        for (Player* member : LiveGroupMembers(group))
        {
            if (member && sServerFacade.IsAlive(member) && ai->IsTank(member))
                ++livingTanks;
        }
    }
    else if (ai->IsTank(bot))
    {
        livingTanks = 1;
    }
    // Single tank picks up everything: role-hold actions would only fight
    // the normal target selection.
    if (IsSingleLivingTank(livingTanks) && ai->IsTank(bot))
    {
        if (dynamic_cast<MoveToAction*>(action) && action->getName() == "golemagg tank hold")
            return 0.0f;
    }
    // Assist tanks (non-first tanks) never follow tank-assist retargets:
    // they stay glued to their rager.
    if (ai->IsTank(bot) && livingTanks > 1)
    {
        bool botIsFirstTank = false;
        if (Group* group = bot->GetGroup())
        {
            for (Player* member : LiveGroupMembers(group))
            {
                if (!member || !sServerFacade.IsAlive(member) || !ai->IsTank(member))
                    continue;
                botIsFirstTank = (member == bot);
                break;
            }
        }
        if (!botIsFirstTank && dynamic_cast<TankAssistAction*>(action))
            return 0.0f;
    }
    // DPS AoE stays off for the whole fight (rager Trust + splash).
    // Name-matched set (same list as the Garr veto): threat flags do not
    // mark our real AoE and wrongly flag heals + single-target dots.
    if (!ai->IsTank(bot) && !ai->IsHeal(bot))
    {
        if (IsGolemaggSuppressedAoeAction(action->getName()) && ShouldExcludeRager(true))
            return 0.0f;
    }
    if (burnPhase)
        return 1.0f;
    // Ranged never melee-fallbacks onto the boss (splash stacks).
    if (ai->IsRanged(bot) && dynamic_cast<MeleeAction*>(action))
        return 0.0f;
    // Backed-off non-tanks stay out until the WHOLE stack expires
    // (donor: 30s after the last application) — not just below 20, or a
    // 20→19 decay re-engages with 19 stacks still ticking.
    Aura* splash = ai->GetAura(kMagmaSplashSpellId, bot);
    bool backedOff = ShouldHoldBackOff(ai->IsTank(bot), splash != nullptr,
        (float)boss->GetHealthPercent());
    // Boss-only engages: adds and totems stay attackable. Reach actions
    // ("reach melee", "reach spell") count — they are how the bot walks
    // back into splash range.
    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    bool targetsBoss = currentTarget && currentTarget == boss;
    bool engagesBoss = targetsBoss &&
        (dynamic_cast<AttackAction*>(action) != nullptr ||
         dynamic_cast<MeleeAction*>(action) != nullptr ||
         dynamic_cast<ReachTargetAction*>(action) != nullptr ||
         dynamic_cast<CastReachTargetSpellAction*>(action) != nullptr);
    if (backedOff && engagesBoss)
    {
        // The back-off move itself and the healer spot always pass.
        std::string name = action->getName();
        if (name != "back off golemagg" && name != "golemagg healer position")
            return 0.0f;
    }
    return 1.0f;
}
