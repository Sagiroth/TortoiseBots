
#include "playerbot/playerbot.h"
#include "PriestTriggers.h"
#include "PriestActions.h"

using namespace ai;

bool InnerFireTrigger::IsActive()
{
    return BuffTrigger::IsActive();
}

bool ShadowformTrigger::IsActive()
{
    return ai->HasSpell("shadowform") && !ai->HasAura("shadowform", bot);
}

// Touch of Weakness and Shadowguard overwrite each other: both are priest
// family 6 upkeep buffs with an identical effect signature (effect 6 APPLY_AURA,
// aura 42 PROC_TRIGGER_SPELL, misc 0, item type 0), so the core's generic
// non-stacking rule removes one when the other lands. Stand down while the
// sibling is up, or each upkeep trigger re-fires forever.
bool TouchOfWeaknessTrigger::IsActive()
{
    return BuffTrigger::IsActive() && !ai->HasAura("shadowguard", bot);
}

bool ShadowguardTrigger::IsActive()
{
    return BuffTrigger::IsActive() && !ai->HasAura("touch of weakness", bot);
}
bool FearWardOnTankTrigger::IsActive()
{
    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
    if (!spellId || !sServerFacade.IsSpellReady(bot, spellId))
        return false;

    return BuffOnTankTrigger::IsActive();
}
