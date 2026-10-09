
#include "playerbot/playerbot.h"
#include "TankTargetValue.h"
#include "PossibleAttackTargetsValue.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/MainTankPolicy.h"
#include "playerbot/ServerFacade.h"

using namespace ai;

namespace
{
    // mod-playerbots PlayerbotAI::GetGroupTankNum: live tanks in the group.
    unsigned GetGroupTankCount(PlayerbotAI* ai)
    {
        Player* bot = ai ? ai->GetBot() : nullptr;
        Group* group = bot ? bot->GetGroup() : nullptr;
        if (!group)
            return 0;

        unsigned count = 0;
        for (Player* member : LiveGroupMembers(group))
            if (member && member->IsAlive() && PlayerbotAI::IsTank(member))
                ++count;

        return count;
    }
}

// mod-playerbots FindTankTargetSmartStrategy (a63c6b67, refined by 0a76fc1d):
// bucket attackers by GetIntervalLevel instead of a flat lowest-threat
// tournament. Loose mobs (nothing held) come first so adds get picked up;
// held mobs rank by melee reach, then lowest threat, so the tank finishes
// what it holds instead of ping-ponging. Explicit-MT stickiness (LD-8):
// in a 2+ tank group the off-tank skips the current-target hold. CC skips stay.
class FindTankTargetSmartStrategy : public FindNonCcTargetStrategy
{
public:
    FindTankTargetSmartStrategy(PlayerbotAI* ai) : FindNonCcTargetStrategy(ai) {}

public:
    virtual void CheckAttacker(Unit* attacker, ThreatManager* /*threatManager*/) override
    {
        Player* bot = ai->GetBot();
        AiObjectContext* context = ai->GetAiObjectContext();

        if (!attacker || !attacker->IsAlive())
            return;

        if (IsCcTarget(attacker)) return;

        if (!PossibleAttackTargetsValue::IsValid(attacker, bot))
        {
            std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
            if (std::find(attackers.begin(), attackers.end(), attacker->getObjectGuid()) == attackers.end())
                return;
        }

        if (!result || IsBetter(attacker, result))
            result = attacker;
    }

    bool IsBetter(Unit* newUnit, Unit* oldUnit)
    {
        Player* bot = ai->GetBot();
        float newThreat = sServerFacade.GetThreatManager(newUnit).getThreat(bot);
        float oldThreat = sServerFacade.GetThreatManager(oldUnit).getThreat(bot);
        if (GetIntervalLevel(newUnit) != GetIntervalLevel(oldUnit))
            return GetIntervalLevel(newUnit) > GetIntervalLevel(oldUnit);

        // Loose adds: nearest first so the pickup is quick.
        if (GetIntervalLevel(newUnit) == 2)
            return bot->GetDistance(newUnit) < bot->GetDistance(oldUnit);

        // Among mobs the tank already holds, stay on the current one; lowest
        // threat only orders the others, so two held mobs never ping-pong.
        // mod-playerbots explicit-MT stickiness (LD-8, TankTargetValue.cpp:76-83):
        // in a 2+ tank group the off-tank skips this hold so loose adds win
        // the tournament above; the main tank (and every lone tank) holds.
        Unit* current = ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        Unit* mainTank = AI_VALUE(Unit*, "main tank");
        unsigned tankCount = GetGroupTankCount(ai);
        bool hold = !mainTank || mainTank == bot || !MainTankSticksToCurrent(true, tankCount);
        if (hold && oldUnit == current)
            return false;
        if (hold && newUnit == current)
            return true;

        return newThreat < oldThreat;
    }

    // 2 = the tank holds nothing here (loose add: pick up first, nearest).
    // 1 = held and in melee reach (finish it). 0 = held but out of reach.
    int GetIntervalLevel(Unit* unit)
    {
        if (!HasTankAggro(unit))
            return 2;

        if (ai->GetBot()->CanReachWithMeleeAutoAttack(unit))
            return 1;

        return 0;
    }

    // "Has aggro" for an arbitrary candidate, mirroring HasAggroValue
    // (values/AttackerCountValues.cpp) minus the multi-tank plumbing: the
    // live victim decides, falling back to the threat manager's victim.
    bool HasTankAggro(Unit* unit)
    {
        Player* bot = ai->GetBot();
        if (!unit)
            return false;

        if (Unit* victim = unit->GetVictim())
        {
            if (victim == bot)
                return true;
            if (Player* victimPlayer = dynamic_cast<Player*>(victim))
                return ai->IsTank(victimPlayer);
            return false;
        }

        HostileReference* ref = sServerFacade.GetThreatManager(unit).getCurrentVictim();
        Unit* victim = ref ? ref->getTarget() : nullptr;
        if (victim == bot)
            return true;
        if (Player* victimPlayer = dynamic_cast<Player*>(victim))
            return ai->IsTank(victimPlayer);
        return false;
    }
};


Unit* TankTargetValue::Calculate()
{
    if (Unit* explicitTarget = GetExplicitAttackTarget())
        return explicitTarget;

    Unit* rti = RtiTargetValue::Calculate();
    if (rti) return rti;

    FindTankTargetSmartStrategy strategy(ai);
    return FindTarget(&strategy);
}
