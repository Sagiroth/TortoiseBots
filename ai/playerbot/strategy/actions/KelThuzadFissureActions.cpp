#include "playerbot/playerbot.h"
#include "KelThuzadFissureActions.h"
#include "playerbot/KelThuzadFissurePolicy.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "Movement/spline/MoveSplineInit.h"

using namespace ai;

std::list<HazardPosition> ai::GetKelThuzadFissures(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (bot->GetMapId() != 533 || !bot->IsInWorld() || !bot->IsAlive() ||
        bot->IsBeingTeleported() || !bot->IsInCombat() || bot->HasAura(28410) ||
        !ai->HasStrategy("kt fissure", BotState::BOT_STATE_COMBAT)) return {};
    AiObjectContext* context = ai->GetAiObjectContext();
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto hold = positions.find("pull hold");
    if (ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("grind", BotState::BOT_STATE_COMBAT) ||
        (hold != positions.end() && hold->second.isSet())) return {};

    auto* value = context->GetValue<KtFissureSnapshot>("kt fissure snapshot");
    auto snapshot = value->Get();
    uint32 const now = WorldTimer::getMSTime();
    if (snapshot.NeedsRefresh(now, bot->GetMapId(), bot->GetInstanceId()))
    {
        snapshot = {true, bot->GetMapId(), bot->GetInstanceId(), now, {}};
        std::list<Creature*> fissures;
        bot->GetCreatureListWithEntryInGrid(fissures, KT_FISSURE_ENTRY, 40.0f);
        for (Creature* fissure : fissures)
            if (fissure && fissure->IsAlive() && fissure->IsInMap(bot))
                snapshot.guids.push_back(fissure->GetObjectGuid().GetRawValue());
        value->Set(snapshot);
    }
    std::list<HazardPosition> hazards;
    for (uint64 guid : snapshot.guids)
    {
        Unit* fissure = ai->GetUnit(ObjectGuid(guid));
        if (fissure && fissure->GetEntry() == KT_FISSURE_ENTRY && fissure->IsAlive() &&
            fissure->IsInMap(bot))
            hazards.emplace_back(WorldPosition(fissure), KT_FISSURE_CLEARANCE);
    }
    return hazards;
}

bool ai::InsideKelThuzadFissure(PlayerbotAI* ai)
{
    WorldPosition const start(ai->GetBot());
    for (auto const& hazard : GetKelThuzadFissures(ai))
        if (std::fabs(start.getZ() - hazard.first.getZ()) <= hazard.second &&
            std::hypot(start.getX() - hazard.first.getX(), start.getY() - hazard.first.getY()) <= hazard.second)
            return true;
    return false;
}

namespace
{
bool FissurePathSafe(WorldPosition const& start, std::vector<WorldPosition> const& path,
    std::list<HazardPosition> const& hazards)
{
    if (path.empty()) return false;
    WorldPosition previous = start;
    float length = 0;
    for (auto const& next : path)
    {
        float const segment = previous.distance(next);
        length += segment;
        if (!std::isfinite(segment) || !std::isfinite(next.getZ()) ||
            std::fabs(next.getZ() - start.getZ()) > 3.0f || length > 20.0f) return false;
        for (auto const& hazard : hazards)
        {
            // Every route point can rise/fall three yards from the start.
            // Include that whole band before projecting a hazard onto the floor.
            float const height = std::fabs(hazard.first.getZ() - start.getZ());
            if (height > hazard.second + 3.0f) continue;
            // Check the continuous 3D segment as well as its floor projection.
            // Rising toward a cloud can be inward even while XY moves outward.
            float const x = previous.getX() - hazard.first.getX();
            float const y = previous.getY() - hazard.first.getY();
            float const z = previous.getZ() - hazard.first.getZ();
            float const dx = next.getX() - previous.getX();
            float const dy = next.getY() - previous.getY();
            float const dz = next.getZ() - previous.getZ();
            float const distance2 = x*x + y*y + z*z;
            float const radius2 = hazard.second * hazard.second;
            float const dot = x*dx + y*dy + z*dz;
            float const segment2 = dx*dx + dy*dy + dz*dz;
            if (distance2 < radius2)
            {
                if (dot < 0.0f) return false;
            }
            else
            {
                float const t = segment2 > 0 ? std::max(0.0f, std::min(1.0f, -dot / segment2)) : 0;
                if ((x+t*dx)*(x+t*dx) + (y+t*dy)*(y+t*dy) + (z+t*dz)*(z+t*dz) < radius2)
                    return false;
            }
            if (!KtFissureSegmentSafe({previous.getX(), previous.getY()}, {next.getX(), next.getY()},
                {hazard.first.getX(), hazard.first.getY(), hazard.second})) return false;
        }
        previous = next;
    }
    for (auto const& hazard : hazards)
        if (std::fabs(hazard.first.getZ() - previous.getZ()) <= hazard.second &&
            std::hypot(previous.getX() - hazard.first.getX(), previous.getY() - hazard.first.getY()) <= hazard.second)
            return false;
    return true;
}
}

bool KelThuzadFissureEscapeAction::isUseful()
{
    return MovementAction::isUseful() && InsideKelThuzadFissure(ai);
}

bool KelThuzadFissureEscapeAction::Execute(Event& /*event*/)
{
    if (!InsideKelThuzadFissure(ai) || !ai->CanMove() || bot->GetTransport() || bot->IsTaxiFlying()) return false;
    WorldPosition const start(bot);
    auto hazards = AI_VALUE(std::list<HazardPosition>, "hazards");
    auto fissures = GetKelThuzadFissures(ai);
    hazards.splice(hazards.end(), fissures);
    for (float radius : {8.0f, 12.0f})
        for (unsigned heading = 0; heading < 16; ++heading)
        {
            float const angle = 2 * M_PI_F * heading / 16;
            WorldPosition point = start + WorldPosition(0, radius * cos(angle), radius * sin(angle), 0);
            point.setZ(point.GetHeight());
            if (!std::isfinite(point.getZ()) || std::fabs(point.getZ() - start.getZ()) > 3.0f ||
                !IsMovingAllowed(533, point.getX(), point.getY(), point.getZ()) || IsHazardNearPosition(point) ||
                !bot->IsWithinLOS(point.getX(), point.getY(), point.getZ() + bot->GetCollisionHeight())) continue;
            auto path = start.GetPathTo(point, bot);
            if (!point.isPathTo(path) || !FissurePathSafe(start, path, hazards)) continue;
            // Dispatch the checked mesh route directly: generic MoveTo may reuse
            // a cached path or rewrite it after validation.
            bot->HandleEmoteState(0);
            if (bot->GetStandState() != UNIT_STAND_STATE_STAND) bot->SetStandState(UNIT_STAND_STATE_STAND);
            if (bot->IsNonMeleeSpellCasted(true, false, true)) ai->InterruptSpell(false);
            ai->StopMoving();
            WorldPosition const stopped(bot);
            path.insert(path.begin(), stopped);
            auto currentHazards = AI_VALUE(std::list<HazardPosition>, "hazards");
            auto currentFissures = GetKelThuzadFissures(ai);
            currentHazards.splice(currentHazards.end(), currentFissures);
            if (!FissurePathSafe(stopped, path, currentHazards)) return false;
            auto const points = WorldPosition().toPointsArray(path);
            Movement::MoveSplineInit init(*bot, "KelThuzadFissureEscape");
            init.MovebyPath(points);
            init.SetWalk(false);
            if (!init.Launch()) return false;
            AI_VALUE(LastMovement&, "last movement").lastPath.clear();
            ClearIdleState();
            WaitForReach(points);
            return true;
        }
    return false;
}
