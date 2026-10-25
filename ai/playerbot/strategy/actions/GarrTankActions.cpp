#include "playerbot/playerbot.h"
#include "MoltenCoreDungeonActions.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/RaidTankAssignmentPolicy.h"
#include "playerbot/strategy/values/GarrTankAssignmentValue.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/values/TargetValue.h"
#include "../../../../runtime/PlayerbotAIStorage.h"

using namespace ai;

namespace
{
bool GarrHoldOrder(PlayerbotAI* ai)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto hold = positions.find("pull hold");
    return ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("grind", BotState::BOT_STATE_COMBAT) ||
        (hold != positions.end() && hold->second.isSet());
}

Unit* GarrExplicitTarget(PlayerbotAI* ai)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* target = ai->GetUnit(AI_VALUE(ObjectGuid, "explicit attack target"));
    if (target && PossibleAttackTargetsValue::IsValid(target, ai->GetBot(),
        sPlayerbotAIConfig.sightDistance, false, false))
        return target;
    target = AI_VALUE(Unit*, "rti target");
    return target && PossibleAttackTargetsValue::IsValid(target, ai->GetBot(),
        sPlayerbotAIConfig.sightDistance, false, false) ? target : nullptr;
}

bool GarrCcTarget(PlayerbotAI* ai, Unit* target)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    return AI_VALUE(Unit*, "cc target") == target || IsRaidCrowdControlMarkedTarget(ai, target) ||
        PossibleAttackTargetsValue::HasBreakableCC(target, ai->GetBot()) ||
        PossibleAttackTargetsValue::HasUnBreakableCC(target, ai->GetBot());
}

bool GarrAssignmentActive(PlayerbotAI* ai, GarrTankAssignment const& assignment)
{
    Unit* boss = ai->GetUnit(assignment.boss);
    return assignment.active && boss && boss->GetEntry() == 12057 && boss->IsAlive() &&
        boss->IsInCombat() && boss->IsInMap(ai->GetBot());
}
}

bool ai::IsGarrTankOptedIn(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    return bot && bot->IsAlive() && bot->IsInWorld() && !bot->IsBeingTeleported() &&
        bot->IsInCombat() && bot->GetMapId() == 409 && ai->IsTank(bot) &&
        (ai->HasStrategy("garr boss tank", BotState::BOT_STATE_COMBAT) ||
         ai->HasStrategy("garr add tank", BotState::BOT_STATE_COMBAT)) && !GarrHoldOrder(ai);
}

GarrTankAssignment GarrTankAssignmentValue::Calculate()
{
    GarrTankAssignment assignment;
    if (!IsGarrTankOptedIn(ai))
        return assignment;
    Player* bot = ai->GetBot();
    std::vector<Player*> members;
    if (bot->GetGroup())
    {
        for (Player* member : LiveGroupMembers(bot->GetGroup()))
            if (member->IsAlive() && member->IsInWorld() && !member->IsBeingTeleported() &&
                member->IsInMap(bot) && member->IsInCombat())
                members.push_back(member);
    }
    else
        members.push_back(bot);

    // A single cached owner map avoids conflicting assignments from each
    // tank's differently ordered/local attacker list. No persistent Unit pointers.
    PlayerbotAI* planner = ai;
    for (Player* member : members)
        if (PlayerbotAI* memberAI = PlayerbotAIStorage::Instance().GetAI(member))
            if (IsGarrTankOptedIn(memberAI) && member->GetObjectGuid() < planner->GetBot()->GetObjectGuid())
                planner = memberAI;
    if (planner != ai)
    {
        assignment = planner->GetAiObjectContext()->GetValue<GarrTankAssignment>("garr tank assignment")->Get();
        assignment.targets.clear();
        for (auto const& owner : assignment.owners)
            if (owner.second == bot->GetObjectGuid())
                assignment.targets.push_back(owner.first);
        return assignment;
    }

    Unit* boss = nullptr;
    std::vector<std::uint64_t> adds;
    auto discover = [&](Unit* unit)
    {
        if (!unit || !unit->IsAlive() || !unit->IsInCombat() || !unit->IsInMap(bot))
            return;
        if (unit->GetEntry() == 12057)
            boss = unit;
        else if (unit->GetEntry() == 12099)
            adds.push_back(unit->GetObjectGuid().GetRawValue());
    };
    for (Player* member : members)
    {
        discover(member->GetVictim());
        if (PlayerbotAI* memberAI = PlayerbotAIStorage::Instance().GetAI(member); IsGarrTankOptedIn(memberAI))
            for (ObjectGuid const& guid : memberAI->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get())
                discover(memberAI->GetUnit(guid));
    }
    if (!boss)
        return assignment;

    std::vector<RaidTankCandidate> tanks;
    for (Player* member : members)
    {
        PlayerbotAI* memberAI = PlayerbotAIStorage::Instance().GetAI(member);
        if (!memberAI)
        {
            if (ai->IsTank(member) && (boss->GetVictim() == member || member->GetVictim() == boss))
                tanks.push_back({member->GetObjectGuid().GetRawValue(), true, false, boss->GetObjectGuid().GetRawValue()});
            continue;
        }
        if (!IsGarrTankOptedIn(memberAI))
            continue;
        Unit* locked = GarrExplicitTarget(memberAI);
        if (locked && locked != boss && locked->GetEntry() != 12099)
            continue;
        tanks.push_back({member->GetObjectGuid().GetRawValue(),
            memberAI->HasStrategy("garr boss tank", BotState::BOT_STATE_COMBAT),
            memberAI->HasStrategy("garr add tank", BotState::BOT_STATE_COMBAT),
            locked ? locked->GetObjectGuid().GetRawValue() : 0});
    }
    auto const plan = PlanGolemaggTanks(tanks, boss->GetObjectGuid().GetRawValue(), adds);
    assignment.active = true;
    assignment.boss = boss->GetObjectGuid();
    assignment.owners.push_back({assignment.boss, ObjectGuid(plan.bossTank)});
    for (auto const& item : plan.addOwners)
        assignment.owners.push_back({ObjectGuid(item.first), ObjectGuid(item.second)});
    for (auto const& owner : assignment.owners)
        if (owner.second == bot->GetObjectGuid())
            assignment.targets.push_back(owner.first);
    return assignment;
}

Unit* ai::GetAssignedGarrTankTarget(PlayerbotAI* ai)
{
    if (!IsGarrTankOptedIn(ai) || GarrExplicitTarget(ai))
        return nullptr;
    AiObjectContext* context = ai->GetAiObjectContext();
    auto const assignment = AI_VALUE(GarrTankAssignment, "garr tank assignment");
    if (!GarrAssignmentActive(ai, assignment))
        return nullptr;
    Player* bot = ai->GetBot();
    Unit* current = AI_VALUE(Unit*, "current target");
    Unit* selected = nullptr;
    bool selectedLoose = false;
    for (ObjectGuid const& guid : assignment.targets)
    {
        Unit* target = ai->GetUnit(guid);
        if (!target || !target->IsAlive() || !target->IsInCombat() || !target->IsInMap(bot) ||
            !PossibleAttackTargetsValue::IsValid(target, bot, sPlayerbotAIConfig.sightDistance, false, false) ||
            GarrCcTarget(ai, target))
            continue;
        Player* victim = dynamic_cast<Player*>(target->GetVictim());
        bool const loose = !victim || !victim->IsAlive() || !ai->IsTank(victim);
        // Recover assigned loose adds first, even if our current target is
        // low-health or no longer held. Otherwise keep a held current target.
        if (!selected || (loose && !selectedLoose) ||
            (loose == selectedLoose &&
             ((target == current && selected != current) ||
              (selected != current && bot->GetDistance(target) < bot->GetDistance(selected)))))
        {
            selected = target;
            selectedLoose = loose;
        }
    }
    return selected;
}

bool ai::IsGarrTankTargetExcluded(PlayerbotAI* ai, Unit* target)
{
    if (!target || (target->GetEntry() != 12057 && target->GetEntry() != 12099) ||
        !IsGarrTankOptedIn(ai) || GarrExplicitTarget(ai))
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    auto const assignment = AI_VALUE(GarrTankAssignment, "garr tank assignment");
    if (!GarrAssignmentActive(ai, assignment))
        return false;
    return !target->IsAlive() || !target->IsInCombat() || !target->IsInMap(ai->GetBot()) ||
        GarrCcTarget(ai, target) ||
        std::find(assignment.targets.begin(), assignment.targets.end(), target->GetObjectGuid()) == assignment.targets.end();
}

bool GarrTankRetargetAction::isUseful()
{
    Unit* current = AI_VALUE(Unit*, "current target");
    Unit* assigned = GetAssignedGarrTankTarget(ai);
    return (assigned && assigned != current) || IsGarrTankTargetExcluded(ai, current);
}

bool GarrTankRetargetAction::Execute(Event& event)
{
    if (!isUseful())
        return false;
    bot->InterruptNonMeleeSpells(false);
    ai->StopMoving();
    bot->AttackStop();
    SET_AI_VALUE(Unit*, "current target", nullptr);
    context->GetValue<Unit*>("tank target")->Reset();
    ai->DoSpecificAction("tank assist", event, true);
    return true;
}
