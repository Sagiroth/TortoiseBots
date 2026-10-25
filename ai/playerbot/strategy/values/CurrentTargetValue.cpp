
#include "playerbot/playerbot.h"
#include "CurrentTargetValue.h"
#include "playerbot/OnyxiaAirCombat.h"

#include "playerbot/ServerFacade.h"
using namespace ai;

Unit* CurrentTargetValue::Get()
{
    if (selection.IsEmpty())
        return NULL;

    Unit* unit = sObjectAccessor.GetUnit(*bot, selection);
    // An explicit attack order carries the walk to the target: keep it as the current
    // target out to the order range so the reach chain can close the distance. Every
    // other target keeps the background sight-distance ceiling.
    float maxDistance = sPlayerbotAIConfig.sightDistance;
    if (unit && ai->GetAiObjectContext()->GetValue<ObjectGuid>("explicit attack target")->Get() == unit->getObjectGuid())
        maxDistance = sPlayerbotAIConfig.reactDistance;
    if (unit && IsOnyxiaAirCombatTarget(ai, unit))
        return unit;
    if (unit && !bot->IsWithinDistInMap(unit, maxDistance))
        return NULL;

    // Distance was the only test here, so a target kept once was kept for good -
    // a player who stealthed after being targeted stayed targeted. This asks the
    // same question the target list asks before picking anyone: can the bot still
    // see them. It covers stealth, invisibility and visibility state and does not
    // involve line of sight, so a target behind a pillar is not lost.
    if (unit && unit != bot &&
        !unit->IsVisibleForOrDetect(bot, bot->GetCamera().GetBody(), true))
        return NULL;

    return unit;
}

void CurrentTargetValue::Set(Unit* target)
{
    selection = target ? target->getObjectGuid() : ObjectGuid();
}
