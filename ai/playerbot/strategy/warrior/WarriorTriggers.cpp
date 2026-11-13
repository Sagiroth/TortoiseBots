
#include "playerbot/playerbot.h"
#include "playerbot/GroupMembers.h"
#include "WarriorTriggers.h"
#include "WarriorActions.h"
#include "playerbot/strategy/actions/PatchwerkSoakerActions.h"

using namespace ai;

bool SweepingStrikesNearCcTrigger::IsActive()
{
    // Aura first: only a warrior with Sweeping Strikes up pays for the scan.
    if (!ai->HasAura("sweeping strikes", bot))
        return false;
    return SweepingStrikesNearCc(ai, bot, CcSweepingStrikesRadius);
}

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
    if (ai::BuffTriggerMountGated(bot, ai))
        return false;
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

void SunderArmorDebuffTrigger::AnnounceFullStack(Unit* target)
{
    if (!sPlayerbotAIConfig.announceSunderStacks || announcedTarget == target->GetObjectGuid())
        return;

    Creature* creature = target->IsCreature() ? static_cast<Creature*>(target) : nullptr;
    if (!creature || !creature->IsElite())
        return;

    announcedTarget = target->GetObjectGuid();

    Group* group = bot->GetGroup();
    if (!group || ai->GetPlayersInGroup().empty())
        return;

    // One voice per target: of the warrior bots fighting it, the lowest guid speaks.
    for (Player* member : LiveGroupMembers(group))
    {
        if (member && member != bot && member->GetClass() == CLASS_WARRIOR && member->GetVictim() == target &&
            member->GetGUIDLow() < bot->GetGUIDLow() && PlayerbotAIStorage::Instance().GetAI(member))
            return;
    }

    std::string const msg = "5 Sunders on " + std::string(creature->GetName());
    if (group->isRaidGroup())
        ai->SayToRaid(msg);
    else
        ai->SayToParty(msg);
}

bool SunderArmorDebuffTrigger::IsActive()
{
    if (!ai->HasSpell("sunder armor"))
        return false;

    Unit* target = GetTarget();
    if (!target)
        return false;

    // Sunder still builds threat at five stacks: an assigned Patchwerk tank
    // needs it as filler while its stronger threat builders are unavailable.
    if (PatchwerkThreatTank(ai, target))
        return true;

    // Stop at a full 5-stack, but re-arm to refresh an expiring stack
    // (donor refreshes at <=6s remaining): without this the 5-stack falls
    // off completely before the trigger fires again.
    if (Aura* aura = ai->GetAura("sunder armor", target))
    {
        if (aura->GetStackAmount() >= 5)
        {
            AnnounceFullStack(target);
            return aura->GetAuraDuration() <= 6000;
        }
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
