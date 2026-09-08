
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
	// Turtle 52511 requires the reactive dodge target (core OnCheckCast
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
	// Turtle 52710 banks a share of damage dealt during the sigil, capped by
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
	// Turtle 52538: 3min party-support opener. Gate on a fresh, durable
	// target so the buff window covers a real fight, not a dying add.
	if (!SpellCanBeCastedTrigger::IsActive())
		return false;

	return AI_VALUE2(uint8, "health", "current target") > 50;
}