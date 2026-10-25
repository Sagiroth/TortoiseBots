
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
    // Prefer a Heal we can interrupt now, not an earlier priest outside the
    // actual interrupt's range. The generic scan below keeps its old fallback.
    if (bot->GetMapId() == 409 && bot->IsAlive() && bot->IsInCombat() &&
        ai->HasStrategy("sulfuron interrupts", BotState::BOT_STATE_COMBAT))
    {
        bool fightingSulfuron = false;
        for (ObjectGuid const& guid : attackers)
        {
            Unit* boss = ai->GetUnit(guid);
            if (boss && boss->GetEntry() == 12098 && boss->IsAlive() &&
                boss->IsInCombat() && boss->IsInMap(bot)) fightingSulfuron = true;
        }
        if (fightingSulfuron)
        {
            for (ObjectGuid const& guid : attackers)
            {
                Unit* priest = ai->GetUnit(guid);
                if (!priest || priest == target || priest->GetEntry() != 11662 ||
                    !priest->IsAlive() || !priest->IsInCombat() || !priest->IsInMap(bot) ||
                    !bot->IsWithinLOSInMap(priest) || ccGuard.IsHeldCcMark(priest)) continue;
                Spell* heal = priest->GetCurrentSpell(CURRENT_GENERIC_SPELL);
                if (!heal || heal->m_spellInfo->Id != 19775 || heal->getState() != SPELL_STATE_PREPARING ||
                    heal->m_spellInfo->PreventionType != SPELL_PREVENTION_TYPE_SILENCE ||
                    !(heal->m_spellInfo->InterruptFlags & SPELL_INTERRUPT_FLAG_DAMAGE) ||
                    !ai->IsInterruptableSpellCasting(priest, spell, true)) continue;
                SpellCastResult result = SPELL_FAILED_NOT_READY;
                if (ai->CanCastSpell(spell, priest, 0, nullptr, false, false, false, &result) &&
                    result == SPELL_CAST_OK) return priest;
            }
        }
    }

    for (std::list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); ++i)
    {
        Unit* unit = ai->GetUnit(*i);
        if (!unit || unit == target)
            continue;

        if (sServerFacade.getDistance2d(bot, unit) > ai->GetRange("spell"))
            continue;

        if (!ai->IsInterruptableSpellCasting(unit, spell, true))
            continue;

        // A real heal only: "positive" also covers self buffs such as Lightning Shield.
        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        bool healing = spell && PlayerbotAI::IsHealSpell(spell->m_spellInfo);
        if (!healing)
        {
            spell = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
            healing = spell && PlayerbotAI::IsHealSpell(spell->m_spellInfo);
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
