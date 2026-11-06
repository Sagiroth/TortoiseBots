
#include "playerbot/playerbot.h"
#include "playerbot/strategy/generic/PullStrategy.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/values/FreeMoveValues.h"
#include "PositionAction.h"
#include "BossTacticsActions.h"
#include "playerbot/strategy/values/LastMovementValue.h"
#include "playerbot/ServerFacade.h"
#include "Movement/spline/MoveSpline.h"
#include "../../../../runtime/StayArrivalPolicy.h"

using namespace ai;

void TellPosition(PlayerbotAI* ai, Player* requester, std::string name, ai::PositionEntry pos)
{
    std::ostringstream out; out << "Position " << name;
    if (pos.isSet())
    {
        float x = pos.x, y = pos.y;
        Map2ZoneCoordinates(x, y, ai->GetBot()->getZoneId());
        out << " is set to " << x << "," << y;
    }
    else
        out << " is not set";
    ai->TellPlayer(requester, out);
}

bool PositionAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string param = event.GetParam();
	if (param.empty())
		return false;

    if (!requester)
        return false;

    ai::PositionMap& posMap = context->GetValue<ai::PositionMap&>("position")->Get();
    if (param == "?")
    {
        for (ai::PositionMap::iterator i = posMap.begin(); i != posMap.end(); ++i)
        {
            if (i->second.isSet())
                TellPosition(ai, requester, i->first, i->second);
        }
        return true;
    }

    std::vector<std::string> params = split(param, ' ');
    if (params.size() != 2)
    {
        ai->TellPlayer(requester, "Whisper position <name> ?/set/reset");
        return false;
    }

    std::string name = params[0];
    std::string action = params[1];
	ai::PositionEntry pos = posMap[name];
	if (action == "?")
	{
	    TellPosition(ai, requester, name, pos);
	    return true;
	}

    std::vector<std::string> coords = split(action, ',');
    if (coords.size() == 3)
    {
        pos.Set(atoi(coords[0].c_str()), atoi(coords[1].c_str()), atoi(coords[2].c_str()), ai->GetBot()->GetMapId());
        posMap[name] = pos;

        std::ostringstream out; out << "Position " << name << " is set";
        ai->TellPlayer(requester, out);
        return true;
    }

	if (action == "set")
	{
        pos.Set(bot->getPositionX(), bot->getPositionY(), bot->getPositionZ(), ai->GetBot()->GetMapId());
	    posMap[name] = pos;

	    std::ostringstream out; out << "Position " << name << " is set";
	    ai->TellPlayer(requester, out);
	    return true;
	}

	if (action == "reset")
	{
	    pos.Reset();
	    posMap[name] = pos;

	    std::ostringstream out; out << "Position " << name << " is reset";
	    ai->TellPlayer(requester, out);
	    return true;
	}

    return false;
}

bool MoveToPositionAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
	ai::PositionEntry pos = context->GetValue<ai::PositionMap&>("position")->Get()[qualifier];
    if (!pos.isSet())
    {
        std::ostringstream out; out << "Position " << qualifier << " is not set";
        ai->TellPlayer(requester, out);
        return false;
    }

    bool const moved = MoveTo(bot->GetMapId(), pos.x, pos.y, pos.z, idle);
    // The run leader reads a lead bot whose anchor walk keeps failing (MoveFailNotePolicy.h PositionMoveFailing);
    // the low bit keeps a failure at ms 0 apart from "none".
    AI_VALUE(LastMovement&, "last movement").positionMoveFailMs = moved ? 0 : (WorldTimer::getMSTime() | 1u);
    return moved;
}

bool MoveToPositionAction::isUseful()
{
    ai::PositionEntry pos = context->GetValue<ai::PositionMap&>("position")->Get()[qualifier];
    float distance = AI_VALUE2(float, "distance", std::string("position_") + qualifier);
    return pos.isSet() && distance > ai->GetRange("follow") && ai->CanMove();
}

bool GuardAction::isUseful()
{
    if (ai->IsStateActive(BotState::BOT_STATE_COMBAT))
    {
        Unit* target = AI_VALUE(Unit*, "current target");

        if (!target)
            return true;

        if (target->GetVictim() == bot) //Try pulling target to guard position
            return true;

        if (!ai->IsRanged(bot)) //Melee bots stay in melee.
            return false;

        WorldPosition formationPosition = AI_VALUE(WorldPosition, "formation position");

        if (formationPosition.sqDistance2d(target) > ai->GetRange("spell")) //Do not move to guard if we can't attack from that position.
            return false;
    }
    else
    {
        if (AI_VALUE(GuidPosition, "rpg target") && CanFreeMoveValue::CanFreeMoveTo(ai, AI_VALUE(GuidPosition, "rpg target")))
        {
            return false;
        }
    }

    return true;
}

bool SetReturnPositionAction::Execute(Event& event)
{
    ai::PositionMap& posMap = context->GetValue<ai::PositionMap&>("position")->Get();
    ai::PositionEntry returnPos = posMap["return"];
    ai::PositionEntry randomPos = posMap["random"];
    if (returnPos.isSet() && !randomPos.isSet())
    {
        float angle = 2 * M_PI * urand(0, 1000) / 100.0f;
        float dist = ai->GetRange("follow") * urand(0, 1000) / 1000.0f;
        float x = returnPos.x + cos(angle) * dist,
             y = returnPos.y + sin(angle) * dist,
             z = bot->getPositionZ();
        bot->UpdateAllowedPositionZ(x, y, z);

        if (!bot->IsWithinLOS(x, y, z, true))
            return false;

        randomPos.Set(x, y, z, bot->GetMapId());
        posMap["random"] = randomPos;
        return true;
    }
    return false;
}

bool SetReturnPositionAction::isUseful()
{
    ai::PositionMap& posMap = context->GetValue<ai::PositionMap&>("position")->Get();
    return posMap["return"].isSet() && !posMap["random"].isSet();
}


bool ReturnAction::isUseful()
{
    ai::PositionEntry pos = context->GetValue<ai::PositionMap&>("position")->Get()[qualifier];
    return pos.isSet() && AI_VALUE2(float, "distance", "position_random") > ai->GetRange("follow");
}

bool ReturnToStayPositionAction::isPossible()
{
    PositionMap& posMap = AI_VALUE(PositionMap&, "position");
    PositionEntry stayPosition = posMap["stay"];
    if (stayPosition.isSet())
    {
        const float distance = bot->GetDistance(stayPosition.x, stayPosition.y, stayPosition.z);
        if (distance > sPlayerbotAIConfig.reactDistance)
        {
            ai->TellError(GetMaster(), "The stay position is too far to return. I am going to stay where I am now");

            // Set the stay position to current position
            stayPosition.Set(bot->getPositionX(), bot->getPositionY(), bot->getPositionZ(), bot->GetMapId());
            posMap["stay"] = stayPosition;
        }

        return true;
    }

    return false;
}

// The stay walk is due (StayWalkDue): the bot is beyond the follow range of its stay
// point in raw 3D (StayPointReached: no bounding radius, no rounding) and the walk does
// not rest after failed walks. Judges the walk under way first: where it took the bot,
// whether the bot still moves. The walk's isUseful and its trigger both ask this.
// Crescent Grove r26 step 272: the old 2D rule minus the bounding radius refused a
// tauren lead bot 5.67 yd from its point on the ground and 6.6 yd below it while the
// trigger fired every tick.
// The point the stay walk goes to: the stay itself, or under a live boss anchor its point on the anchor's circle (the
// main tank's stay is the lead's approach stand or a landed pullback's anchor, and the boss follows the tank there).
// False: the clamped point has no floor on the anchor's level; the stay is left as it is and the walk to it is refused,
// so the walk's back-off rests it.
bool ai::StayWalkPoint(PlayerbotAI* ai, Player* bot, PositionEntry& stay)
{
    float x = stay.x;
    float y = stay.y;
    float z = stay.z;
    if (!ClampMoveToBossAnchor(ai, bot, x, y, z))
        return false;
    stay.x = x;
    stay.y = y;
    stay.z = z;
    return true;
}

bool ai::StayWalkDueNow(PlayerbotAI* ai, Player* bot, PositionEntry const& stayPosition)
{
    PositionEntry point = stayPosition;
    StayWalkPoint(ai, bot, point);
    TortoiseBots::StayWalkBackoff& backoff = ai->GetAiObjectContext()->GetValue<LastMovement&>("last movement")->Get().stayWalk;
    TortoiseBots::StayWalkTrack(backoff, point.x, point.y, point.z, point.mapId);
    float const dx = bot->GetPositionX() - point.x;
    float const dy = bot->GetPositionY() - point.y;
    float const dz = bot->GetPositionZ() - point.z;
    bool const moving = sServerFacade.isMoving(bot) || !bot->movespline->Finalized();
    return TortoiseBots::StayWalkDue(backoff, TortoiseBots::StayPointReached(dx, dy, dz, ai->GetRange("follow")),
        TortoiseBots::StayPointDistance(dx, dy, dz), moving, bot->IsInCombat(), bot->GetTransport() != nullptr, WorldTimer::getMSTime());
}

bool ReturnToStayPositionAction::isUseful()
{
    PositionEntry stayPosition = AI_VALUE(PositionMap&, "position")["stay"];
    if (!stayPosition.isSet() || stayPosition.mapId != bot->GetMapId() || !ai->CanMove())
        return false;
    return StayWalkDueNow(ai, bot, stayPosition);
}

// The walk's dispatch feeds the back-off (StayWalkDispatched): a refused walk failed, an
// accepted one starts an attempt that StayWalkDueNow judges by where it takes the bot.
bool ReturnToStayPositionAction::Execute(Event& event)
{
    (void)event;
    PositionEntry stayPosition = AI_VALUE(PositionMap&, "position")["stay"];
    if (!stayPosition.isSet())
        return false;
    // The walk's point is the trigger's (StayWalkDueNow): the stay, or its clamp under a live boss anchor.
    PositionEntry point = stayPosition;
    bool const pointOk = StayWalkPoint(ai, bot, point);
    bool const moved = pointOk && MoveTo(bot->GetMapId(), point.x, point.y, point.z, idle);
    AI_VALUE(LastMovement&, "last movement").positionMoveFailMs = moved ? 0 : (WorldTimer::getMSTime() | 1u);
    TortoiseBots::StayWalkBackoff& backoff = AI_VALUE(LastMovement&, "last movement").stayWalk;
    TortoiseBots::StayWalkTrack(backoff, point.x, point.y, point.z, point.mapId);
    TortoiseBots::StayWalkDispatched(backoff, moved, TortoiseBots::StayPointDistance(bot->GetPositionX() - point.x,
        bot->GetPositionY() - point.y, bot->GetPositionZ() - point.z), bot->IsInCombat(),
        bot->GetTransport() != nullptr, WorldTimer::getMSTime());
    return moved;
}

bool ReturnToPullPositionAction::isPossible()
{
    PositionMap& posMap = AI_VALUE(PositionMap&, "position");
    PositionEntry pullPosition = posMap["pull"];
    PullStrategy* strategy = PullStrategy::Get(ai);
    return pullPosition.isSet() && pullPosition.mapId == bot->GetMapId() &&
        strategy && strategy->HasPullActionCompleted();
}
