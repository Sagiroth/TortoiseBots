
#include "playerbot/playerbot.h"
#include "OnyxiasLairDungeonActions.h"

using namespace ai;
#include "playerbot/ServerFacade.h"
#include "playerbot/OnyxiaBreathPolicy.h"

namespace
{
// Donor safe-zone pairs per breath axis (Onyxia's lair map coords):
// 0 = N-S axis, 1 = E-W axis, 2 = SE-NW axis, 3 = SW-NE axis.
struct BreathSafeSpot { float x, y, z; };
BreathSafeSpot const kBreathSafeSpots[4][2] =
{
    { { -10.0f, -180.0f, -87.0f }, { -20.0f, -250.0f, -88.0f } },
    { { 20.0f, -210.0f, -85.5f }, { -75.0f, -210.0f, -83.4f } },
    { { -60.0f, -195.0f, -85.0f }, { 10.0f, -240.0f, -85.9f } },
    { { 7.0f, -185.0f, -86.2f }, { -60.0f, -240.0f, -85.2f } },
};
} // namespace

bool OnyxiaBreathSafeZoneAction::Execute(Event& event)
{
    (void)event;
    // Boss via the attacker scan (works for whelp tanks/melee/healers
    // whose own target is not Onyxia). Axis from boss facing: the core
    // faces the breath destination before clearing target, so facing maps
    // to the breath lane (N-S axis 0, E-W 1, SE-NW 2, SW-NE 3).
    AiObjectContext* context = ai->GetAiObjectContext();
    const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    Unit* boss = nullptr;
    for (const ObjectGuid& attackerGuid : attackers)
    {
        Unit* attacker = ai->GetUnit(attackerGuid);
        if (attacker && attacker->GetEntry() == kOnyxiaEntry && sServerFacade.IsAlive(attacker))
        {
            boss = attacker;
            break;
        }
    }
    if (!boss)
        return false;
    int axis = BreathAxisFromFacing(boss->GetOrientation());
    // Nearest of the pair; hold when already inside (donor early-out).
    BreathSafeSpot const* best = nullptr;
    float bestDist = FLT_MAX;
    for (BreathSafeSpot const& spot : kBreathSafeSpots[axis])
    {
        float dist = bot->GetDistance2d(spot.x, spot.y);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = &spot;
        }
    }
    if (!best)
        return false;
    if (!ShouldMoveToBreathSafeZone(true, bot->IsWithinDist2d(best->x, best->y, kBreathSafeZoneRadius)))
        return false;
    bot->AttackStop();
    bot->CastStop();
    return MoveTo(bot->GetMapId(), best->x, best->y, best->z, false, IsReaction(), false, true);
}
