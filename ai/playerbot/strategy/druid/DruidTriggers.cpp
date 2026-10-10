
#include "playerbot/playerbot.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/OocRebirthPolicy.h"
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

bool ThornsOnTankTrigger::IsActive()
{
    if (!BuffOnTankTrigger::IsActive())
        return false;
    // Don't apply thorns if fire shield (conflict) is on the tank.
    Unit* target = GetTarget();
    return target && !ai->HasAura("fire shield", target);
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

bool OocRebirthTrigger::IsTargetValid(Unit* target)
{
    if (!RebirthTrigger::IsTargetValid(target))
        return false;
    // Explicit orders always win: a master-assigned revive target bypasses
    // the living-resurrector gate below (otherwise an explicit order to rez
    // is silently ignored while any priest is grouped).
    if (!AI_VALUE(std::list<ObjectGuid>, "revive targets").empty())
        return true;
    // Single decision source: OocRebirthPolicy::ShouldCastOocRebirth (unit
    // tested in tools/test_ooc_rebirth_policy.cpp). livingResurrector
    // counts only members that could actually rez: same map and within
    // spell range of the corpse — a priest on another map or 500 yd away
    // vetoes nothing (cf. PartyMemberValue::Check, ReleaseSpiritAction).
    OocRebirthState state{};
    state.partyMemberDead = true;
    state.botKnowsRebirth = ai->HasSpell("rebirth");
    state.botAlive = sServerFacade.IsAlive(bot);
    state.botInCombat = sServerFacade.IsInCombat(bot);
    state.livingResurrector = false;
    Group* group = bot->GetGroup();
    if (group && target)
    {
        for (Player* member : LiveGroupMembers(group))
        {
            if (!member || member == bot || !sServerFacade.IsAlive(member))
                continue;
            uint8 cls = member->GetClass();
            if (cls != CLASS_PRIEST && cls != CLASS_PALADIN && cls != CLASS_SHAMAN)
                continue;
            if (member->GetMapId() != bot->GetMapId())
                continue;
            if (sServerFacade.getDistance2d(member, target) > sPlayerbotAIConfig.sightDistance)
                continue;
            state.livingResurrector = true;
            break;
        }
    }
    return ShouldCastOocRebirth(state);
}
