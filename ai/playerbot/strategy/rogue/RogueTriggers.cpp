
#include "playerbot/playerbot.h"
#include "RogueTriggers.h"
#include "RogueActions.h"

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

bool MainHandWeaponNoEnchantTrigger::IsActive()
{
    Item* weapon = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    if (!weapon || weapon->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT))
        return false;

    if (ai->HasCheat(BotCheatMask::item))
        return true;

    static const uint32 mhPoisons[] = { 6947, 6949, 6950, 8926, 8927, 8928, 21927, 2892, 2893, 8984, 8985, 20844, 22053, 22054, 10918, 10920, 10921, 10922, 22055, 3775, 3776, 5237, 6951, 9186 };
    for (uint32 id : mhPoisons)
    {
        if (bot->HasItemCount(id, 1))
            return true;
    }
    return false;
}

bool OffHandWeaponNoEnchantTrigger::IsActive()
{
    Item* weapon = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
    if (!weapon || weapon->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT))
        return false;

    if (ai->HasCheat(BotCheatMask::item))
        return true;

    static const uint32 ohPoisons[] = { 2892, 2893, 8984, 8985, 20844, 22053, 22054, 6947, 6949, 6950, 8926, 8927, 8928, 21927, 10918, 10920, 10921, 10922, 22055, 3775, 3776, 5237, 6951, 9186 };
    for (uint32 id : ohPoisons)
    {
        if (bot->HasItemCount(id, 1))
            return true;
    }
    return false;
}
