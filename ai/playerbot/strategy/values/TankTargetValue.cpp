
#include "playerbot/playerbot.h"
#include "TankTargetValue.h"
#include "PossibleAttackTargetsValue.h"
#include "playerbot/ServerFacade.h"

using namespace ai;

// mod-playerbots FindTankTargetSmartStrategy (a63c6b67, refined by 0a76fc1d):
// bucket attackers by GetIntervalLevel instead of a flat lowest-threat
// tournament. Loose mobs (nothing held) come first so adds get picked up;
// held mobs rank by melee reach, then lowest threat, so the tank finishes
// what it holds instead of ping-ponging. Multi-tank / explicit-MT plumbing
// is skipped: single-tank groups only. CC skips stay.
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
        Unit* current = ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        if (oldUnit == current)
            return false;
        if (newUnit == current)
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

    // mod-playerbots conditional tank RTI (LD-5, TankTargetValue.cpp:110-131):
    // take the marked target only when its victim is a non-tank (peel for
    // them) or a tank bot with a different RTI setting (else it is the other
    // tank's mob — leave it for them). Otherwise fall through to the smart
    // tournament instead of peeling the main tank's skull.
    if (Unit* rti = RtiTargetValue::Calculate())
    {
        Unit* victim = rti->GetVictim();
        if (victim && victim != bot)
        {
            if (Player* victimPlayer = dynamic_cast<Player*>(victim))
            {
                if (!ai->IsTank(victimPlayer))
                    return rti;
                PlayerbotAI* victimAi = PlayerbotAIStorage::Instance().GetAI(victimPlayer);
                std::string myRti = ai->GetAiObjectContext()->GetValue<std::string>("rti")->Get();
                if (!victimAi || victimAi->GetAiObjectContext()->GetValue<std::string>("rti")->Get() != myRti)
                    return rti;
            }
            else
                return rti;
        }
        else if (!victim)
            return rti;
    }

    FindTankTargetSmartStrategy strategy(ai);
    return FindTarget(&strategy);
}
