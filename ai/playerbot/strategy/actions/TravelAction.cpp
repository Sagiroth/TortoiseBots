
#include "playerbot/playerbot.h"
#include "../../runtime/PlayerbotAIStorage.h" // Headless storage shim
#include "TravelAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"
#include "playerbot/TravelMgr.h"


using namespace ai;
using namespace MaNGOS;

bool TravelAction::Execute(Event& event)
{
    TravelTarget * target = AI_VALUE(TravelTarget *, "travel target");

    target->CheckStatus();

    SET_AI_VALUE2(time_t, "manual time", "next travel check", time(0) + 5);

    return false;
}

bool TravelAction::isUseful()
{
    if (!AI_VALUE(bool,"travel target active"))
        return false;

    if (bot->GetGroup() && !bot->GetGroup()->IsLeader(bot->getObjectGuid()))
        if (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT))
            return false;

    if (sServerFacade.isMoving(bot))
        return false;

    if (AI_VALUE2(time_t, "manual time", "next travel check") > time(0))
        return false;

    TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");
    if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_WORK)
        return true;

    if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_COOLDOWN)
        return true;

    if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_READY && !urand(0, 20))
        return true;

    return false;
}
