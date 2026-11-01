
#include "playerbot/playerbot.h"
#include "SnareTargetValue.h"
#include "TargetValue.h"
#include "CcMarkHoldPolicy.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/policy/combat/RaidSnarePolicy.h"
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
    // Only the tank's Taunt may touch a held mark (below); hoisted out of
    // the loop (IsTank walks strategies/roles, the qualifier never changes).
    // The raid gate is hoisted for the same reason: Group::isRaidGroup and
    // the Main Tank flag never change inside the loop.
    bool const tankTaunt = spell == "taunt" && ai->IsTank(bot);
    Group* group = bot->GetGroup();
    bool const inRaidGroup = group && group->isRaidGroup();
    ObjectGuid const mainTankGuid = group ? group->GetMainTankGuid() : ObjectGuid();
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
        // mark is a normal snare/Taunt target. A non-Taunt spell (or a
        // non-tank) skips it without paying for the tests below.
        if (ccGuard.IsHeldCcMark(unit))
        {
            if (!tankTaunt)
                continue;
            Unit* victim = unit->GetVictim();
            Player* victimPlayer = victim ? victim->ToPlayer() : nullptr;
            if (!CcHeldMarkTauntAllowed(tankTaunt, FindNonCcTargetStrategy::IsCrowdControlled(ai, unit),
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

                // A mob on the tank is already controlled: in a raid group
                // it is never worth the snare GCD (RaidSnarePolicy.h). The
                // victim read covers any tank the module identifies
                // (PlayerbotAI::IsTank) plus the raid's flagged Main Tank.
                Player* victimPlayer = victim ? victim->ToPlayer() : nullptr;
                bool victimOnTank = victimPlayer &&
                    (ai->IsTank(victimPlayer) || (!mainTankGuid.IsEmpty() && victimPlayer->getObjectGuid() == mainTankGuid));
                if (victim && shouldSnare &&
                    SnareAllowedInRaid(inRaidGroup, victimOnTank, false))
                    return unit;
            }
        }

        Unit* chaseTarget;
        switch (unit->GetMotionMaster()->GetCurrentMovementGeneratorType())
        {
        case FLEEING_MOTION_TYPE:
        case TIMED_FLEEING_MOTION_TYPE:
            return unit;
        case ASSISTANCE_MOTION_TYPE:
            // A called add runs at the group, not away from it: in a raid it
            // is picked up, not snared, while it runs to a tank. Outside a
            // raid group the old unconditional pick stands.
            if (inRaidGroup)
                continue;
            return unit;
        case CHASE_MOTION_TYPE:
        {
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

            // Same raid veto as the idle branch: a mob chasing a tank is
            // already controlled.
            bool victimOnTank = chaseTargetPlayer &&
                (ai->IsTank(chaseTargetPlayer) || (!mainTankGuid.IsEmpty() && chaseTarget->getObjectGuid() == mainTankGuid));
            // Outside a raid the old veto stands: never snare a mob chasing a tank.
            if (chaseTargetPlayer && shouldSnare &&
                SnareAllowedInRaid(inRaidGroup, victimOnTank, false) &&
                (inRaidGroup || !ai->IsTank(chaseTargetPlayer)))
                return unit;
        }
        }
    }

    return NULL;
}
