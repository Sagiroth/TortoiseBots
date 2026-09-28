
#include "playerbot/playerbot.h"
#include "LootTriggers.h"
#include "playerbot/LootObjectStack.h"
#include "playerbot/PlayerbotAIConfig.h"

#include "playerbot/ServerFacade.h"
using namespace ai;

bool LootAvailableTrigger::IsActive()
{
    if (AI_VALUE2(bool, "combat", "self target") || AI_VALUE2(bool, "mounted", "self target"))
        return false;

    if (!AI_VALUE(bool, "has available loot"))
        return false;

    // Loot every corpse this bot (or its group) killed and may loot before
    // pulling the next mob, like a player. Fire the selection action ("loot")
    // when no corpse is picked yet, or the picked one can no longer be looted
    // or reached (looted by someone else, despawned, expired, or abandoned
    // after a failed approach): "loot" then picks the nearest corpse still on
    // the stack, so the chain repairs itself instead of stalling.
    //
    // The old "already within INTERACTION_DISTANCE, or no hostile targets
    // around" gate starved the chain in mob-dense grind zones: "attack
    // anything" (GrindingStrategy, 5.0) grabbed the next mob and the bot walked
    // off, and a selected corpse that went stale was never replaced, so the
    // "far from current loot" -> "move to loot" path (7.0) could not run.
    // While a valid target is selected, those two actions walk to and open it,
    // so re-selecting the target every tick is neither needed nor desirable.
    LootObject lootTarget = AI_VALUE(LootObject, "loot target");
    return lootTarget.IsEmpty() || !lootTarget.IsLootPossible(bot);
}

bool FarFromCurrentLootTrigger::IsActive()
{
    LootObject loot = AI_VALUE(LootObject, "loot target");

    if (!loot.IsLootPossible(bot))
        return false;

    // Abandon loot the bot cannot safely reach without breaking its follow leash.
    // "move to loot" runs at priority 7 but "out of free move range" fires "follow" at
    // ACTION_HIGH (20). A corpse is only reachable without oscillation if the master is
    // within followDistance + GetMaxLootDistance of it — otherwise the bot must leave the
    // leash to reach the corpse and follow immediately wins, causing a yo-yo.
    Player* master = ai->GetMaster();
    if (master && master != bot)
    {
        Creature* creature = ai->GetCreature(loot.guid);
        if (creature && sServerFacade.GetDeathState(creature) == CORPSE)
        {
            float safeRange = sPlayerbotAIConfig.lootDistance;
            if (sServerFacade.getDistance2d(master, creature) > safeRange)
                return false;
        }
    }

    // Must agree with OpenLootAction::DoLoot's loot-range rule, or the bot deadlocks: for a
    // creature corpse the server validates loot with a 3D distance check (Player::SendLoot),
    // but the plain "distance" value here is 2D and ignores Z. On sloped ground the bot read
    // "close enough" (2D <= 5) and so never fired "move to loot", yet "open loot" failed the
    // 3D gate -> it sat a few yards off a corpse it could not loot. Use the same 3D rule so
    // the bot keeps approaching until it is genuinely on the corpse, then loots.
    if (Creature* creature = PlayerbotAIStorage::Instance().GetAI(bot)->GetCreature(loot.guid))
    {
        if (sServerFacade.GetDeathState(creature) == CORPSE)
            return !creature->IsWithinDistInMap(bot, bot->GetMaxLootDistance(creature), true, SizeFactor::None);
    }

    return AI_VALUE2(float, "distance", "loot target") > INTERACTION_DISTANCE;
}

bool CanLootTrigger::IsActive()
{
    return AI_VALUE(bool, "can loot");
}
