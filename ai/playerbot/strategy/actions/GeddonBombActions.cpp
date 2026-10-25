#include "playerbot/playerbot.h"
#include "GeddonBombActions.h"
#include "DungeonActions.h"
#include "MoltenCoreDungeonActions.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/RaidBombRunoutPolicy.h"
#include "playerbot/policy/raid/GeddonInfernoPolicy.h"
#include "playerbot/strategy/values/LastMovementValue.h"
#include "Movement/spline/MoveSplineInit.h"
#include "Timer.h"
#include "Objects/DynamicObject.h"

using namespace ai;
namespace
{
GeddonBombPoint Point(WorldPosition const& p) { return {p.getX(), p.getY(), p.getZ()}; }
GeddonBombState& State(PlayerbotAI* ai)
{
    return ai->GetAiObjectContext()->GetValue<GeddonBombState&>("geddon bomb state")->Get();
}
std::list<HazardPosition> MoltenGround(Player* bot)
{
    std::list<HazardPosition> result;
    // Living Bomb's WEARER_CASTS_PROC_TRIGGER flag makes the carrier the
    // affective caster of 44060. EffectPersistentAA registers it on that player.
    // Include human and dead-but-present raid members, not only bot carriers.
    for (Player* member : LiveGroupMembers(bot->GetGroup()))
    {
        if (!member || !member->IsInWorld() || !member->IsInMap(bot)) continue;
        std::vector<DynamicObject*> areas;
        member->GetDynObjects(44060, SpellEffectIndex(0), areas);
        for (DynamicObject* area : areas)
            if (area && area->IsInMap(bot) && area->GetCasterGuid() == member->GetObjectGuid() &&
                area->GetSpellId() == 44060 && area->GetEffIndex() == 0 && area->GetDuration() > 0 &&
                std::isfinite(area->GetRadius()) && area->GetRadius() > 0)
                result.emplace_back(WorldPosition(area), area->GetRadius() + 2.0f);
    }
    return result;
}
}

bool ai::HasGeddonBombSpot(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    auto& state = State(ai);
    if (!state.configured) return false;
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || !bot->GetGroup() ||
        !state.InScope(bot->GetMapId(), bot->GetInstanceId(), bot->GetGroup()->GetId()))
    {
        state.Clear();
        return false;
    }
    return true;
}

bool ai::UpdateGeddonBombMovement(PlayerbotAI* ai)
{
    if (!HasGeddonBombSpot(ai)) return false;
    Player* bot = ai->GetBot();
    auto& state = State(ai);
    if (!bot->IsAlive()) { state.phase = GeddonBombPhase::Idle; return false; }
    Aura* aura = ai->GetAura(20475, bot);
    auto const previous = state.phase;
    state.Observe(aura != nullptr, aura ? aura->GetAuraApplyTime() : 0,
        Point(WorldPosition(bot)), WorldTimer::getMSTime());
    if (state.phase != previous)
        sPlayerbotAIConfig.logEvent(ai, "GeddonBombPhase",
            std::to_string(static_cast<int>(state.phase)), "0=idle,1=outbound,2=waiting,3=returning");
    return state.Active();
}

bool GeddonBombMoveAction::Execute(Event& /*event*/)
{
    if (!UpdateGeddonBombMovement(ai))
        return false;
    auto& state = State(ai);
    // The explicit bombspot command opts into this temporary override of parked
    // orders. Do not alter saved stay positions, targets, roles or strategies.
    bot->AttackStop();
    // PlayerbotAI::InterruptSpell skips channels and auto-repeat. The core API
    // clears all three slots, even while stationary or when routing fails.
    bot->InterruptNonMeleeSpells(false);
    if (!ai->CanMove() || bot->GetTransport() || bot->IsTaxiFlying()) return false;
    ai->StopMoving();
    bot->HandleEmoteState(0);
    bot->SetStandState(UNIT_STAND_STATE_STAND);

    WorldPosition const start(bot);
    bool const returning = state.phase == GeddonBombPhase::Returning;
    bool returningHome = returning;
    GeddonBombPoint desired = returning ? state.home : state.spot;
    WorldPosition destination(bot->GetMapId(), desired.x, desired.y, desired.z);
    auto fail = [&](char const* reason)
    {
        if (!state.failureLogged)
        {
            state.failureLogged = true;
            ai->TellError(ai->GetMaster(), std::string("Geddon bomb spot: ") + reason);
            sPlayerbotAIConfig.logEvent(ai, "GeddonBombBlocked", reason, returning ? "return" : "outbound");
        }
        SetDuration(500);
        return false;
    };
    Unit* geddon = nullptr;
    std::vector<Unit*> unengaged;
    std::vector<RaidRunoutPoint> members;
    for (Player* member : LiveGroupMembers(bot->GetGroup()))
        if (member && member != bot && member->IsAlive() && member->IsInMap(bot))
            members.push_back({member->GetPositionX(), member->GetPositionY()});
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "possible targets no los"))
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !unit->IsAlive() || !unit->IsInMap(bot)) continue;
        if (!unit->IsInCombat()) unengaged.push_back(unit);
        else if (unit->GetEntry() == 12056) geddon = unit;
    }
    Unit* inferno = geddon && (geddon->HasAura(19695) || geddon->HasUnitState(UNIT_STAT_ROOT)) ? geddon : nullptr;
    auto hazards = AI_VALUE(std::list<HazardPosition>, "hazards");
    // Preserve the old runout's known-unpulled-enemy clearance when choosing
    // automatic points beside the player's marker; also check their segments.
    for (Unit* unit : unengaged)
        hazards.emplace_back(WorldPosition(unit),sPlayerbotAIConfig.aggroDistance);
    auto ground = MoltenGround(bot);
    hazards.splice(hazards.end(), ground);
    auto clearEndpoint = [&](WorldPosition const& point)
    {
        if (!point.isValid() || std::fabs(point.getZ()-start.getZ()) > 5.0f ||
            !IsMovingAllowed(bot->GetMapId(),point.getX(),point.getY(),point.getZ()) ||
            !bot->IsWithinLOS(point.getX(),point.getY(),point.getZ()+bot->GetCollisionHeight())) return false;
        // Explosion radius is 18 yd; leave a 4-yd endpoint margin.
        if (!returningHome && !RaidBombClearOfGroup({point.getX(),point.getY()},members,22.0f)) return false;
        for (auto const& hazard : hazards)
            if (std::fabs(hazard.first.getZ()-point.getZ()) <= 5.0f &&
                RaidRunoutDistanceSquared({point.getX(),point.getY()},
                    {hazard.first.getX(),hazard.first.getY()}) <= hazard.second*hazard.second) return false;
        return true;
    };
    auto safePath = [&](std::vector<WorldPosition> const& path)
    {
        if (!destination.isPathTo(path, 1.0f, 1.0f) || !clearEndpoint(destination)) return false;
        WorldPosition previous = start;
        float length = 0;
        Unit* obstacle = inferno ? inferno : geddon;
        float minimum = 0;
        if (obstacle)
        {
            float const radius = inferno ? kGeddonInfernoClearance + bot->GetCombatReach() + obstacle->GetCombatReach()
                : obstacle->GetCombatReach() + bot->GetCombatReach();
            RaidRunoutPoint const center = {obstacle->GetPositionX(), obstacle->GetPositionY()};
            minimum = std::min(radius * radius, RaidRunoutDistanceSquared({start.getX(),start.getY()},center));
            if (returningHome && !inferno) minimum = std::min(minimum, RaidRunoutDistanceSquared({desired.x,desired.y},center));
            if (inferno && RaidRunoutDistanceSquared({desired.x,desired.y},center) < radius*radius) return false;
        }
        for (auto const& next : path)
        {
            if (!next.isValid() || std::fabs(next.getZ()-start.getZ()) > 5.0f) return false;
            for (auto const& hazard : hazards)
            {
                if (std::fabs(hazard.first.getZ()-start.getZ()) > 5.0f) continue;
                RaidRunoutPoint const center = {hazard.first.getX(), hazard.first.getY()};
                // Allow escape from an existing hazard, but never cut closer
                // through it on the way to either endpoint.
                float const clearance = std::min(hazard.second*hazard.second,
                    RaidRunoutDistanceSquared({start.getX(),start.getY()}, center));
                if (RaidRunoutSegmentDistanceSquared({previous.getX(),previous.getY()},
                    {next.getX(),next.getY()}, center) + 0.01f < clearance) return false;
            }
            length += previous.distance(next);
            if (length > 50.0f) return false;
            if (obstacle && RaidRunoutSegmentDistanceSquared({previous.getX(),previous.getY()},
                {next.getX(),next.getY()}, {obstacle->GetPositionX(),obstacle->GetPositionY()}) + 0.01f < minimum) return false;
            previous = next;
        }
        return true;
    };
    std::vector<GeddonBombPoint> candidates;
    if (returning) candidates.push_back(state.home);
    {
        // Reuse this application's safe point first; do not oscillate back to
        // the marker when old fire expires during an otherwise safe run.
        if (state.hasWaitingPoint) candidates.push_back(state.waitingPoint);
        candidates.push_back(state.spot);
        float const away = std::atan2(state.spot.y-state.home.y,state.spot.x-state.home.x);
        for (float radius : {8.0f, 12.0f})
            for (int step : {0,1,-1,2,-2,3,-3,4})
            {
                float const angle = away + step*0.78539816f;
                candidates.push_back({state.spot.x+radius*std::cos(angle),
                    state.spot.y+radius*std::sin(angle),state.spot.z});
            }
    }
    std::vector<WorldPosition> path;
    bool found = false;
    for (size_t index = 0; index < candidates.size(); ++index)
    {
        auto const& candidate = candidates[index];
        returningHome = returning && index == 0;
        desired = candidate;
        destination = WorldPosition(bot->GetMapId(),desired.x,desired.y,desired.z);
        if (!returningHome && GeddonBombDistance(candidate,state.spot) > 0.01f)
            destination.setZ(destination.GetHeight());
        desired.z = destination.getZ();
        if (!clearEndpoint(destination)) continue;
        path = start.GetPathTo(destination,bot);
        path.insert(path.begin(),start);
        if (!safePath(path) && geddon)
        {
            // Melee can step around the boss while retaining the same endpoint.
            float const angle = std::atan2(start.getY()-geddon->GetPositionY(),start.getX()-geddon->GetPositionX());
            float const radius = std::min(
                std::hypot(start.getX()-geddon->GetPositionX(),start.getY()-geddon->GetPositionY()),
                std::hypot(desired.x-geddon->GetPositionX(),desired.y-geddon->GetPositionY())) + 6.0f;
            for (float turn : {-1.04719755f,1.04719755f})
            {
                WorldPosition via(bot->GetMapId(),geddon->GetPositionX()+radius*std::cos(angle+turn),
                    geddon->GetPositionY()+radius*std::sin(angle+turn),start.getZ());
                via.setZ(via.GetHeight());
                auto first = start.GetPathTo(via,bot);
                auto second = via.GetPathTo(destination,bot);
                if (!via.isPathTo(first) || !destination.isPathTo(second)) continue;
                first.insert(first.begin(),start);first.insert(first.end(),second.begin(),second.end());
                if (safePath(first)) { path = first;break; }
            }
        }
        if (safePath(path)) { found = true;break; }
    }
    if (!found) return fail("no safe bounded route near the bomb marker or back to the saved position");
    if (!returningHome)
    {
        // If Inferno blocks the return, step clear of freshly spawned fire
        // beside the marker while preserving the original return destination.
        if (!state.hasWaitingPoint || GeddonBombDistance(state.waitingPoint,desired) > 0.1f)
            sPlayerbotAIConfig.logEvent(ai,"GeddonBombPoint",std::to_string(desired.x),std::to_string(desired.y));
        state.waitingPoint = desired;
        state.hasWaitingPoint = true;
        if (GeddonBombDistance(Point(start),desired) <= 1.5f)
        {
            if (!returning) state.phase = GeddonBombPhase::Waiting;
            SetDuration(500);
            return true;
        }
        if (!returning) state.phase = GeddonBombPhase::Outbound;
    }
    // Do not call generic MoveTo: it can replace a checked route with lastPath
    // or a shortcut. Launch the exact stopped-origin spline checked above.
    auto points = WorldPosition().toPointsArray(path);
    Movement::MoveSplineInit init(*bot, "GeddonBombSpot");
    init.MovebyPath(points); init.SetWalk(false);
    if (!init.Launch()) return fail("movement launch failed");
    AI_VALUE(LastMovement&, "last movement").lastPath.clear();
    ClearIdleState();
    SetDuration(500);
    return true;
}
