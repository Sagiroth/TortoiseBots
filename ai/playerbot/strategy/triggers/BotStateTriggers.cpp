
#include "playerbot/playerbot.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/policy/combat/CombatIdlePolicy.h"
#include "playerbot/strategy/generic/PullStrategy.h"
#include "BotStateTriggers.h"

using namespace ai;

bool CombatStartTrigger::IsActive()
{
    if (!ai->IsStateActive(BotState::BOT_STATE_COMBAT) && !ai->IsStateActive(BotState::BOT_STATE_DEAD))
    {
        // Check if any member of the group (near this bot) is getting attacked
        return AI_VALUE(bool, "has attackers");
    }

    return false;
}

namespace
{
    // True while the held order on target is a fight in progress, not a
    // stale pointer. Victim or threat on the bot, its pet, or a live
    // groupmate counts, as does an explicit order the bot is still walking
    // to (in order range, bot moving). Everything else - far, idle,
    // unreachable, evading - is stale and must not veto combat end.
    bool CombatOrderEngaged(Player* bot, Unit* target, ObjectGuid const& attackGuid)
    {
        if (!target)
            return false;
        if (Creature* creature = dynamic_cast<Creature*>(target))
        {
            if (creature->IsInEvadeMode() || creature->IsEvadeBecauseTargetNotReachable())
                return false;
        }
        if (target->GetVictim() == bot)
            return true;
        if (target->GetThreatManager().getThreat(bot) > 0.0f)
            return true;
        if (Pet* pet = bot->GetPet())
        {
            if (target->GetVictim() == pet || target->GetThreatManager().getThreat(pet) > 0.0f)
                return true;
        }
        if (Group* group = bot->GetGroup())
        {
            Group::MemberSlotList const& slots = group->GetMemberSlots();
            for (Group::member_citerator itr = slots.begin(); itr != slots.end(); ++itr)
            {
                Player* member = sObjectMgr.GetPlayer(itr->guid);
                if (!member || member == bot || !sServerFacade.IsAlive(member) || !member->IsInWorld() ||
                    member->GetMapId() != bot->GetMapId())
                    continue;
                if (target->GetVictim() == member || target->GetThreatManager().getThreat(member) > 0.0f)
                    return true;
            }
        }
        // An explicit order still being walked to: the bot's own attack
        // target inside order range while the bot moves. A standing bot
        // holding a far order is wedged, not pulling.
        if (!attackGuid.IsEmpty() && target->getObjectGuid() == attackGuid &&
            bot->IsWithinDistInMap(target, sPlayerbotAIConfig.reactDistance) &&
            (sServerFacade.isMoving(bot) || bot->IsMoving()))
            return true;
        // Someone else's fight the bot walked into: a target already in
        // core combat with threat on anyone holds while it is close enough
        // to matter; far away it is scenery.
        if (sServerFacade.IsInCombat(target) && target->GetVictim() &&
            bot->IsWithinDistInMap(target, sPlayerbotAIConfig.sightDistance))
            return true;
        return false;
    }
}

bool CombatEndTrigger::IsActive()
{
    // Check if the bot is currently in combat
    if (ai->IsStateActive(BotState::BOT_STATE_COMBAT))
    {
        // Don't end combat if pull is in progress
        PullStrategy* strategy = PullStrategy::Get(ai);
        if (strategy && strategy->HasTarget())
            return false;

        // A held order vetoes combat end only while it is a real fight. A
        // stale order on a far, idle, unreachable or evading mob must not:
        // the bot would sit in the combat engine doing nothing until the
        // 5-minute combat-stuck rescue, the way Grin did.
        Unit* currentTarget = AI_VALUE(Unit*, "current target");
        ObjectGuid attackGuid = AI_VALUE(ObjectGuid, "attack target");
        Unit* attackTarget = attackGuid.IsEmpty() ? nullptr : ai->GetUnit(attackGuid);
        bool orderHolds = CombatOrderHolds(
            currentTarget && sServerFacade.IsAlive(currentTarget) && sServerFacade.IsHostileTo(currentTarget, bot),
            currentTarget && CombatOrderEngaged(bot, currentTarget, attackGuid)) ||
            CombatOrderHolds(
                attackTarget && sServerFacade.IsAlive(attackTarget) && sServerFacade.IsHostileTo(attackTarget, bot),
                attackTarget && CombatOrderEngaged(bot, attackTarget, attackGuid));
        if (orderHolds)
            return false;

        // Check if any member of the group (near this bot) is getting attacked
        if (AI_VALUE(bool, "has attackers"))
            return false;

        // Core quiet, no attackers, no engaged order: clear whatever stale
        // orders survive and switch at once when the idle window has passed,
        // the way .combatstop does. The normal path keeps its instant switch
        // when no order is held at all.
        bool const staleOrder = currentTarget || attackTarget ||
            !attackGuid.IsEmpty() || !AI_VALUE(ObjectGuid, "explicit attack target").IsEmpty();
        time_t const combatStart = AI_VALUE(time_t, "combat start time");
        uint32 const idleSeconds = combatStart ? uint32(time(0) - combatStart) : 0;
        if (staleOrder && !CombatIdleExitDue(sServerFacade.IsInCombat(bot), false, false, idleSeconds))
            return false;
        if (staleOrder)
        {
            AiObjectContext* context = ai->GetAiObjectContext();
            context->GetValue<ObjectGuid>("attack target")->Set(ObjectGuid());
            context->GetValue<ObjectGuid>("explicit attack target")->Set(ObjectGuid());
            context->GetValue<Unit*>("current target")->Set(nullptr);
            bot->AttackStop();
        }

        return true;
    }

    return false;
}

bool DeathTrigger::IsActive()
{
    return !ai->IsStateActive(BotState::BOT_STATE_DEAD) && !sServerFacade.IsAlive(bot);
}

bool ResurrectTrigger::IsActive()
{
    return ai->IsStateActive(BotState::BOT_STATE_DEAD) && sServerFacade.IsAlive(bot);
}