
#include "playerbot/playerbot.h"
#include "InvalidTargetValue.h"
#include "PossibleAttackTargetsValue.h"
#include "EnemyPlayerValue.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"

using namespace ai;

bool InvalidTargetValue::Calculate()
{
    Unit* target = AI_VALUE(Unit*, qualifier);
    if (!target || !target->IsInWorld() || target->GetMapId() != bot->GetMapId())
    {
        return true;
    }

    Unit* duelTarget = AI_VALUE(Unit*, "duel target");
    if (duelTarget && duelTarget == target)
    {
        return false;
    }

    if (qualifier == "current target")
    {
        if (target->getObjectGuid() != bot->GetSelectionGuid())
        {
            return true;
        }
    }

    // A player command deliberately selects a hostile target before either side
    // has threat.  The ordinary validity path requires active combat, which made
    // ranged bots immediately discard that target before their first spell (a
    // melee bot happened to survive because auto-attack creates combat at once).
    // Keep every normal attackability/range/CC check, but let the bot's own
    // explicit attack target pass the combat-only gate until the pull begins.
    const bool explicitAttackTarget = qualifier == "current target" &&
        AI_VALUE(ObjectGuid, "attack target") == target->getObjectGuid();
    const bool validTarget = PossibleAttackTargetsValue::IsValid(target, bot,
        sPlayerbotAIConfig.sightDistance, false, !explicitAttackTarget);
    if (!validTarget)
    {
        std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
        if (std::find(attackers.begin(), attackers.end(), target->getObjectGuid()) != attackers.end())
        {
            return false;
        }
    }

    return !validTarget;
}
