
#include "playerbot/playerbot.h"
#include "EnemyHealerTargetValue.h"
#include "TargetValue.h"
#include "CcMarkHoldPolicy.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"

using namespace ai;

Unit* EnemyHealerTargetValue::Calculate()
{
    std::string spell = qualifier;

    std::list<ObjectGuid> attackers = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    Unit* target = ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
    CcGuard ccGuard(ai);
    for (std::list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); ++i)
    {
        Unit* unit = ai->GetUnit(*i);
        if (!unit || unit == target)
            continue;

        if (sServerFacade.getDistance2d(bot, unit) > ai->GetRange("spell"))
            continue;

        if (!ai->IsInterruptableSpellCasting(unit, spell, true))
            continue;

        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        bool healing = spell && IsPositiveSpell(spell->m_spellInfo);
        if (!healing)
        {
            spell = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
            healing = spell && IsPositiveSpell(spell->m_spellInfo);
        }
        if (!healing)
            continue;

        // A held CC mark (Moon or "rti cc", in its hold window or under a
        // CC cast bar) is the CC bot's: Kick and Shield Bash deal damage,
        // and the walk to it drags the bot onto the mark (a Stonevault
        // Oracle on Moon casts Healing Wave). Checked last: it only runs
        // for a mob that is healing right now.
        if (!CcEnemyHealerPickAllowed(ccGuard.IsHeldCcMark(unit)))
            continue;

        return unit;
    }

    return NULL;
}
