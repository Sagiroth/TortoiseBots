
#include "playerbot/playerbot.h"
#include "AttackerWithoutAuraTargetValue.h"
#include "TargetValue.h"
#include "CcMarkHoldPolicy.h"
#include "playerbot/PlayerbotAIConfig.h"

using namespace ai;

Unit* AttackerWithoutAuraTargetValue::Calculate()
{
    std::list<ObjectGuid> attackers = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    Unit* target = ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
    CcGuard ccGuard(ai);
    for (std::list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); ++i)
    {
        Unit* unit = ai->GetUnit(*i);
        if (!unit || unit == target)
            continue;

        // DoT spreading skips a held CC mark: the DoT ticks would break
        // the sheep the moment it lands. With the run's automatic crowd
        // control on, no DoT goes on a CC mark or a unit already under
        // damage-breakable CC either (CcDotTargetAllowed).
        bool const ccMarkOrControlled = sPlayerbotAIConfig.dungeonRunAutoCc &&
            (ccGuard.IsCcMarkedUnit(unit) || ccGuard.IsCrowdControlledUnit(unit));
        if (!CcDotTargetAllowed(sPlayerbotAIConfig.dungeonRunAutoCc, ccGuard.IsHeldCcMark(unit), ccMarkOrControlled))
            continue;

        if (bot->GetDistance(unit) > ai->GetRange("spell"))
            continue;

        if (!ai->HasAura(qualifier, unit, false, checkIsOwner))
            return unit;
    }

    return NULL;
}
