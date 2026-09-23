
#include "playerbot/playerbot.h"
#include "DpsTargetValue.h"
#include "LeastHpTargetValue.h"
#include "PossibleAttackTargetsValue.h"
#include "playerbot/GroupMembers.h"
#include "../../../../runtime/PlayerbotAIStorage.h"

using namespace ai;

namespace
{
    class DpsTargetStrategy : public FindLeastHpTargetStrategy
    {
    public:
        explicit DpsTargetStrategy(PlayerbotAI* ai) : FindLeastHpTargetStrategy(ai) {}
        using FindNonCcTargetStrategy::IsCcTarget;
    };

    bool IsAttackedByParty(Unit* target, Group* group)
    {
        for (Player* member : LiveGroupMembers(group))
        {
            if (member->GetVictim() == target)
                return true;

            Unit* pet = member->GetPet();
            if (pet && pet->GetVictim() == target)
                return true;
        }

        return false;
    }

    Unit* GetGroupTankTarget(PlayerbotAI* ai)
    {
        Player* bot = ai ? ai->GetBot() : nullptr;
        Group* group = bot ? bot->GetGroup() : nullptr;
        if (!bot || !group)
            return nullptr;

        PlayerbotAIStorage& storage = PlayerbotAIStorage::Instance();
        Player* master = ai->GetMaster();
        bool useHumanMasterTarget = master && master != bot && master->GetGroup() == group &&
            !storage.GetAI(master) && PlayerbotAI::IsTank(master);
        Player* tank = useHumanMasterTarget ? master : nullptr;

        if (!tank)
        {
            for (Player* member : LiveGroupMembers(group))
            {
                if (!storage.GetAI(member) || !PlayerbotAI::IsTank(member))
                    continue;

                tank = member;
                break;
            }
        }

        if (!tank)
            return nullptr;

        Unit* target = useHumanMasterTarget
            ? ai->GetUnit(master->GetSelectionGuid())
            : tank->GetVictim();
        if (!target || !sServerFacade.IsAlive(target) ||
            !PossibleAttackTargetsValue::IsValid(target, bot, sPlayerbotAIConfig.sightDistance, false, false) ||
            !bot->IsWithinLOSInMap(target) ||
            (!target->IsInCombat() && !IsAttackedByParty(target, group)))
            return nullptr;

        return target;
    }
}



Unit* DpsTargetValue::Calculate()
{
    // Explicit orders and raid marks remain ahead of tank assistance.
    if (Unit* explicitTarget = GetExplicitAttackTarget())
        return explicitTarget;

    Unit* rti = RtiTargetValue::Calculate();
    if (rti) return rti;

    DpsTargetStrategy strategy(ai);
    if (Unit* tankTarget = GetGroupTankTarget(ai))
    {
        if (tankTarget != AI_VALUE(Unit*, "cc target") && !strategy.IsCcTarget(tankTarget))
            return tankTarget;
    }

    return TargetValue::FindTarget(&strategy);
}

class FindMaxHpTargetStrategy : public FindTargetStrategy
{
public:
    FindMaxHpTargetStrategy(PlayerbotAI* ai) : FindTargetStrategy(ai)
    {
        maxHealth = 0;
    }

public:
    virtual void CheckAttacker(Unit* attacker, ThreatManager* threatManager) override
    {
        Group* group = ai->GetBot()->GetGroup();
        if (group)
        {
            uint64 guid = group->GetTargetIcon(4);
            if (guid && attacker->getObjectGuid() == ObjectGuid(guid))
                return;
        }
        if (!result || result->GetHealth() < attacker->GetHealth())
            result = attacker;
    }

protected:
    float maxHealth;
};

Unit* DpsAoeTargetValue::Calculate()
{
    if (Unit* explicitTarget = GetExplicitAttackTarget())
        return explicitTarget;

    Unit* rti = RtiTargetValue::Calculate();
    if (rti) return rti;

    FindMaxHpTargetStrategy strategy(ai);
    return TargetValue::FindTarget(&strategy);
}
