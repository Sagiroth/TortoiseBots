
#include "playerbot/GroupMembers.h"
#include "playerbot/playerbot.h"
#include "ThreatValues.h"

#include "Objects/Creature.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/ThreatCapPolicy.h"
#include "Threat/ThreatManager.h"

using namespace ai;

float MyThreatValue::Calculate()
{
    Unit* target = AI_VALUE(Unit*, qualifier);

    ObjectGuid targetGuid = target ? target->getObjectGuid() : ObjectGuid();
    if (targetGuid != lastTarget) //Reset history if we switched target.
        LogCalculatedValue::Reset();

    lastTarget = targetGuid;

    return ThreatValue::GetThreat(bot, target);
}

float TankThreatValue::Calculate()
{
    Unit* target = AI_VALUE(Unit*, qualifier);

    return ThreatValue::GetTankThreat(ai, target);
}

uint8 ThreatValue::Calculate()
{
    if (qualifier == "aoe")
    {
        uint8 maxThreat = 0;
        std::list<ObjectGuid> attackers = context->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
        for (std::list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); i++)
        {
            Unit* unit = ai->GetUnit(*i);
            if (!unit || !sServerFacade.IsAlive(unit))
                continue;

            uint8 threat = Calculate(unit);
            if (!maxThreat || threat > maxThreat)
                maxThreat = threat;
        }

        return maxThreat;
    }

    Unit* target = AI_VALUE(Unit*, qualifier);

    if (target && sServerFacade.IsFriendlyTo(target, bot))
        target = target->GetVictim();

    return Calculate(target);
}

float ThreatValue::GetThreat(Player* player, Unit* target)
{
    if (!target || target->GetMapId() != player->GetMapId())
        return 0;

    if (dynamic_cast<Player*>(target) && (dynamic_cast<Player*>(target))->IsBeingTeleported())
        return 0;

    if (sServerFacade.IsFriendlyTo(target, player))
        target = target->GetVictim();

    if (!target || target->getObjectGuid().IsPlayer())
        return 0;

    float botThreat = sServerFacade.GetThreatManager(target).getThreat(player);

    return botThreat;
}

float ThreatValue::GetTankThreat(PlayerbotAI* ai, Unit* target)
{
    if (!target)
        return 0;

    if (sServerFacade.IsFriendlyTo(target, ai->GetBot()))
        target = target->GetVictim();

    if (!target || target->getObjectGuid().IsPlayer())
        return 0;

    Group* group = ai->GetBot()->GetGroup();
    if (!group)
        return 0;

    std::vector<float> tankThreats;

    for (Player* player : LiveGroupMembers(group))
    {
        if (!player || !sServerFacade.IsAlive(player) || !ai->IsSafe(player) || player == ai->GetBot())
            continue;

        if (ai->IsTank(player))
            tankThreats.push_back(sServerFacade.GetThreatManager(target).getThreat(player));
    }

    // A taunt-immune target cannot be pulled back, so the damage dealers are measured against the second-highest tank
    // and that tank stays second (the off-tank takes the boss on the next threat reset).
    return ThreatCapReference(tankThreats, TargetTauntImmune(target));
}

bool ai::TargetTauntImmune(Unit* target)
{
    Creature* const creature = target ? dynamic_cast<Creature*>(target) : nullptr;
    return creature && creature->HasImmunityFlag(CREATURE_IMMUNITY_TAUNT);
}

uint8 ThreatValue::Calculate(Unit* target)
{
    if (!target)
        return 0;

    if (sServerFacade.IsFriendlyTo(target, bot))
        target = target->GetVictim();

    if (!target)
        return 0;

    if (target->getObjectGuid().IsPlayer())
        return 0;

    Group* group = bot->GetGroup();
    if (!group)
        return 0;

    float botThreat, maxThreat;
    if (qualifier == "aoe")
    {
        botThreat = GetThreat(bot, target);
        maxThreat = GetTankThreat(ai, target);
    }
    else
    {
        botThreat = AI_VALUE2(float, "my threat", qualifier);
        maxThreat = AI_VALUE2(float, "tank threat", qualifier);
    }

    if (maxThreat < 0)
        return 0;

    // calculate normal threat for fleeing targets
    // (the flee-for-assistance walk and its pause count too: Creature::DoFleeToGetAssistance)
    MovementGeneratorType const motion = target->GetMotionMaster()->GetCurrentMovementGeneratorType();
    bool fleeing = motion == FLEEING_MOTION_TYPE || motion == TIMED_FLEEING_MOTION_TYPE ||
        motion == ASSISTANCE_MOTION_TYPE || motion == ASSISTANCE_DISTRACT_MOTION_TYPE;

    // return high threat if tank has no threat
    if (target->IsInCombat() && maxThreat == 0 && !fleeing)
        return 100;

    // return low threat if mob if fleeing
    if (maxThreat > 0 && fleeing)
        return 0;

    return botThreat * 100 / maxThreat;
}
