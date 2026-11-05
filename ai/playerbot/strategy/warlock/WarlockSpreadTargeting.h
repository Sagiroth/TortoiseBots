#pragma once

#include "playerbot/playerbot.h"
#include "playerbot/DungeonPlanMgr.h"
#include "playerbot/strategy/values/CcMarkHoldPolicy.h"
#include "playerbot/strategy/values/TargetValue.h"
#include "../../../runtime/policy/class/WarlockRaidRotationPolicy.h"

// Shared warlock spread targeting: the trigger and its action resolve the
// same target, and that target already passed the raid rotation policy, so a
// low-health or capped-out attacker cannot hide a later legal one.
namespace ai
{
namespace warlock
{

// Bosses: rank 3 (world and raid bosses, Zul'Gurub included), an
// instance-binding boss, or a boss row of the loaded dungeon plan (5-man
// bosses carry the elite rank). Drain Soul is a trash finisher and never
// spends a boss execute phase channelling; the life gate never skips one.
inline bool WarlockBossTarget(PlayerbotAI* ai, Unit* target)
{
    Creature* creature = target ? dynamic_cast<Creature*>(target) : nullptr;
    if (!creature)
        return false;
    return TortoiseBots::WarlockIsBossTarget(
        creature->IsWorldBoss(),
        creature->HasExtraFlag(CREATURE_FLAG_EXTRA_INSTANCE_BIND),
        ai::GetDungeonPlans().IsBossEntry(creature->GetMapId(), creature->GetEntry()));
}

// Attackers other than the current target that already carry the spread spell.
inline std::size_t WarlockExtraDottedTargets(PlayerbotAI* ai, std::string const& spell, bool checkIsOwner)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* current = context->GetValue<Unit*>("current target")->Get();
    std::list<ObjectGuid> attackers = context->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    std::size_t dotted = 0;
    for (ObjectGuid const& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit != current && ai->HasAura(spell, unit, false, checkIsOwner))
            ++dotted;
    }
    return dotted;
}

// The first attacker (other than the current target) that misses the aura and
// passes the rotation gates: health, boss/utility exemption and the raid cap.
// Same CC and range filters as "attacker without aura", so a DoT never lands
// on a held CC mark or out of reach.
inline Unit* WarlockSpreadAttackerTarget(PlayerbotAI* ai, std::string const& spell,
                                         bool auraCheckIsOwner, bool capCheckIsOwner,
                                         bool raidUtility, bool ignoreHealth)
{
    bool const raidRotation = sPlayerbotAIConfig.warlockRaidRotation;
    if (raidRotation)
    {
        Group* group = ai->GetBot()->GetGroup();
        if (TortoiseBots::WarlockSpreadCapApplies(true, group && group->isRaidGroup()) &&
            !TortoiseBots::WarlockSpreadAllowed(true, true,
                WarlockExtraDottedTargets(ai, spell, capCheckIsOwner)))
            return nullptr;
    }

    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* current = context->GetValue<Unit*>("current target")->Get();
    std::list<ObjectGuid> attackers = context->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    CcGuard ccGuard(ai);
    auto const picked = TortoiseBots::WarlockFirstAllowed(attackers.begin(), attackers.end(),
        [&](ObjectGuid const& guid)
        {
            Unit* unit = ai->GetUnit(guid);
            if (!unit || unit == current)
                return false;

            bool const ccMarkOrControlled = sPlayerbotAIConfig.dungeonRunAutoCc &&
                (ccGuard.IsCcMarkedUnit(unit) || ccGuard.IsCrowdControlledUnit(unit));
            if (!CcDotTargetAllowed(sPlayerbotAIConfig.dungeonRunAutoCc, ccGuard.IsHeldCcMark(unit), ccMarkOrControlled))
                return false;

            if (ai->GetBot()->GetDistance(unit) > ai->GetRange("spell"))
                return false;

            return TortoiseBots::WarlockSpreadCandidateAllowed(
                raidRotation, (uint8)unit->GetHealthPercent(), WarlockBossTarget(ai, unit),
                raidUtility, ai->HasAura(spell, unit, false, auraCheckIsOwner), ignoreHealth);
        });
    return picked == attackers.end() ? nullptr : ai->GetUnit(*picked);
}

} // namespace warlock
} // namespace ai
