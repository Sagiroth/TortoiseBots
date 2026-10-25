
#include "playerbot/playerbot.h"
#include "AttackerWithoutAuraTargetValue.h"
#include "TargetValue.h"
#include "RaidDpsTargetPolicy.h"
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
        if (!unit || unit == target || IsRaidDpsTargetExcluded(ai, unit))
            continue;

        // DoT spreading skips a held CC mark: the DoT ticks would break
        // the sheep the moment it lands.
        if (ccGuard.IsHeldCcMark(unit))
            continue;

        if (bot->GetDistance(unit) > ai->GetRange("spell"))
            continue;

        if (!ai->HasAura(qualifier, unit, false, checkIsOwner))
            return unit;
    }

    return NULL;
}
