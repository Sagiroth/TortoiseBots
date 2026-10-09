
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

bool InnerFocusForHealTrigger::IsActive()
{
    // Untalented bots (low level, holy without the 21-point disc talent)
    // must never queue this; BuffTrigger's own HasSpell gate is bypassed
    // because this trigger carries the mana + heal-target conditions.
    if (!ai->HasSpell("inner focus"))
        return false;

    uint32 spellId = AI_VALUE2(uint32, "spell id", "inner focus");
    if (!spellId || !sServerFacade.IsSpellReady(bot, spellId))
        return false;

    // Buff already up: nothing to pre-cast.
    if (ai->HasAura("inner focus", bot))
        return false;

    if (!(AI_VALUE2(bool, "has mana", "self target") &&
        AI_VALUE2(uint8, "mana", "self target") < sPlayerbotAIConfig.mediumMana))
        return false;

    Unit* healTarget = AI_VALUE(Unit*, "party member to heal");
    if (!healTarget || !healTarget->IsAlive())
        return false;

    return AI_VALUE2(uint8, "health", "party member to heal") < sPlayerbotAIConfig.mediumHealth;
}
