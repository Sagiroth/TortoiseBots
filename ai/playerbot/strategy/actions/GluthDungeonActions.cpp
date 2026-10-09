#include "playerbot/playerbot.h"
#include "GluthDungeonActions.h"
#include "playerbot/GluthKitePolicy.h"
#include "AttackAction.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

bool GluthChooseTargetAction::Execute(Event& event)
{
    if (PlayerbotAI::IsTank(bot))
        return false;

    Unit* boss = nullptr;
    Unit* execute = nullptr;

    const std::list<ObjectGuid> targets =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    for (const ObjectGuid& guid : targets)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !unit->IsAlive())
            continue;
        if (unit->GetEntry() == 15932)
            boss = unit;
        else if (unit->GetEntry() == 16360)
        {
            const float pct = 100.0f * unit->GetHealth() / unit->GetMaxHealth();
            if (IsGluthChowExecute(pct) && bot->GetDistance(unit) <= 30.0f &&
                (!execute || bot->GetDistance(unit) < bot->GetDistance(execute)))
                execute = unit;
        }
    }

    Unit* want = execute ? execute : boss;
    if (!want)
        return false;
    if (AI_VALUE(Unit*, "current target") == want)
        return false;
    return Attack(bot, want);
}
