
#include "playerbot/playerbot.h"
#include "MageActions.h"

using namespace ai;

bool CastBlinkAction::isUseful()
{
    Map* map = bot->GetMap();
    if (map && map->IsDungeon() && ai->HasStrategy("no blink instance", BotState::BOT_STATE_COMBAT))
        return false;
    return CastBuffSpellAction::isUseful();
}

bool CastBlinkBackAction::Execute(Event& event)
{
    Unit* threat = AI_VALUE(Unit*, "current target");
    if (!threat || !threat->IsAlive())
        return false;

    // Stop first: a running spline, or a chase generator (it turns a player
    // back toward its target once its spline is done), can overwrite the
    // facing before the queued Blink fires on the next spell update. Then
    // face directly away from the threat; force sets the orientation at once
    // (and sends a heartbeat) instead of a timed spline turn.
    ai->StopMoving();

    float away = bot->GetAngle(threat) + M_PI_F;
    if (away >= 2.0f * M_PI_F)
        away -= 2.0f * M_PI_F;
    sServerFacade.SetFacingTo(bot, away, true);

    return CastBuffSpellAction::Execute(event);
}
