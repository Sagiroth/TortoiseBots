
#include "playerbot/playerbot.h"
#include "MageTriggers.h"
#include "MageActions.h"

using namespace ai;

bool BlinkBackTrigger::IsActive()
{
    // Blink spell id 1953; ready check keeps the escape for when it can run.
    if (!sServerFacade.IsSpellReady(bot, 1953))
        return false;
    // Rooted/stunned handled by the plain blink node above (EMERGENCY).
    if (bot->HasAuraType(SPELL_AURA_MOD_ROOT) || bot->HasAuraType(SPELL_AURA_MOD_STUN))
        return false;
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target || !target->IsAlive())
        return false;
    // Inside ~8 yd melee reach of the bot: blink away, then resume nuking.
    return sServerFacade.getDistance2d(bot, target) <= 8.0f;
}

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
    if (!ai->HasSpell("mana shield"))
        return false;

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
    // Tortoise 1.18.1 Arcane Power (12042) drains max mana every second for its
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
    // Tortoise 52516 (ranks 52516/51991/51995/51997) roots the caster while
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

bool BlizzardChannelCheckTrigger::IsActive()
{
    if (Spell* spell = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
    {
        if (spell->m_spellInfo)
        {
            // Per-rank Blizzard channel ids (effect spells): 10 rank 1,
            // 6141 rank 2, 8427 rank 3, 10185/10186/10187 ranks 4-6.
            // Matches mod-playerbots BlizzardChannelCheckTrigger ids.
            uint32 id = spell->m_spellInfo->Id;
            if (id == 10 || id == 6141 || id == 8427 ||
                id == 10185 || id == 10186 || id == 10187)
            {
                // Pack thinned below a blizzard's worth: stop channeling.
                return AI_VALUE(uint8, "attacker count") < 2;
            }
        }
    }
    return false;
}
