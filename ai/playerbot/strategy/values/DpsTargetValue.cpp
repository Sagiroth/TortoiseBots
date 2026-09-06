
#include "playerbot/playerbot.h"
#include "DpsTargetValue.h"
#include "LeastHpTargetValue.h"

using namespace ai;


Unit* DpsTargetValue::Calculate()
{
    // Keep an explicit player-command target ahead of normal assist/RTI
    // selection.  Without this, the first normal combat tick can choose a
    // different group attacker (for example a lower-health mob), causing
    // dps-assist to replace the target the owner just ordered the bot to hit.
    if (Unit* explicitTarget = GetExplicitAttackTarget())
        return explicitTarget;

    Unit* rti = RtiTargetValue::Calculate();
    if (rti) return rti;

    FindLeastHpTargetStrategy strategy(ai);
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
