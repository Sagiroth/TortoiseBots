
#include "playerbot/playerbot.h"
#include "PossibleTargetsValue.h"
#include "PossibleAttackTargetsValue.h"
#include "FreeMoveValues.h"

#include "playerbot/ServerFacade.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;
using namespace MaNGOS;

std::list<ObjectGuid> PossibleTargetsValue::Calculate()
{
    // Phase 2 guarded scan cadence (Issue #175): the Cell grid visit below is
    // the hottest world-thread cost (~60% of tick). True idle (taxi/rested
    // sanctuary) returns empty immediately; roaming bots reuse the cached
    // list at a staggered 1s cadence. Combat/death/damage/grace/human guards
    // inside ShouldReuseSpatialScan() force full 100ms rate. Throttling only
    // delays *discovery* of new grind candidates: attackers/threat, current
    // targets and explicit commands bypass this value entirely.
    if (ai->IsSpatialScanIdle())
        return std::list<ObjectGuid>();
    if (ai->ShouldReuseSpatialScan())
        return LazyGet();

    float rangeCheck = range;
    bool shouldIgnoreValidate = false;
    if (!qualifier.empty())
    {
        rangeCheck = Qualified::getMultiQualifierInt(qualifier, 0, ":");
        shouldIgnoreValidate = Qualified::getMultiQualifierInt(qualifier, 1, ":");
    }

    std::list<Unit*> targets;
    FindPossibleTargets(bot, targets, rangeCheck);

    std::list<ObjectGuid> results;
    for (std::list<Unit*>::iterator i = targets.begin(); i != targets.end(); ++i)
    {
        Unit* unit = *i;
        if (unit && (shouldIgnoreValidate || AcceptUnit(unit)))
        {
            results.push_back(unit->getObjectGuid());
        }
    }

    Set(results);
    ai->NoteSpatialScan();
    return results;
}

void PossibleTargetsValue::FindUnits(std::list<Unit*> &targets)
{
    FindPossibleTargets(bot, targets, range);
}

bool PossibleTargetsValue::AcceptUnit(Unit* unit)
{
    return IsValid(unit, bot, ignoreLos);
}

void PossibleTargetsValue::FindPossibleTargets(Player* player, std::list<Unit*>& targets, float range)
{
    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck u_check(player, player, range);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(targets, u_check);
    Cell::VisitAllObjects(player, searcher, range);
}

bool PossibleTargetsValue::IsFriendly(Unit* target, Player* player)
{
    bool friendly = false;
    if (sServerFacade.IsFriendlyTo(target, player))
    {
        friendly = true;

    }

    return friendly;
}

bool PossibleTargetsValue::IsAttackable(Unit* target, Player* player)
{
    return !target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_ATTACKABLE_1) &&
           !target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNTARGETABLE) &&
           !target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE) &&
           !target->HasAuraType(SPELL_AURA_SPIRIT_OF_REDEMPTION);
}

bool PossibleTargetsValue::IsValid(Unit* target, Player* player, bool ignoreLos)
{
    // If the target is available
    if (target && target->IsInWorld() && (target->GetMapId() == player->GetMapId()))
    {
        // If the target is dead
        if (sServerFacade.UnitIsDead(target))
        {
            return false;
        }

        // If the target is friendly
        if (IsFriendly(target, player))
        {
            return false;
        }

        // If the target can't be attacked
        if (!IsAttackable(target, player))
        {
            return false;
        }

        // A creature in evade mode is untouchable: the core refuses to start an attack
        // on one and drops every point of damage aimed at it (WorldObject::DealDamageMods),
        // while its health regenerates. One is therefore not a possible target - not for
        // grinding (GrindTargetValue), not for a pull, not for the travel/area scans - and
        // a bot that reaches it walks the whole way for nothing and then stands on it.
        // It is also what clears a stale "attack target" in SelectNewTargetAction: that
        // check is "possible targets no los", so the guid must not survive here.
        if (target->IsCreature() && static_cast<Creature*>(target)->IsInEvadeMode())
        {
            return false;
        }

        // Being in combat with *this* target is a reason to know where it is
        // without seeing it. Being in combat at all is not: player->IsInCombat()
        // used to be part of this, which meant a bot fighting anyone could pick
        // out every stealthed player within range.
        bool isInCombatWithTarget = target->GetVictim() == player ||
                                     target->GetThreatManager().getThreat(player) > 0.0f;

        if (!ignoreLos && !isInCombatWithTarget)
        {
            if (!target->IsVisibleForOrDetect(player, player->GetCamera().GetBody(), true))
            {
                return false;
            }
        }
        if (!CanFreeMoveValue::CanFreeAttack(PlayerbotAIStorage::Instance().GetAI(player), target))
            return false;

        return true;
    }

    return false;
}
