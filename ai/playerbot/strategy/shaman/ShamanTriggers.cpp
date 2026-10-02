
#include "playerbot/playerbot.h"
#include "ShamanTriggers.h"
#include "ShamanActions.h"

using namespace ai;

std::list<std::string> ShamanWeaponTrigger::spells;

bool ShamanWeaponTrigger::IsActive()
{
    // Upkeep must survive every level: before this the strategies queued one
    // fixed imbue (windfury for enhancement, flametongue elsewhere) while this
    // trigger stayed true on any known imbue, so the cast failed every tick
    // below its gate (issue #406: windfury is gated at 30). A queued imbue the
    // bot never trained also has no spell id, so the trigger now requires a
    // trained spell it can actually cast - the newest known rank, since
    // HasSpell resolves the highest rank - and skips unknown imbues.
    if (spells.empty())
    {
        spells.push_back("windfury weapon");
        spells.push_back("flametongue weapon");
        spells.push_back("frostbrand weapon");
        spells.push_back("rockbiter weapon");
    }

    for (std::list<std::string>::iterator i = spells.begin(); i != spells.end(); ++i)
    {
        std::string const& spell = *i;

        if (!ai->HasSpell(spell))
            continue;

        uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
        if (!spellId)
            continue;

        if (AI_VALUE2(Item*, "item for spell", spellId))
            return true;
    }

    return false;
}

bool ShockTrigger::IsActive()
{
    if (!ai->HasSpell("earth shock") && !ai->HasSpell("flame shock") && !ai->HasSpell("frost shock"))
        return false;

    return SpellTrigger::IsActive() && !ai->HasAnyAuraOf(GetTarget(), "frost shock", "earth shock", "flame shock", NULL) && !HasMaxDebuffs();
}
