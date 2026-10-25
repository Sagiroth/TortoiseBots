
#include "playerbot/playerbot.h"
#include "WarriorTriggers.h"
#include "WarriorActions.h"
#include "playerbot/strategy/actions/PatchwerkSoakerActions.h"

using namespace ai;

bool BattleStanceTrigger::IsActive()
{
    return !ai->IsTank(bot) && BuffTrigger::IsActive();
}

bool DefensiveStanceTrigger::IsActive()
{
    return ai->IsTank(bot) && BuffTrigger::IsActive();
}

bool BerserkerStanceTrigger::IsActive()
{
    return !ai->IsTank(bot) && BuffTrigger::IsActive();
}

bool ShieldBlockTrigger::IsActive()
{
    Unit* target = AI_VALUE(Unit*, "current target");
    return target && target->IsAlive() && target->GetVictim() == bot &&
        sServerFacade.IsHostileTo(bot, target) && bot->CanReachWithMeleeAutoAttack(target) &&
        BuffTrigger::IsActive();
}


bool BloodrageBuffTrigger::IsActive()
{
    if (!ai->HasSpell("bloodrage"))
        return false;

    uint32 spellId = AI_VALUE2(uint32, "spell id", "bloodrage");
    bool spellReady = spellId && sServerFacade.IsSpellReady(bot, spellId);

    uint8 rage = AI_VALUE2(uint8, "rage", "self target");
    return ShouldBloodrage(
        spellReady,
        AI_VALUE2(uint8, "health", "self target") >= sPlayerbotAIConfig.mediumHealth,
        rage);
}

bool SunderArmorDebuffTrigger::IsActive()
{
    if (!ai->HasSpell("sunder armor"))
        return false;

    Unit* target = GetTarget();
    if (!target)
        return false;

    // Sunder still generates threat at five stacks; assigned Patchwerk tanks
    // need this filler while their stronger threat builders are unavailable.
    if (PatchwerkThreatTank(ai, target)) return true;

    // Stop at a full 5-stack, but re-arm to refresh an expiring stack
    // (donor refreshes at <=6s remaining): without this the 5-stack falls
    // off completely before the trigger fires again.
    if (Aura* aura = ai->GetAura("sunder armor", target))
    {
        if (aura->GetStackAmount() >= 5)
            return aura->GetAuraDuration() >= 0 && aura->GetAuraDuration() <= 6000;
        return true;
    }

    if (ai->IsTank(bot) && !target->IsPlayer())
        return true;

    return !ai->HasAura("sunder armor", target, true) && !HasMaxDebuffs();
}

bool PatchwerkShieldSlamTrigger::IsActive()
{
    Unit* target = AI_VALUE(Unit*, "current target");
    return PatchwerkThreatTank(ai, target) && ai->CanCastSpell("shield slam", target);
}
