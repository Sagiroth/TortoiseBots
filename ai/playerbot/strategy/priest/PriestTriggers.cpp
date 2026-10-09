
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

    // Explicit orders win: a wardable manual buff target is served by the
    // generic row. With a non-empty list FearWardTrigger checks only manual
    // targets (no self fallback), so mirror that check here: stand down iff
    // a manual target would take the ward. (FearWardTrigger::IsActive is
    // private engine-side, hence the inline mirror of its list branch.)
    const std::list<ObjectGuid>& manualTargets = AI_VALUE(std::list<ObjectGuid>, "buff targets");
    if (!manualTargets.empty())
    {
        FearWardTrigger probe(ai);
        for (const ObjectGuid& guid : manualTargets)
        {
            if (probe.IsTargetValid(ai->GetUnit(guid)))
                return false;
        }
    }

    return BuffOnTankTrigger::IsActive();
}
