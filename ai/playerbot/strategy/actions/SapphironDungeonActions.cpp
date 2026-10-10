#include "playerbot/playerbot.h"
#include "SapphironDungeonActions.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/SapphironIcePolicy.h"
#include "playerbot/strategy/SapphironDungeonHelper.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

bool SapphironHideAction::Execute(Event& event)
{
    Unit* boss = FindSapphironBoss(ai, bot);
    if (!boss)
        return false;

    // Nearest live group member carrying Icebolt (28522).
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    Player* iceblocked = nullptr;
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || member == bot || !member->IsAlive())
            continue;
        if (!ai->HasAura(28522, member))
            continue;
        if (!iceblocked || bot->GetDistance(member) < bot->GetDistance(iceblocked))
            iceblocked = member;
    }
    if (!iceblocked)
        return false;

    float x, y;
    SapphironHideSpot(boss->GetPositionX(), boss->GetPositionY(),
        iceblocked->GetPositionX(), iceblocked->GetPositionY(), x, y);
    if (bot->GetDistance2d(x, y) < 2.0f)
        return false;
    return MoveTo(bot->GetMapId(), x, y, iceblocked->GetPositionZ());
}

bool SapphironAvoidBlizzardAction::Execute(Event& event)
{
    std::list<Unit*> nearby;
    MaNGOS::AllCreaturesOfEntryInRange check(bot, 16474, 30.0f);
    MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRange> searcher(nearby, check);
    Cell::VisitAllObjects(bot, searcher, 30.0f);

    Unit* closest = nullptr;
    for (Unit* unit : nearby)
    {
        if (!unit || !unit->IsAlive())
            continue;
        if (!closest || bot->GetDistance(unit) < bot->GetDistance(closest))
            closest = unit;
    }
    if (!closest)
        return false;

    float x, y;
    SapphironBlizzardExit(closest->GetPositionX(), closest->GetPositionY(),
        bot->GetPositionX(), bot->GetPositionY(), x, y);
    return MoveTo(bot->GetMapId(), x, y, bot->GetPositionZ());
}
