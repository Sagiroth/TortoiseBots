
#include "playerbot/playerbot.h"
#include "DruidTriggers.h"
#include "DruidActions.h"

using namespace ai;

bool EntanglingRootsKiteTrigger::IsActive()
{
	if (!DebuffTrigger::IsActive())
		return false;

    if (AI_VALUE(uint8, "attackers count") > 3)
        return false;

	if (GetTarget()->GetPowerType() != POWER_MANA)
		return false;

    std::list<ObjectGuid> attackers = context->GetValue<std::list<ObjectGuid>>("attackers")->Get();
    for (std::list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); i++)
    {
        Unit* unit = ai->GetUnit(*i);
        if (!unit || !sServerFacade.IsAlive(unit))
            continue;

        if (ai->HasMyAura("entangling roots", unit))
            return false;
    }

    return !HasMaxDebuffs();
}

bool InFeralFormTrigger::IsActive()
{
    return ai->HasAura("bear form", bot) || ai->HasAura("dire bear form", bot) || ai->HasAura("cat form", bot);
}

bool FerociousBiteExecuteTrigger::IsActive()
{
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target || !target->IsAlive())
        return false;

    if (!ai->HasSpell("ferocious bite"))
        return false;

    if (AI_VALUE2(uint8, "combo", "current target") < 1)
        return false;

    // Dying target a bite can matter against: HP% alone would fire 1-CP
    // bites on sub-25% bosses all execute phase, eating every combo point
    // before Rip (CP>=3) can refresh — Rip falls off for the whole phase.
    // The donor's absolute gate (remaining < 20000, tuned for WotLK bite
    // damage) is scaled to vanilla pools (~4000: top-rank bite hits an
    // order of magnitude softer here), so bosses keep Rip while trash and
    // near-dead targets still eat early bites.
    if (target->GetHealth() >= 4000)
        return false;

    return AI_VALUE2(uint8, "health", "current target") < 25;
}

bool FerociousBiteTimeTrigger::IsActive()
{
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target || !target->IsAlive())
        return false;

    if (AI_VALUE2(uint8, "combo", "current target") < 5)
        return false;

    // At 5 CP bite only when Rip is absent or healthy (>10 s left), so
    // bite never clips a Rip refresh. (Donor also checks savage roar —
    // no such spell in 1.18.1.)
    Aura* rip = ai->GetAura("rip", target, true);
    return !rip || rip->GetAuraDuration() > 10000;
}
