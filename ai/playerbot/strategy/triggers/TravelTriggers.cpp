
#include "playerbot/playerbot.h"
#include "TravelTriggers.h"

#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/ServerFacade.h"
using namespace ai;

bool HasNearbyQuestTakerTrigger::IsActive()
{
    TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");
    if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_WORK) //We are not currently working on a target.
        return false;

    if (target->GetExpiredTime() < 2 * MINUTE) //The target was set more than 2 minutes ago.
        return false;

    return AI_VALUE(bool, "has nearby quest taker");
}
