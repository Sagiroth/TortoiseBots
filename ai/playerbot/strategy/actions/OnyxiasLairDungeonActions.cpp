
#include "playerbot/playerbot.h"
#include "OnyxiasLairDungeonActions.h"

using namespace ai;
#include "playerbot/ServerFacade.h"
#include "playerbot/policy/raid/OnyxiaBreathPolicy.h"
#include "playerbot/policy/raid/OnyxiaWhelpEggPolicy.h"
#include "ChooseTargetActions.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"

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

OnyxiaWhelpScan ai::ScanOnyxiaWhelps(PlayerbotAI* ai, Player* bot)
{
    OnyxiaWhelpScan scan;
    AiObjectContext* context = ai->GetAiObjectContext();
    // Cached attackers only: whelps jump the raid, so they are on this
    // list; no grid sweep.
    const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    float bestDist = FLT_MAX;
    // Egg positions from the bot's cached egg search, fetched at most once
    // and only for a whelp inside the padded egg-field box.
    std::vector<OnyxiaPoint> eggs;
    bool eggsLoaded = false;
    for (const ObjectGuid& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !sServerFacade.IsAlive(unit))
            continue;
        if (unit->GetEntry() == kOnyxiaEntry)
        {
            // Core drives phase 2 flight via the Hover aura (17131).
            scan.bossAirborne = unit->HasAura(17131);
            continue;
        }
        if (unit->GetEntry() != kOnyxianWhelpEntry)
            continue;
        const float dist = bot->GetDistance(unit);
        if (dist > kOnyxiaWhelpEngageRange)
            continue;
        bool nearEgg = false;
        if (IsInOnyxiaEggZone(unit->GetPositionX(), unit->GetPositionY()))
        {
            if (!eggsLoaded)
            {
                eggsLoaded = true;
                for (GameObject* egg : CollectOnyxiaEggs(ai))
                    eggs.push_back({ egg->GetPositionX(), egg->GetPositionY() });
            }
            nearEgg = IsNearAnyOnyxiaEgg(unit->GetPositionX(), unit->GetPositionY(), eggs.data(), eggs.size());
        }
        if (!IsOnyxiaWhelpPickable(true, dist, nearEgg))
            continue;
        if (dist < bestDist)
        {
            bestDist = dist;
            scan.whelp = unit;
        }
    }
    return scan;
}

bool ai::IsOnyxiaWhelpDuty(PlayerbotAI* ai, Player* bot)
{
    return IsOnyxiaWhelpHunter(bot->GetClass(), PlayerbotAI::IsTank(bot), PlayerbotAI::IsHeal(bot), ai->IsRanged(bot));
}

std::vector<GameObject*> ai::CollectOnyxiaEggs(PlayerbotAI* ai)
{
    // Cached value, qualified by the egg entry: spawned eggs only.
    AiObjectContext* context = ai->GetAiObjectContext();
    const std::list<ObjectGuid> guids =
        AI_VALUE2(std::list<ObjectGuid>, "nearest game objects no los", std::to_string(kOnyxiaEggEntry));
    std::vector<GameObject*> eggs;
    for (const ObjectGuid& guid : guids)
    {
        GameObject* egg = ai->GetGameObject(guid);
        if (egg && sServerFacade.isSpawned(egg))
            eggs.push_back(egg);
    }
    return eggs;
}

GameObject* ai::FindOnyxiaEggInDanger(PlayerbotAI* ai, Player* bot)
{
    // Cheap box test first: outside the egg fields no search runs.
    if (!IsInOnyxiaEggZone(bot->GetPositionX(), bot->GetPositionY()))
        return nullptr;
    GameObject* nearest = nullptr;
    float bestDist = FLT_MAX;
    for (GameObject* egg : CollectOnyxiaEggs(ai))
    {
        const float dist = OnyxiaDist2d(bot->GetPositionX(), bot->GetPositionY(), egg->GetPositionX(), egg->GetPositionY());
        if (IsInsideOnyxiaEggDanger(dist) && dist < bestDist)
        {
            bestDist = dist;
            nearest = egg;
        }
    }
    return nearest;
}

bool OnyxiaKillWhelpsAction::Execute(Event& event)
{
    (void)event;
    if (!sPlayerbotAIConfig.onyxiaWhelpsAndEggs || bot->GetMapId() != kOnyxiaMapId)
        return false;
    if (!IsOnyxiaWhelpDuty(ai, bot) || !AI_VALUE(ObjectGuid, "explicit attack target").IsEmpty())
        return false;
    const OnyxiaWhelpScan scan = ScanOnyxiaWhelps(ai, bot);
    if (!scan.bossAirborne || !scan.whelp)
        return false;
    // Already on a whelp: keep it (donor early-out).
    Unit* current = AI_VALUE(Unit*, "current target");
    if (current && sServerFacade.IsAlive(current) && current->GetEntry() == kOnyxianWhelpEntry)
        return false;
    if (!PossibleAttackTargetsValue::IsValid(scan.whelp, bot))
        return false;
    return Attack(bot, scan.whelp);
}

float OnyxiaWhelpHoldMultiplier::GetValue(Action* action)
{
    if (!action || !sPlayerbotAIConfig.onyxiaWhelpsAndEggs)
        return 1.0f;
    const bool assist = dynamic_cast<DpsAssistAction*>(action) || dynamic_cast<TankAssistAction*>(action);
    const bool aoe = dynamic_cast<DpsAoeAction*>(action) != nullptr;
    if (!assist && !aoe)
        return 1.0f;
    if (bot->GetMapId() != kOnyxiaMapId || !IsOnyxiaWhelpDuty(ai, bot))
        return 1.0f;
    Unit* current = AI_VALUE(Unit*, "current target");
    const bool onWhelp = current && sServerFacade.IsAlive(current) && current->GetEntry() == kOnyxianWhelpEntry;
    if (!onWhelp || !AI_VALUE(ObjectGuid, "explicit attack target").IsEmpty())
        return 1.0f;
    // The hold only lasts while the boss is airborne (phase 2).
    return ScanOnyxiaWhelps(ai, bot).bossAirborne ? 0.0f : 1.0f;
}

bool OnyxiaAvoidEggsAction::Execute(Event& event)
{
    (void)event;
    if (!sPlayerbotAIConfig.onyxiaWhelpsAndEggs || bot->GetMapId() != kOnyxiaMapId)
        return false;
    if (!ShouldAvoidOnyxiaEggs(true, true, IsInOnyxiaEggZone(bot->GetPositionX(), bot->GetPositionY()),
                               FindOnyxiaEggInDanger(ai, bot) != nullptr))
        return false;
    const OnyxiaEggExit& exit = kOnyxiaEggExits[NearestOnyxiaEggExit(bot->GetPositionX(), bot->GetPositionY())];
    return MoveTo(bot->GetMapId(), exit.x, exit.y, exit.z, false, IsReaction(), false, true);
}
