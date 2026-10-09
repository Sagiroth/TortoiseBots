
#include "playerbot/playerbot.h"
#include "AreaTriggerAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomBotFacade.h"

using namespace ai;

bool ReachAreaTriggerAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    uint32 triggerId;

    if (ai->IsRealPlayer()) //Do not trigger own area trigger.
        return false;

    WorldPacket p(event.GetPacket());
    p.rpos(0);
    p >> triggerId;

    AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(triggerId);
    if(!atEntry)
        return false;

    AreaTrigger const* at = sObjectMgr.GetAreaTrigger(triggerId);
    if (!at)
    {
        WorldPacket p1(CMSG_AREATRIGGER);
        p1 << triggerId;
        p1.rpos(0);
        bot->GetSession()->HandleAreaTriggerOpcode(p1);

        return true;
    }

    if (bot->GetMapId() != atEntry->mapid || bot->GetDistance(atEntry->x, atEntry->y, atEntry->z) > sPlayerbotAIConfig.sightDistance)
    {
        ai->TellError(requester, "I won't follow: too far away");
        return true;
    }

    MotionMaster &mm = *bot->GetMotionMaster();
	mm.MovePoint(atEntry->mapid, atEntry->x, atEntry->y, atEntry->z, FORCED_MOVEMENT_RUN);
    const float distance = bot->GetDistance(atEntry->x, atEntry->y, atEntry->z, SizeFactor::None);
    const float duration = 1000.0f * distance / bot->GetSpeed(MOVE_RUN) + sPlayerbotAIConfig.reactDelay;
    ai->TellError(requester, "Wait for me");
    SetDuration(duration);
    context->GetValue<LastMovement&>("last area trigger")->Get().lastAreaTrigger = triggerId;

    return true;
}



bool AreaTriggerAction::Execute(Event& event)
{
    LastMovement& movement = context->GetValue<LastMovement&>("last area trigger")->Get();

    uint32 triggerId = movement.lastAreaTrigger;
    movement.lastAreaTrigger = 0;

    AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(triggerId);
    if(!atEntry)
        return false;

    AreaTrigger const* at = sObjectMgr.GetAreaTrigger(triggerId);
    if (!at)
        return true;

    WorldPosition const before(bot);
    WorldPacket p(CMSG_AREATRIGGER);
    p << triggerId;
    p.rpos(0);
    bot->GetSession()->HandleAreaTriggerOpcode(p);

    // A refused teleport (missing item or level) leaves the bot standing in the
    // trigger, and the route planner kept sending it back: the Booty Bay
    // transpolyporter held 13 bots ~8 min each (live 2026-10-09).
    if (!bot->IsBeingTeleported() && bot->GetMapId() == before.GetMapId() && WorldPosition(bot).distance(before) < 1.0f)
    {
        // Masterless pool bots skip the requirement of an open-world exit
        // (transponder pads and the like) and ride it anyway; dungeon entrances
        // keep their level gate.
        AreaTriggerTeleport const* tele = sObjectMgr.GetAreaTriggerTeleport(triggerId);
        if (tele && WorldPosition(tele->destination.mapId, 0, 0, 0).isOverworld() &&
            !ai->HasRealPlayerMaster() && sRandomBotFacade.IsRandomBot(bot) && bot->IsAlive() &&
            bot->TeleportTo(tele->destination.mapId, tele->destination.x, tele->destination.y, tele->destination.z, tele->destination.o))
            return true;

        // Otherwise close the trigger for this bot for 30 min; TravelNodePath::getCost skips it.
        SET_AI_VALUE2(time_t, "manual time", "area trigger refused::" + std::to_string(triggerId), time(0) + 30 * MINUTE);
    }

    return true;
}
