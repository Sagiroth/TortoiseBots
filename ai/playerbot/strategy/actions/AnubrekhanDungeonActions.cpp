#include "playerbot/playerbot.h"
#include "AnubrekhanDungeonActions.h"
#include "AttackAction.h"

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
            (!weakest ||
             100.0f * unit->GetHealth() / unit->GetMaxHealth() <
                 100.0f * weakest->GetHealth() / weakest->GetMaxHealth()))
            weakest = unit;
    }

    // Explicit master orders win over the fight choreography.
    ObjectGuid explicitGuid = AI_VALUE(ObjectGuid, "explicit attack target");
    if (!explicitGuid.IsEmpty())
        return false;

    Unit* want = weakest ? weakest : boss;
    if (!want)
        return false;
    if (AI_VALUE(Unit*, "current target") == want)
        return false;
    return Attack(bot, want);
}

bool AnubrekhanToCenterAction::Execute(Event& event)
{
    // Map gate (non-blocking review): never path to Naxx coords from
    // another map if the strategy is forced on outside.
    if (bot->GetMapId() != 533)
        return false;
    return MoveNear(bot->GetMapId(), 3272.49f, -3476.27f, bot->GetPositionZ(), 5.0f);
}

float AnubrekhanSwarmMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;
    if (dynamic_cast<FleeAction*>(action) == nullptr)
        return 1.0f;

    // Cached lists only: no grid sweep per action evaluation. During
    // the encounter Anub is on threat group-wide.
    const std::list<ObjectGuid> attackers =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
    for (const ObjectGuid& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->GetEntry() == 15956 && ai->HasAura(28785, unit))
            return 0.0f;
    }
    const std::list<ObjectGuid> targets =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    for (const ObjectGuid& guid : targets)
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->GetEntry() == 15956 && ai->HasAura(28785, unit))
            return 0.0f;
    }
    return 1.0f;
}
