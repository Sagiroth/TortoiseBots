
#include "playerbot/playerbot.h"
#include "WarlockTriggers.h"
#include "WarlockActions.h"

using namespace ai;

bool DemonArmorTrigger::IsActive()
{
	if (!ai->HasSpell("demon skin") && !ai->HasSpell("demon armor"))
		return false;

	Unit* target = GetTarget();
	return !ai->HasAura("demon skin", target) &&
			   !ai->HasAura("demon armor", target);
}

bool SpellstoneTrigger::IsActive()
{
    return BuffTrigger::IsActive() && AI_VALUE2(uint32, "item count", getName()) > 0;
}

bool InfernoTrigger::IsActive()
{
	return AI_VALUE(uint8, "attackers count") > 1 && bot->HasSpell(1122) && bot->HasItemCount(5565, 1) && !urand(0, 2);
}

bool CorruptionTrigger::IsActive()
{
	if (!ai->HasSpell("corruption"))
		return false;

	Unit* target = GetTarget();
	return target && !ai->HasAura("corruption", target) && !HasMaxDebuffs();
}

bool CorruptionOnAttackerTrigger::IsActive()
{
    return DebuffOnAttackerTrigger::IsActive();
}

bool LifeTapTrigger::IsActive()
{
	if (!ai->HasSpell("life tap"))
		return false;

	const uint32 mana = AI_VALUE2(uint8, "mana", "self target");
	if (mana <= sPlayerbotAIConfig.lowMana)
	{
		const uint32 health = AI_VALUE2(uint8, "health", "self target");
		if (health > sPlayerbotAIConfig.lowHealth)
		{
			return true;
		}
	}

	return false;
}

bool DrainSoulTrigger::IsActive()
{
	if (!ai->HasSpell("drain soul"))
		return false;

	// If no item cheats enabled
    if (!ai->HasCheat(BotCheatMask::item))
    {
		// Check if it has less than 5 soul shards
        if (!bot->HasItemCount(6265, 5))
        {
			// Check if it has enough bag space
			if (AI_VALUE(uint8, "bag space") > 0)
			{
                // Check if target health is less than 25% (was 15%)
                const uint32 targetHealth = AI_VALUE2(uint8, "health", "current target");
                if (targetHealth <= 25)
                {
                    return true;
                }
			}
        }
	}

	return false;
}

bool NoCurseTrigger::IsActive()
{
	if (!ai->HasSpell("curse of agony") &&
		!ai->HasSpell("curse of doom") &&
		!ai->HasSpell("curse of recklessness") &&
		!ai->HasSpell("curse of shadow") &&
		!ai->HasSpell("curse of the elements") &&
		!ai->HasSpell("curse of weakness") &&
		!ai->HasSpell("curse of tongues"))
		return false;

	Unit* target = GetTarget();
	if (target)
	{
		return !ai->HasAura("curse of agony", target, false, true) &&
			   !ai->HasAura("curse of doom", target, false, true) &&
			   !ai->HasAura("curse of recklessness", target, false, true) &&
			   !ai->HasAura("curse of shadow", target, false, true) &&
			   !ai->HasAura("curse of the elements", target, false, true) &&
			   !ai->HasAura("curse of weakness", target, false, true) &&
			   !ai->HasAura("curse of tongues", target, false, true);
	}

	return false;
}

bool NoCurseOnAttackerTrigger::IsActive()
{
	if (!ai->HasSpell("curse of agony") &&
		!ai->HasSpell("curse of doom") &&
		!ai->HasSpell("curse of recklessness") &&
		!ai->HasSpell("curse of shadow") &&
		!ai->HasSpell("curse of the elements") &&
		!ai->HasSpell("curse of weakness") &&
		!ai->HasSpell("curse of tongues"))
		return false;

    std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    for (std::list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); ++i)
    {
        Unit* attacker = ai->GetUnit(*i);
        if (attacker && attacker != currentTarget)
        {
			if (!ai->HasAura("curse of agony", attacker, false, true) &&
				!ai->HasAura("curse of doom", attacker, false, true) &&
				!ai->HasAura("curse of recklessness", attacker, false, true) &&
				!ai->HasAura("curse of shadow", attacker, false, true) &&
				!ai->HasAura("curse of the elements", attacker, false, true) &&
				!ai->HasAura("curse of weakness", attacker, false, true) &&
				!ai->HasAura("curse of tongues", attacker, false, true))
			{
				return true;
			}
        }
    }

	return false;
}

bool FearPvpTrigger::IsActive()
{
    Unit* target = AI_VALUE(Unit*, "current target");
	if (target && target->IsPlayer())
	{
		// Check if low health
        const uint8 health = AI_VALUE2(uint8, "health", "self target");
        if (health <= sPlayerbotAIConfig.lowHealth)
        {
			// Check if targeting bot
			if (target->GetVictim() == bot)
			{
                // Check if the bot has feared anyone
                bool alreadyFeared = false;
                std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
                for (std::list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); ++i)
                {
                    Unit* attacker = ai->GetUnit(*i);
                    if (ai->HasAura("fear", attacker, false, true))
                    {
                        alreadyFeared = true;
                        break;
                    }
                }

                if (!alreadyFeared)
                {
                    const float distance = target->GetDistance(bot);
                    return distance <= 10.0f;
                }
			}
		}
	}

	return false;
}

bool ConflagrateTrigger::IsActive()
{
	Unit* target = AI_VALUE(Unit*, "current target");
	if (target)
	{
		// Check if immolate in target
		Aura* aura = ai->GetAura("immolate", target, true);
		if (aura)
		{
			// Check if immolate is about to expire
			if (aura->GetAuraDuration() <= 7000)
			{
				return true;
			}
		}
	}

	return false;
}

bool DemonicSacrificeTrigger::IsActive()
{
	if (ai->HasStrategy("pet", BotState::BOT_STATE_COMBAT))
	{
		return ai->HasSpell(18788) &&
			   !ai->HasAura(18789, bot) && // Burning Wish (Imp)
			   !ai->HasAura(18790, bot) && // Fel Stamina (Voidwalker)
			   !ai->HasAura(18791, bot) && // Touch of Shadow (Succubus)
				   !ai->HasAura(18792, bot);   // Fel Energy (Felhunter)
	}

	return false;
}

bool SoulLinkTrigger::IsActive()
{
	return ai->HasSpell(19028) && !ai->HasAura(19028, bot) && AI_VALUE(Unit*, "pet target");
}

bool NoSpecificPetTrigger::IsActive()
{
    Unit* pet = AI_VALUE(Unit*, "pet target");
    if (pet)
    {
        return pet->GetEntry() != entry;
    }

    return true;
}

uint32 SoulstoneTrigger::GetItemId()
{
    uint32 itemId = 0;
    const uint32 level = bot->GetLevel();
    if (level >= 18 && level < 30)
    {
        itemId = 5232;
    }
    else if (level >= 30 && level < 40)
    {
        itemId = 16892;
    }
    else if (level >= 40 && level < 50)
    {
        itemId = 16893;
    }
    else if (level >= 50 && level < 60)
    {
        itemId = 16895;
    }
    else if (level >= 60)
    {
        itemId = 16896;
    }

    return itemId;
}

bool RainOfFireChannelCheckTrigger::IsActive()
{
    if (Spell* spell = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
    {
        if (spell->m_spellInfo)
        {
            uint32 id = spell->m_spellInfo->Id;
            if (id == 5740 || id == 6219 || id == 11677 || id == 11678 || id == 27212)
            {
                uint8 aoeCount = AI_VALUE(uint8, "aoe count");
                return aoeCount < 2;
            }
        }
    }
    return false;
}

namespace
{
uint8 OwnAfflictionDotsOn(PlayerbotAI* ai, Unit* target)
{
    // Mirrors the core Dark Harvest filter (spell_warlock.cpp
    // IsDarkHarvestAfflictionPeriodicAura): own periodic damage/leech from
    // the Affliction family. Drain Life/Soul are channels, not auras, so
    // only the four DoT auras count here.
    static const char* dots[] = { "corruption", "siphon life", "curse of agony", "curse of doom" };
    uint8 count = 0;
    for (const char* dot : dots)
        if (ai->HasAura(dot, target, false, true))
            ++count;
    return count;
}
}

bool DarkHarvestTrigger::IsActive()
{
    // Tortoise 52550: channeled DoT that accelerates own Affliction ticks AND
    // refunds its 30s cooldown when the target dies mid-channel. Starting it
    // with fewer than two own DoTs rolling wastes the window, so gate on it.
    if (!SpellCanBeCastedTrigger::IsActive())
        return false;

    Unit* target = GetTarget();
    if (!target || !target->IsAlive())
        return false;

    return OwnAfflictionDotsOn(ai, target) >= 2;
}

bool DarkHarvestChannelCheckTrigger::IsActive()
{
    if (Spell* spell = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
    {
        if (spell->m_spellInfo && spell->m_spellInfo->Id == 52550)
        {
            // A dying target refunds the cooldown: keep channeling it. Cancel
            // only when a LIVE target has lost every own DoT.
            Unit* target = AI_VALUE(Unit*, "current target");
            if (!target || !target->IsAlive())
                return false;
            return OwnAfflictionDotsOn(ai, target) == 0;
        }
    }
    return false;
}

bool PowerOverwhelmingTrigger::IsActive()
{
    // Tortoise 51714: pet burst (CC break + damage buff) costing the demon a
    // share of base health over the duration. Core CanCastSpell covers
    // cooldown/mana/range; the pet must be alive and healthy enough that the
    // health price cannot finish it, and a live enemy must justify burst.
    if (!SpellCanBeCastedTrigger::IsActive())
        return false;

    Unit* pet = AI_VALUE(Unit*, "pet target");
    if (!pet || !pet->IsAlive() || !pet->HealthAbovePct(60))
        return false;

    Unit* target = GetTarget();
    return target && target->IsAlive();
}
