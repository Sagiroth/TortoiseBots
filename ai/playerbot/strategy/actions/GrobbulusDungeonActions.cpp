#include "playerbot/playerbot.h"
#include "GrobbulusDungeonActions.h"
#include "playerbot/GrobbulusCloudPolicy.h"

using namespace ai;

bool GrobbulusGoBehindAction::Execute(Event& event)
{
    // Boss via context first (current target, then attackers) — no grid
    // scan per movement decision. Grid fallback only for the rare case
    // the raid fights Grobbulus but he is on no cached list.
    Unit* boss = AI_VALUE(Unit*, "current target");
    if (!boss || boss->GetEntry() != 15931 || !boss->IsAlive())
    {
        boss = nullptr;
        const std::list<ObjectGuid> attackers =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
        for (const ObjectGuid& guid : attackers)
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->IsAlive() && unit->GetEntry() == 15931)
            {
                boss = unit;
                break;
            }
        }
    }
    if (!boss)
        return false;

    float x, y;
    GrobbulusBehindSpot(boss->GetPositionX(), boss->GetPositionY(),
        boss->GetOrientation(), x, y);
    if (bot->GetDistance2d(x, y) < 2.0f)
        return false;
    // Reaction context: IsReaction() idiom (sibling hazard rows) so the move
    // issues without booking a multi-second WaitForReach that would hold
    // the reaction and stall combat.
    return MoveTo(bot->GetMapId(), x, y, boss->GetPositionZ(), false, IsReaction(), false, true);
}
