#include "playerbot/playerbot.h"
#include "ServiceNearbyNpcAction.h"

using namespace ai;

bool ServiceNearbyNpcAction::isUseful()
{
    return AI_VALUE(bool, "should service nearby npc");
}

bool ServiceNearbyNpcAction::Execute(Event& event)
{
    GuidPosition target = AI_VALUE(GuidPosition, "nearby service target");

    if (!target)
        return false;

    Creature* npc = target.GetCreature(bot->GetInstanceId());

    if (!npc || !npc->IsAlive())
        return false;

    if (!bot->IsWithinDistInMap(npc, INTERACTION_DISTANCE))
        return MoveNear(npc, INTERACTION_DISTANCE - 1.0f);

    // In range. The selector prefers a vendor whenever a sale is due, so a
    // vendor here means "sell first"; the trainer is then picked up on a later
    // tick, once the sale has funded the rank.
    if (target.HasNpcFlag(UNIT_NPC_FLAG_VENDOR))
        return ai->DoSpecificAction("sell", Event("rpg action", "vendor"), true);

    return ai->DoSpecificAction("trainer", Event("rpg action", target), true);
}
