
#include "playerbot/playerbot.h"
#include "OnyxiasLairDungeonTriggers.h"
#include "playerbot/OnyxiaAirCombat.h"
#include "playerbot/policy/raid/OnyxiaWhelpEggPolicy.h"
#include "playerbot/strategy/actions/OnyxiasLairDungeonActions.h"

using namespace ai;

bool OnyxiaAirborneTrigger::IsActive()
{
    if (!OnyxiaRangedFocusEnabled(ai) || !GetAirborneOnyxia(ai)) return false;
    // The base fight also uses this trigger for generic raid spread. The
    // opt-in air-spread strategy owns its own independent reaction trigger.
    return !ai->HasStrategy("onyxia air spread", BotState::BOT_STATE_COMBAT) || OnyxiaAirNeedsAssist(ai);
}

bool OnyxiaWhelpsTrigger::IsActive()
{
    // Toggle and map first: the default install pays nothing here.
    if (!sPlayerbotAIConfig.onyxiaWhelpsAndEggs || bot->GetMapId() != kOnyxiaMapId)
        return false;
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
        return false;
    if (!ai->HasStrategy("onyxia", BotState::BOT_STATE_COMBAT) || !IsOnyxiaWhelpDuty(ai, bot))
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    const bool explicitOrder = !AI_VALUE(ObjectGuid, "explicit attack target").IsEmpty();
    Unit* current = AI_VALUE(Unit*, "current target");
    const bool onWhelp = current && sServerFacade.IsAlive(current) && current->GetEntry() == kOnyxianWhelpEntry;
    const OnyxiaWhelpScan scan = ScanOnyxiaWhelps(ai, bot);
    return ShouldHuntOnyxiaWhelps(true, true, scan.bossAirborne, true, explicitOrder, onWhelp, scan.whelp != nullptr);
}

bool OnyxiaEggsNearTrigger::IsActive()
{
    if (!sPlayerbotAIConfig.onyxiaWhelpsAndEggs || bot->GetMapId() != kOnyxiaMapId)
        return false;
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
        return false;
    if (!ai->HasStrategy("onyxia", BotState::BOT_STATE_COMBAT))
        return false;
    return ShouldAvoidOnyxiaEggs(true, true, IsInOnyxiaEggZone(bot->GetPositionX(), bot->GetPositionY()),
                                 FindOnyxiaEggInDanger(ai, bot) != nullptr);
}

bool OnyxiaAirSpreadTrigger::IsActive()
{
    OnyxiaAirSpreadAction action(ai);
    return action.isUseful();
}

bool OnyxiaBreathTrigger::IsActive()
{
    OnyxiaBreathEscapeAction action(ai);
    return action.isUseful();
}

bool OnyxiaLandingTankTrigger::IsActive()
{
    OnyxiaLandingAttackAction action(ai);
    return action.isUseful();
}
