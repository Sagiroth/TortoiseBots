#include "playerbot/playerbot.h"
#include "LoathebDungeonActions.h"
#include "playerbot/LoathebSporesPolicy.h"
#include "AttackAction.h"
#include "ChooseTargetActions.h"
#include "MovementActions.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

bool LoathebChooseTargetAction::Execute(Event& event)
{
    // Explicit master orders win: a bot told to hit something else keeps
    // its target without paying for the grid sweep below.
    if (!AI_VALUE(ObjectGuid, "explicit attack target").IsEmpty())
        return false;

    Unit* boss = nullptr;
    Unit* spore = nullptr;

    const std::list<ObjectGuid> targets =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    for (const ObjectGuid& guid : targets)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !unit->IsAlive())
            continue;
        if (unit->GetEntry() == 16011)
            boss = unit;
        else if (unit->GetEntry() == 16286 && ShouldKillLoathebSpore(true, bot->GetDistance(unit)))
            spore = unit;
    }
    // Spores may not be on anyone's target list yet: 5yd sweep by entry.
    if (!spore)
    {
        std::list<Unit*> nearby;
        MaNGOS::AllCreaturesOfEntryInRange check(bot, 16286, 5.0f);
        MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRange> searcher(nearby, check);
        Cell::VisitAllObjects(bot, searcher, 5.0f);
        for (Unit* unit : nearby)
        {
            if (unit && unit->IsAlive() && ShouldKillLoathebSpore(true, bot->GetDistance(unit)))
            {
                spore = unit;
                break;
            }
        }
    }

    Unit* want = spore ? spore : boss;
    if (!want)
        return false;
    // Spores threaten one random raid member (core AddThreats a single
    // target), so a neutral spore underfoot is never *this* bot's attacker:
    // skip the attacker gate and only check attackability + tap. The boss
    // keeps the full gate (threat/victim/CC checks apply to him normally).
    if (want == spore)
    {
        if (!PossibleAttackTargetsValue::IsPossibleTarget(want, bot, sPlayerbotAIConfig.sightDistance, false))
            return false;
    }
    else if (!PossibleAttackTargetsValue::IsValid(want, bot))
        return false;
    if (AI_VALUE(Unit*, "current target") == want)
        return false;
    return Attack(bot, want);
}

bool LoathebPositionAction::Execute(Event& event)
{
    // Never path to Naxx coords from another map if the strategy is
    // forced on outside.
    if (bot->GetMapId() != 533)
        return false;
    if (PlayerbotAI::IsTank(bot))
    {
        if (!AI_VALUE2(bool, "has aggro", "current target"))
            return false;
        if (bot->GetDistance2d(2877.57f, -3967.00f) < 3.0f)
            return false;
        return MoveTo(bot->GetMapId(), 2877.57f, -3967.00f, bot->GetPositionZ());
    }
    if (ai->IsRanged(bot))
    {
        if (bot->GetDistance2d(2896.96f, -3980.61f) < 5.0f)
            return false;
        return MoveTo(bot->GetMapId(), 2896.96f, -3980.61f, bot->GetPositionZ());
    }
    return false;
}

float LoathebSporeHoldMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;
    const bool assist = dynamic_cast<DpsAssistAction*>(action) || dynamic_cast<TankAssistAction*>(action);
    const bool aoe = dynamic_cast<DpsAoeAction*>(action) != nullptr;
    const bool flee = dynamic_cast<FleeAction*>(action) != nullptr;
    if (!assist && !aoe && !flee)
        return 1.0f;

    // O(1) hold while the current target is a live spore.
    Unit* current = AI_VALUE(Unit*, "current target");
    if (current && current->IsAlive() && current->GetEntry() == 16286)
        return 0.0f;

    // Tanks holding a live target stay on it while Loatheb is on threat
    // (cached attackers walk only, no grid sweep). A tank with no live
    // target (its spore just died) may re-acquire normally.
    if (current && current->IsAlive() && dynamic_cast<TankAssistAction*>(action) && PlayerbotAI::IsTank(bot))
    {
        const std::list<ObjectGuid> attackers =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
        for (const ObjectGuid& guid : attackers)
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->IsAlive() && unit->GetEntry() == 16011)
                return 0.0f;
        }
    }
    return 1.0f;
}
