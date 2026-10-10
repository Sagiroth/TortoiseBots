#include "playerbot/playerbot.h"
#include "HeiganDungeonActions.h"
#include "playerbot/HeiganDancePolicy.h"
#include "playerbot/strategy/HeiganDungeonHelper.h"

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
}

bool HeiganDanceMoveAction::Execute(Event& event)
{
    Unit* heigan = FindHeiganBoss(ai, bot);
    if (!heigan || !IsHeiganDancing(ai, heigan))
    {
        fallbackAnchorMs = 0;
        lastArea = -1;
        return false;
    }

    int32 elapsed = -1;
    if (Aura* cloud = ai->GetAura(kHeiganPlagueCloud, heigan))
        elapsed = HeiganDanceElapsed(cloud->GetAuraMaxDuration(), cloud->GetAuraDuration());
    else
    {
        // No aura on the boss (fallback path): anchor the clock at first
        // sight of the victimless dance. Wrong for late joiners, exact
        // for present bots, and the aura path re-anchors whenever the
        // aura is visible.
        const uint32 now = WorldTimer::getMSTime();
        if (!fallbackAnchorMs)
            fallbackAnchorMs = now;
        elapsed = int32(now - fallbackAnchorMs);
    }
    if (elapsed < 0)
        elapsed = 0;

    const int area = HeiganSafeAreaNow(elapsed);
    if (area != lastArea)
    {
        // New safe spot: stop the current cast so the move starts now
        // (donor HeiganDanceAction::MoveToWaypoint).
        lastArea = area;
        bot->CastStop();
    }
    if (bot->GetDistance2d(kHeiganSafeSpots[area][0], kHeiganSafeSpots[area][1]) < 4.0f)
        return false;
    return MoveTo(bot->GetMapId(), kHeiganSafeSpots[area][0], kHeiganSafeSpots[area][1], kHeiganSafeSpots[area][2]);
}

float HeiganDanceSuppressionMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;
    // Cheap class checks first; the encounter state is only read for
    // actions this multiplier may block.
    if (dynamic_cast<HeiganDanceMoveAction*>(action) ||
        dynamic_cast<HeiganHoldPlatformAction*>(action))
        return 1.0f;
    if (dynamic_cast<MovementAction*>(action) == nullptr)
        return 1.0f;

    // Fail-open: boss off threat (or off map) means no dance to protect.
    // Attackers-list only here — no grid sweep per action evaluation.
    const std::list<ObjectGuid> attackers =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
    Unit* boss = nullptr;
    for (const ObjectGuid& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->GetEntry() == kHeiganEntry)
        {
            boss = unit;
            break;
        }
    }
    if (!boss || !IsHeiganDancing(ai, boss))
        return 1.0f;
    return 0.0f;
}

bool HeiganHoldPlatformAction::Execute(Event& event)
{
    if (bot->GetDistance2d(2794.26f, -3706.67f) < 4.0f)
        return false;
    return MoveTo(bot->GetMapId(), 2794.26f, -3706.67f, 276.54f);
}
