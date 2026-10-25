#include "playerbot/playerbot.h"
#include "OnyxiaTankPositionActions.h"
#include "ReachTargetActions.h"
#include "playerbot/OnyxiaTankPositionPolicy.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/values/LastMovementValue.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "Movement/spline/MoveSplineInit.h"

using namespace ai;

Unit* ai::GetOnyxiaTankPositionTarget(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!bot || bot->GetMapId() != 249 || !bot->IsInWorld() || !bot->IsAlive() ||
        bot->IsBeingTeleported() || !bot->IsInCombat() || !ai->IsTank(bot) ||
        !ai->HasStrategy("onyxia landing tank", BotState::BOT_STATE_COMBAT))
        return nullptr;
    AiObjectContext* context = ai->GetAiObjectContext();
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto hold = positions.find("pull hold");
    if (ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("grind", BotState::BOT_STATE_COMBAT) ||
        (hold != positions.end() && hold->second.isSet()))
        return nullptr;
    // An attack order on Onyxia is compatible with the assigned spot; an
    // explicit order/mark on another valid enemy temporarily overrides it.
    Unit* order = ai->GetUnit(AI_VALUE(ObjectGuid, "explicit attack target"));
    Unit* marked = AI_VALUE(Unit*, "rti target");
    for (Unit* target : {order, marked})
        if (target && target->GetEntry() != 10184 && PossibleAttackTargetsValue::IsValid(
            target, bot, sPlayerbotAIConfig.sightDistance, false, false))
            return nullptr;
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
    {
        Unit* boss = ai->GetUnit(guid);
        if (boss && boss->GetEntry() == 10184 && boss->IsAlive() && boss->IsInCombat() &&
            boss->IsInMap(bot) && !boss->IsFlying() && !boss->HasAura(17131) &&
            PossibleAttackTargetsValue::IsValid(boss, bot, sPlayerbotAIConfig.sightDistance, false, false))
            return boss;
    }
    return nullptr;
}

bool OnyxiaTankPositionAction::isUseful()
{
    if (!MovementAction::isUseful() || !GetOnyxiaTankPositionTarget(ai)) return false;
    OnyxiaTankPoint const here{bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()};
    return OnyxiaTankDistance(here, kOnyxiaTankSpot) > 1.5f || !bot->IsStopped() ||
        bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE;
}

bool OnyxiaTankPositionAction::Execute(Event& /*event*/)
{
    if (!GetOnyxiaTankPositionTarget(ai) || !ai->CanMove() || bot->GetTransport() || bot->IsTaxiFlying())
        return false;
    WorldPosition const start(bot);
    OnyxiaTankPoint const here{start.getX(), start.getY(), start.getZ()};
    // Cancel an existing chase even if a safe route cannot be found. Do not
    // leave its generator able to take the assigned tank into a cave later.
    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
        ai->StopMoving();
    if (OnyxiaTankDistance(here, kOnyxiaTankSpot) <= 1.5f)
    {
        ai->StopMoving();
        blockedReported = false;
        return true;
    }
    auto fail = [&]()
    {
        if (!blockedReported)
        {
            sPlayerbotAIConfig.logEvent(ai, "OnyxiaTankPositionBlocked", "unsafe or incomplete route", "manual reposition required");
            ai->TellError(ai->GetMaster(), "Onyxia tank spot: no safe route. Move the tank into the central room.");
            blockedReported = true;
        }
        SetDuration(1000);
        return false;
    };
    WorldPosition destination(bot->GetMapId(), kOnyxiaTankSpot.x, kOnyxiaTankSpot.y, kOnyxiaTankSpot.z);
    destination.setZ(destination.GetHeight());
    OnyxiaTankPoint const end{destination.getX(), destination.getY(), destination.getZ()};
    if (!OnyxiaTankCorridor(here) || !OnyxiaTankCorridor(end) ||
        OnyxiaTankDistance(end, kOnyxiaTankSpot) > 1.5f) return fail();
    std::vector<WorldPosition> const path = start.GetPathTo(destination, bot);
    std::vector<OnyxiaTankPoint> route;
    for (auto const& point : path) route.push_back({point.getX(), point.getY(), point.getZ()});
    if (!OnyxiaTankRouteSafe(here, route) || !destination.isPathTo(path, 1.0f, 1.0f)) return fail();
    auto hazards = AI_VALUE(std::list<HazardPosition>, "hazards");
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "possible targets no los"))
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->IsAlive() && unit->IsInMap(bot) && !unit->IsInCombat())
            hazards.emplace_back(WorldPosition(unit), sPlayerbotAIConfig.aggroDistance);
    }
    WorldPosition previous = start;
    for (auto const& point : path)
    {
        unsigned const steps = std::max(1u, static_cast<unsigned>(std::ceil(previous.distance(point) / 2.0f)));
        for (unsigned i = 1; i <= steps; ++i)
        {
            float const fraction = float(i) / steps;
            WorldPosition sample(bot->GetMapId(), previous.getX() + fraction*(point.getX()-previous.getX()),
                previous.getY() + fraction*(point.getY()-previous.getY()), previous.getZ() + fraction*(point.getZ()-previous.getZ()));
            if (!IsMovingAllowed(bot->GetMapId(), sample.getX(), sample.getY(), sample.getZ())) return fail();
            for (auto const& hazard : hazards)
                if (sample.distance(hazard.first) <= hazard.second + 2.0f) return fail();
        }
        previous = point;
    }
    ai->StopMoving();
    bot->InterruptNonMeleeSpells(false);
    Movement::MoveSplineInit init(*bot, "OnyxiaTankPosition");
    init.MovebyPath(WorldPosition().toPointsArray(path));
    init.SetWalk(false);
    if (!init.Launch()) return fail();
    AI_VALUE(LastMovement&, "last movement").lastPath.clear();
    ClearIdleState();
    blockedReported = false;
    SetDuration(500);
    return true;
}

float OnyxiaTankPositionMultiplier::GetValue(Action* action)
{
    if (!action || !GetOnyxiaTankPositionTarget(ai)) return 1.0f;
    std::string const& name = action->getName();
    if (name == "onyxia tank position" || name == "set facing" || name == "flee" ||
        name == "move away from hazard" || name == "onyxia deep breath escape") return 1.0f;
    return dynamic_cast<MovementAction*>(action) || dynamic_cast<CastReachTargetSpellAction*>(action) ? 0.0f : 1.0f;
}
