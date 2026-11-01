#include "playerbot/playerbot.h"
#include "HoldKillBlockersValue.h"
#include <cstdlib>
#include "playerbot/BossTacticsPolicy.h"

using namespace ai;

// Living creatures of the qualifier's entry within kHoldKillRangeYd of the bot, by grid search (the core's
// AllCreaturesOfEntryInRange through the world object's entry-in-grid list). Never the bots' "attackers" list: the
// Portal of Madness 15141 a dead Mad Servant leaves is unselectable (unit_flags 33555200) and never attacks, so it would
// never show up there. A blocker counts while it fights, or while it can never fight at all: a loose Voidwalker that has
// not woken yet is not a blocker yet, the portal is.
uint32 HoldKillBlockersValue::Calculate()
{
    const uint32 entry = static_cast<uint32>(std::strtoul(qualifier.c_str(), nullptr, 10));
    if (!entry || !bot)
        return 0;
    std::list<Creature*> found;
    bot->GetCreatureListWithEntryInGrid(found, entry, kHoldKillRangeYd);
    uint32 count = 0;
    for (Creature* creature : found)
    {
        if (!creature->IsAlive())
            continue;
        if (CountsAsBlocker(creature->IsInCombat(), creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE)))
            ++count;
    }
    return count;
}
