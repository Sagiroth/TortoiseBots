#include "playerbot/playerbot.h"
#include "HeiganDungeonActions.h"
#include "playerbot/HeiganDancePolicy.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

namespace
{
    // Core sect safe spots (boss_heigan.cpp): first entry of each array.
    // Area 0..3 in eruption order.
    const float kHeiganSafeSpots[4][3] = {
        { 2799.50f, -3691.00f, 273.62f },
        { 2790.51f, -3690.45f, 273.62f },
        { 2778.40f, -3702.65f, 273.62f },
        { 2777.20f, -3712.41f, 273.63f },
    };

    Unit* FindHeiganForDance(PlayerbotAI* ai, Player* bot)
    {
        const std::list<ObjectGuid> attackers =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
        for (const ObjectGuid& guid : attackers)
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->GetEntry() == 15936)
                return unit;
        }
        std::list<Unit*> nearby;
        MaNGOS::AllCreaturesOfEntryInRange check(bot, 15936, 100.0f);
        MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRange> searcher(nearby, check);
        Cell::VisitAllObjects(bot, searcher, 100.0f);
        for (Unit* unit : nearby)
        {
            if (unit && unit->IsAlive())
                return unit;
        }
        return nullptr;
    }
}

bool HeiganDanceMoveAction::Execute(Event& event)
{
    Unit* heigan = FindHeiganForDance(ai, bot);
    if (!heigan)
        return false;

    Aura* cloud = ai->GetAura(29350, heigan);
    if (!cloud)
        return false;

    // Dance elapsed from the aura's remaining duration (45s channel).
    const int32 remaining = cloud->GetAuraDuration();
    const int32 elapsed = 45000 - remaining;
    if (elapsed < 0)
        return false;

    const int area = HeiganSafeAreaNow(elapsed);
    if (bot->GetDistance2d(kHeiganSafeSpots[area][0], kHeiganSafeSpots[area][1]) < 4.0f)
        return false;
    return MoveTo(bot->GetMapId(), kHeiganSafeSpots[area][0], kHeiganSafeSpots[area][1], kHeiganSafeSpots[area][2]);
}

bool HeiganHoldPlatformAction::Execute(Event& event)
{
    if (bot->GetDistance2d(2794.26f, -3706.67f) < 4.0f)
        return false;
    return MoveTo(bot->GetMapId(), 2794.26f, -3706.67f, 276.54f);
}
