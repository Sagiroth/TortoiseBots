
#include "playerbot/playerbot.h"
#include "SnareTargetValue.h"
#include "TargetValue.h"
#include "CcMarkHoldPolicy.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "Movement/TargetedMovementGenerator.h"

using namespace ai;

Unit* SnareTargetValue::Calculate()
{
    std::string spell = qualifier;

    Unit* enemy = AI_VALUE(Unit*, "enemy player target");
    if (enemy)
    {
        Player* plr = dynamic_cast<Player*>(enemy);
        if (plr && !(plr->HasAuraType(SPELL_AURA_MOD_ROOT) || plr->HasAuraType(SPELL_AURA_MOD_STUN)) && (!plr->IsStopped() || plr->IsNonMeleeSpellCasted(false) || (plr->GetVictim() && plr->GetVictim()->getObjectGuid() == bot->getObjectGuid())))
            return enemy;
    }

    std::list<ObjectGuid> attackers = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    Unit* target = ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
    CcGuard ccGuard(ai);
    for (std::list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); ++i)
    {
        Unit* unit = ai->GetUnit(*i);
        if (!unit)
            continue;

        // A held CC mark (sheeped, under the CC cast bar or in its hold
        // window) is the CC bot's job: a damaging snare on it breaks the
        // sheep, and only the tank's Taunt may touch it, while it is not
        // CC'd yet and is hitting a healer (owner decision). Taunt does no
        // damage and does not change the tank's current target. A free
        // mark is a normal snare/Taunt target.
        if (ccGuard.IsHeldCcMark(unit))
        {
            Unit* victim = unit->GetVictim();
            Player* victimPlayer = victim ? victim->ToPlayer() : nullptr;
            bool const tankTaunt = spell == "taunt" && ai->IsTank(bot);
            if (!tankTaunt ||
                !CcHeldMarkTauntAllowed(tankTaunt, FindNonCcTargetStrategy::IsCrowdControlled(ai, unit),
                    ccGuard.GroupCastsCcAt(unit), victimPlayer && PlayerbotAI::IsHeal(victimPlayer)))
                continue;
            if (sServerFacade.getDistance2d(bot, unit) > ai->GetRange("spell"))
                continue;
            // Standing or chasing: the healer is being hit either way.
            return unit;
        }

        if (sServerFacade.getDistance2d(bot, unit) > ai->GetRange("spell"))
            continue;

        // case real player or bot not moving
        if (unit->IsPlayer() && unit->GetMotionMaster()->GetCurrentMovementGeneratorType() == IDLE_MOTION_TYPE)
        {
            if (Unit* victim = unit->GetVictim())
            {
                // check if need to snare
                bool shouldSnare = true;

                // do not slow down if bot is melee and mob/bot attack each other
                if (victim == bot || victim->IsPlayer())
                    shouldSnare = true;

                if (unit->HasAuraType(SPELL_AURA_MOD_ROOT) || unit->HasAuraType(SPELL_AURA_MOD_STUN))
                    shouldSnare = false;

                if (victim && shouldSnare)
                    return unit;
            }
        }

        Unit* chaseTarget;
        switch (unit->GetMotionMaster()->GetCurrentMovementGeneratorType())
        {
        case FLEEING_MOTION_TYPE:
            return unit;
        case CHASE_MOTION_TYPE:
            chaseTarget = sServerFacade.GetChaseTarget(unit);
            if (!chaseTarget) continue;
            Player* chaseTargetPlayer = sObjectMgr.GetPlayer(chaseTarget->getObjectGuid());

            // check if need to snare
            bool shouldSnare = true;

            // do not slow down if bot is melee and mob/bot attack each other
            if (chaseTargetPlayer && !ai->IsRanged(bot) && chaseTargetPlayer == bot)
                shouldSnare = false;

            if (!sServerFacade.isMoving(unit))
                shouldSnare = false;

            if (unit->HasAuraType(SPELL_AURA_MOD_ROOT) || unit->HasAuraType(SPELL_AURA_MOD_STUN))
                shouldSnare = false;

            if (chaseTargetPlayer && shouldSnare && !ai->IsTank(chaseTargetPlayer))
                return unit;
        }
    }

    return NULL;
}
