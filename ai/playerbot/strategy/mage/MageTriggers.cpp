
#include "playerbot/playerbot.h"
#include "MageTriggers.h"
#include "MageActions.h"

using namespace ai;

bool AnyMageArmorTrigger::IsActive()
{
    Unit* target = GetTarget();
    return !ai->HasAura("ice armor", target) &&
           !ai->HasAura("frost armor", target) &&
           !ai->HasAura("mage armor", target);
}

bool MageArmorTrigger::IsActive()
{
    Unit* target = GetTarget();
    return !ai->HasAura("mage armor", target);
}

bool IceArmorTrigger::IsActive()
{
    Unit* target = GetTarget();
    return !ai->HasAura("ice armor", target) &&
           !ai->HasAura("frost armor", target);
}

bool ManaShieldTrigger::IsActive()
{
    return !ai->HasAura("mana shield", bot) && AI_VALUE2(uint8, "mana", "self target") > sPlayerbotAIConfig.mediumMana;
}

bool NoImprovedScorchDebuffTrigger::IsActive()
{
    if (bot->HasSpell(11095) || bot->HasSpell(12872) || bot->HasSpell(12873))
    {
        return DebuffTrigger::IsActive();
    }

    return false;
}

bool ArcanePowerTrigger::IsActive()
{
    // Turtle 1.18.1 Arcane Power (12042) drains max mana every second for its
    // full duration and kills the caster below 10% mana. The aura has
    // SPELL_ATTR_CANT_CANCEL ("cannot be cancelled"), so no ongoing rescue by
    // aura removal is possible. Gate activation conservatively; the action
    // repeats the same mana floor as defense in depth.
    if (!BuffTrigger::IsActive())
        return false;

    if (!ai->IsStateActive(BotState::BOT_STATE_COMBAT))
        return false;

    // Require a live enemy target so the risk buys damage, not idle drain.
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target || !target->IsAlive())
        return false;

    // 20s duration at ~1% max mana/s drain = ~20% plus cast costs under
    // reduced regen. 70% start keeps the bot above the 10% lethal floor
    // through a normal burst window. Revisit only with measured mana data.
    if (!AI_VALUE2(bool, "has mana", "self target"))
        return false;

    return AI_VALUE2(uint8, "mana", "self target") >= 70;
}

bool IciclesTrigger::IsActive()
{
    // Turtle 52516 (ranks 52516/51991/51995/51997) roots the caster while
    // channeling icicle bolts at the target; incoming damage shatters the
    // prison 75% of the time for 30% of base health (spell_mage.cpp
    // spell_mage_icicles_root). Only start the channel on a durable target
    // while nothing is attacking the bot and health leaves shatter margin.
    if (!SpellCanBeCastedTrigger::IsActive())
        return false;

    Unit* target = GetTarget();
    if (!target || !target->IsAlive())
        return false;

    if (AI_VALUE2(uint8, "health", "current target") <= 30)
        return false;

    if (AI_VALUE2(uint8, "health", "self target") < 60)
        return false;

    return AI_VALUE(uint8, "my attacker count") == 0;
}

bool IciclesChannelCheckTrigger::IsActive()
{
    if (Spell* spell = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
    {
        if (spell->m_spellInfo)
        {
            uint32 id = spell->m_spellInfo->Id;
            if (id == 52516 || id == 51991 || id == 51995 || id == 51997)
            {
                Unit* target = AI_VALUE(Unit*, "current target");
                return !target || !target->IsAlive();
            }
        }
    }
    return false;
}

bool EvocationChannelCheckTrigger::IsActive()
{
    if (Spell* spell = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
    {
        if (spell->m_spellInfo && spell->m_spellInfo->Id == 12051)
        {
            // Evocation restores mana; channeling at full mana wastes the
            // vulnerable window. Target-death does not apply (self channel).
            if (!AI_VALUE2(bool, "has mana", "self target"))
                return false;
            return AI_VALUE2(uint8, "mana", "self target") >= 95;
        }
    }
    return false;
}
