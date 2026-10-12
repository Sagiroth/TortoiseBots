
#include "playerbot/playerbot.h"
#include "RogueTriggers.h"
#include "RogueActions.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"

using namespace ai;

bool RiposteCastTrigger::IsActive()
{
	Unit* target = GetTarget();
	if (!target)
		return false;

	bool isMelee = true;
	if (target->IsPlayer())
	{
		isMelee = !ai->IsRanged((Player*)target);
	}

	return SpellCanBeCastedTrigger::IsActive() && isMelee;
}

bool SurpriseAttackTrigger::IsActive()
{
	// Tortoise 52511 requires the reactive dodge target (core OnCheckCast
	// enforces REACTIVE_ROGUE_DODGE); mirror the Riposte melee sanity so a
	// queued proc is not wasted on a ranged target.
	Unit* target = GetTarget();
	if (!target)
		return false;

	bool isMelee = true;
	if (target->IsPlayer())
	{
		isMelee = !ai->IsRanged((Player*)target);
	}

	return SpellCanBeCastedTrigger::IsActive() && isMelee;
}

bool ShadowOfDeathTrigger::IsActive()
{
	// Tortoise 52710 banks a share of damage dealt during the sigil, capped by
	// AP x CP/2, then detonates. Only spend the 60s cooldown and full CP bar
	// on a target durable enough to pay it back.
	if (!SpellCanBeCastedTrigger::IsActive())
		return false;

	if (AI_VALUE2(uint8, "combo", "current target") < 5)
		return false;

	return AI_VALUE2(uint8, "health", "current target") > 30;
}

bool MarkForDeathTrigger::IsActive()
{
	// Tortoise 52538: 3min party-support opener. Gate on a fresh, durable
	// target so the buff window covers a real fight, not a dying add.
	if (!SpellCanBeCastedTrigger::IsActive())
		return false;

	return AI_VALUE2(uint8, "health", "current target") > 50;
}

bool AlmostDeadFinisherTrigger::IsActive()
{
    // Any banked combo point qualifies (donor: no CP gate); the target must
    // actually be nearly dead so healthy targets keep building to SnD/4CP.
    if (AI_VALUE2(uint8, "combo", "current target") < 1)
        return false;
    if (!sServerFacade.IsSpellReady(bot, 2098))
        return false;
    return AI_VALUE2(uint8, "health", "current target") <= 25;
}

bool ExposeArmorTrigger::IsActive()
{
    if (!NoDebuffAndComboPointsAvailableTrigger::IsActive())
        return false;

    // Donor: mod-playerbots ExposeArmorTrigger skips when sunder armor is on the
    // target. Our own Expose upkeep is unchanged (the base trigger above
    // already goes quiet while our Expose is up).
    // Not ai->HasAura(): its IsRealAura filter hides another caster's stackable
    // debuff on a hostile unit until max stacks, so a warrior's 1-4 stack
    // Sunder read as absent and Expose overwrote it. Check every Sunder Armor
    // spell id directly (any rank, any caster, any stack count).
    Unit* target = AI_VALUE(Unit*, "current target");
    if (target)
    {
        for (uint32 spellId : chat->SpellIds("sunder armor"))
        {
            if (target->HasAura(spellId))
                return false;
        }
    }

    return true;
}

bool MainHandWeaponNoEnchantTrigger::IsActive()
{
    Item* weapon = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    if (!weapon || weapon->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT))
        return false;

    static const uint32 mhPoisons[] = { 6947, 6949, 6950, 8926, 8927, 8928, 21927, 2892, 2893, 8984, 8985, 20844, 22053, 22054, 10918, 10920, 10921, 10922, 22055, 3775, 3776, 5237, 6951, 9186 };
    return HasUsablePoisonItem(ai, bot, mhPoisons);
}

bool OffHandWeaponNoEnchantTrigger::IsActive()
{
    Item* weapon = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
    if (!weapon || weapon->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT))
        return false;

    static const uint32 ohPoisons[] = { 2892, 2893, 8984, 8985, 20844, 22053, 22054, 6947, 6949, 6950, 8926, 8927, 8928, 21927, 10918, 10920, 10921, 10922, 22055, 3775, 3776, 5237, 6951, 9186 };
    return HasUsablePoisonItem(ai, bot, ohPoisons);
}

bool SapCcMarkTrigger::IsActive()
{
    if (sServerFacade.IsInCombat(bot) || !ai->HasRealPlayerMaster())
        return false;

    // Player switch: `.bot action sap off` adds "no sap" (on by default).
    if (ai->HasStrategy("no sap", BotState::BOT_STATE_NON_COMBAT))
        return false;

    const uint32 spellId = AI_VALUE2(uint32, "spell id", "sap");
    if (!spellId || !sServerFacade.IsSpellReady(bot, spellId))
        return false;

    // Only the assigned mark: no mark, "rti cc none" or a mark out of sight
    // or line of sight leaves this empty.
    Unit* target = AI_VALUE(Unit*, "rti cc target");
    if (!target || !target->IsCreature() || sServerFacade.IsInCombat(target))
        return false;

    // Vanilla Sap: humanoids only, never a mob already in combat.
    if (target->GetCreatureType() != CREATURE_TYPE_HUMANOID)
        return false;

    // Already held (our Sap, another rogue's, a Polymorph, a Shackle...).
    if (PossibleAttackTargetsValue::HasBreakableCC(target, bot) ||
        PossibleAttackTargetsValue::HasUnBreakableCC(target, bot) ||
        ai->HasAura("sap", target))
        return false;

    return true;
}

bool BladeFlurryNearCcTrigger::IsActive()
{
    // Aura first: only a rogue with Blade Flurry up pays for the scan.
    if (!ai->HasAura("blade flurry", bot))
        return false;
    return BladeFlurryNearCc(ai, bot, CcBladeFlurryCancelRadius);
}
