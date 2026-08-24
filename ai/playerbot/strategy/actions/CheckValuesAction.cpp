
#include "playerbot/playerbot.h"
#include "CheckValuesAction.h"

#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"

#include "playerbot/TravelMgr.h"
#include "playerbot/TravelNode.h"
#include "playerbot/strategy/values/LastMovementValue.h"
using namespace ai;

CheckValuesAction::CheckValuesAction(PlayerbotAI* ai) : Action(ai, "check values")
{
}

bool CheckValuesAction::Execute(Event& event)
{
    if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
    {
        ai->Ping(bot->getPositionX() - 7.5, bot->getPositionY() + 7.5);

        LastMovement& lastMove = AI_VALUE(LastMovement&, "last movement");

        if (!lastMove.lastPath.empty())
            ai->Ping(lastMove.lastPath.GetFront().getX() - 7.5, lastMove.lastPath.GetFront().getY() + 7.5);
    }

    if (ai->HasStrategy("map", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("map full", BotState::BOT_STATE_NON_COMBAT))
    {
        sTravelNodeMap.manageNodes(bot, ai->HasStrategy("map full", BotState::BOT_STATE_NON_COMBAT));
    }

    std::list<ObjectGuid> possible_targets = AI_VALUE(std::list<ObjectGuid>, "possible targets");
    std::list<ObjectGuid> all_targets = AI_VALUE(std::list<ObjectGuid>, "all targets");
    std::list<ObjectGuid> npcs = AI_VALUE(std::list<ObjectGuid>, "nearest npcs");
    std::list<ObjectGuid> corpses = AI_VALUE(std::list<ObjectGuid>, "nearest corpses");
    std::list<ObjectGuid> gos = AI_VALUE(std::list<ObjectGuid>, "nearest game objects no los");
    std::list<ObjectGuid> nfp = AI_VALUE(std::list<ObjectGuid>, "nearest friendly players");

    return true;
}
