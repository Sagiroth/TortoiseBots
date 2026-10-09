#include "playerbot/playerbot.h"
#include "AnubrekhanDungeonActions.h"
#include "AttackAction.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

bool AnubrekhanChooseTargetAction::Execute(Event& event)
{
    if (PlayerbotAI::IsTank(bot))
        return false;

    Unit* boss = nullptr;
    Unit* weakest = nullptr;

    const std::list<ObjectGuid> targets =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    for (const ObjectGuid& guid : targets)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !unit->IsAlive())
            continue;
        if (unit->GetEntry() == 15956)
            boss = unit;
        else if (unit->GetEntry() == 16573 &&
            (!weakest || unit->GetHealth() < weakest->GetHealth()))
            weakest = unit;
    }

    Unit* want = weakest ? weakest : boss;
    if (!want)
        return false;
    if (AI_VALUE(Unit*, "current target") == want)
        return false;
    return Attack(bot, want);
}

bool AnubrekhanToCenterAction::Execute(Event& event)
{
    if (bot->GetDistance2d(3272.49f, -3476.27f) < 3.0f)
        return false;
    return MoveTo(bot->GetMapId(), 3272.49f, -3476.27f, bot->GetPositionZ());
}

float AnubrekhanSwarmMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;
    if (dynamic_cast<FleeAction*>(action) == nullptr)
        return 1.0f;

    const std::list<ObjectGuid> attackers =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
    for (const ObjectGuid& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->GetEntry() == 15956 && ai->HasAura(28785, unit))
            return 0.0f;
    }

    std::list<Unit*> nearby;
    MaNGOS::AllCreaturesOfEntryInRange check(bot, 15956, 100.0f);
    MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRange> searcher(nearby, check);
    Cell::VisitAllObjects(bot, searcher, 100.0f);
    for (Unit* unit : nearby)
    {
        if (unit && unit->IsAlive() && ai->HasAura(28785, unit))
            return 0.0f;
    }
    return 1.0f;
}
