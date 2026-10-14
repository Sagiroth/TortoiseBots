
#include "playerbot/playerbot.h"
#include "StayActions.h"

#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/LastMovementValue.h"

using namespace ai;

bool StayActionBase::Stay(Player* requester)
{
    AI_VALUE(LastMovement&, "last movement").Set(NULL);

    //if (!urand(0, 10)) ai->PlaySound(TEXTEMOTE_YAWN);

    MotionMaster &mm = *bot->GetMotionMaster();
#ifdef CMANGOS
	if (mm.GetCurrentMovementGeneratorType() == TAXI_MOTION_TYPE || bot->IsTaxiFlying())
#endif
#ifdef MANGOS
	if (mm.GetCurrentMovementGeneratorType() == FLIGHT_MOTION_TYPE || bot->IsFlying())
#endif
	{
		return false;
	}

    uint32 sitDelay = sPlayerbotAIConfig.sitDelay / 1000;
    time_t stayTime = AI_VALUE(time_t, "stay time");
    time_t now = time(0);
    if (!stayTime)
    {
        stayTime = now - urand(0, sitDelay / 2);
        context->GetValue<time_t>("stay time")->Set(stayTime);
    }

    // Already standing still: the bookkeeping above is the whole job, and
    // the bot is staying as asked, so this is a success, not a failure.
    if (!sServerFacade.isMoving(bot))
        return true;

    ai->StopMoving();
    bot->ClearUnitState(UNIT_STAT_CHASE);
    bot->ClearUnitState(UNIT_STAT_FOLLOW);

    return true;
}

bool StayAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    return Stay(requester);
}

bool StayAction::isUseful()
{
    if (AI_VALUE2(bool, "moving", "self target"))
        return false;

    // Standing still, Stay() only has work while the idle clock ("stay
    // time", read by the sit/return triggers) is not running yet, a follow
    // target or path is still remembered, or the unit has not actually
    // stopped. Otherwise it is a no-op: letting it run anyway reported
    // "stay - FAILED" on every idle tick of a staying bot.
    LastMovement& lastMove = AI_VALUE(LastMovement&, "last movement");
    return !AI_VALUE(time_t, "stay time") || lastMove.lastFollow || !lastMove.lastPath.empty() ||
        sServerFacade.isMoving(bot);
}

bool SitAction::Execute(Event& event)
{
    if (sServerFacade.isMoving(bot))
        return false;

    bot->SetStandState(UNIT_STAND_STATE_SIT);
    return true;
}

bool SitAction::isUseful()
{
    return !AI_VALUE2(bool, "moving", "self target");
}
