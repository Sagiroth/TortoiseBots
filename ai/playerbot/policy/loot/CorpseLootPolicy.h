#pragma once

#include "playerbot/playerbot.h"

namespace ai
{
    // One rule for "this corpse still has body loot for this bot": the raw lootable flag
    // or the viewer's own loot right. The core masks the flag per viewer, so a corpse
    // holding only this bot's quest item can lack it while IsAllowedToLoot still says yes.
    inline bool CorpseHasBodyLootFor(Player* bot, Creature* creature)
    {
        return creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE) || bot->IsAllowedToLoot(creature);
    }
}
