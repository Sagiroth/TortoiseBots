
#include "playerbot/playerbot.h"
#include "SetAvoidAreaAction.h"

#include "playerbot/strategy/values/PositionValue.h"
#include "Maps/PathFinder.h"
using namespace ai;


bool SetAvoidAreaAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : ai->GetMaster();
    ai->TellError(requester, "Mob avoidance is unavailable: the pinned Tortoise core does not expose a path area filter.",
        PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true);
    return false;
}

bool SetAvoidAreaAction::isUseful()
{
    return false;
}
