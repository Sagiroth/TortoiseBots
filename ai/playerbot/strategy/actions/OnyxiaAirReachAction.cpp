#include "playerbot/playerbot.h"
#include "ReachTargetActions.h"
#include "OnyxiasLairDungeonActions.h"
#include "playerbot/OnyxiaAirPolicy.h"
#include "playerbot/OnyxiaTankPositionPolicy.h"
#include "playerbot/policy/raid/OnyxiaWhelpEggPolicy.h"
#include <sstream>
#include "playerbot/strategy/values/PositionValue.h"
#include "Movement/spline/MoveSplineInit.h"

using namespace ai;

bool ReachTargetAction::ReachAirborneOnyxia(Unit* target)
{
    if (!IsOnyxiaAirCombatTarget(ai, target)) return false;
    std::string searchDetail;
    auto fail = [&](char const* reason)
    {
        LogOnyxiaAir(ai, OnyxiaAirLog::Reach, reason, target, "spell=" + spellName + searchDetail);
        return false;
    };
    if (!ai->CanMove() || bot->GetTransport() || bot->IsTaxiFlying() || bot->IsFalling()) return fail("movement gate");
    if (OnyxiaBreathActive(ai)) return fail("Deep Breath gate");
    if (bot->IsNonMeleeSpellCasted(false, true, true)) return fail("casting gate");
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto const hold = positions.find("pull hold");
    if (ai->HasStrategy("stay", ai->GetState()) || ai->HasStrategy("follow", ai->GetState()) ||
        ai->HasStrategy("wait for attack", ai->GetState()) ||
        (hold != positions.end() && hold->second.isSet())) return fail("player hold");
    float maximum = range, minimum = 0;
    if (!spellName.empty()) ai->GetSpellRange(spellName, &maximum, &minimum);
    if (bot->GetCombatDistance(target) >= minimum && bot->GetCombatDistance(target) <= maximum &&
        bot->IsWithinLOSInMap(target, true)) return false;
    if (!bot->movespline->Finalized()) return true;

    uint32 const now = WorldTimer::getMSTime();
    if (lastOnyxiaAirRoute && uint32(now - lastOnyxiaAirRoute) < 1000) return false;
    lastOnyxiaAirRoute = now;
    WorldPosition const start(bot);
    auto floor = [](WorldPosition const& point)
    {
        return OnyxiaSpreadFloor(point.getX(), point.getY(), point.getZ()) ||
            OnyxiaTankCorridor({point.getX(), point.getY(), point.getZ()});
    };
    bool const entering = !floor(start);
    if (entering && !OnyxiaAirApproachFloor(start.getX(), start.getY(), start.getZ()))
        return fail("start outside approach floor/z");
    auto const eggs = CollectOnyxiaEggs(ai);
    auto eggSafe = [&eggs](WorldPosition const& point)
    {
        for (GameObject* egg : eggs)
            if (OnyxiaDist2d(point.getX(), point.getY(), egg->GetPositionX(), egg->GetPositionY()) < kOnyxiaEggDangerRadius)
                return false;
        return true;
    };
    if (!eggSafe(start)) return fail("start near egg trap");
    auto radius = [](WorldPosition const& p) { return std::hypot(p.getX() + 25.0f, p.getY() + 215.0f); };
    std::vector<WorldPosition> candidates;
    // Project onto the central floor, not the boss's elevated spline endpoint.
    // The core's spell range uses combat reach, including Onyxia's flight reach.
    unsigned badFloor = 0, badHazard = 0, badRange = 0, badLos = 0;
    for (float x = -53.0f; x <= 3.0f; x += 4.0f)
        for (float y = -235.0f; y <= -195.0f; y += 4.0f)
        {
            WorldPosition point(249, x, y, start.getZ());
            point.setZ(point.GetHeight());
            float const distance = point.distance(WorldPosition(target)) - bot->GetCombatReach() - target->GetCombatReach();
            if (!OnyxiaSpreadFloor(x, y, point.getZ())) { ++badFloor; continue; }
            if (IsHazardNearPosition(point)) { ++badHazard; continue; }
            if (!entering && (distance > maximum - 1.0f || distance < minimum + 1.0f)) { ++badRange; continue; }
            if (!entering && !bot->IsWithinLOSAtPosition(x, y, point.getZ(), target->GetPositionX(),
                target->GetPositionY(), target->GetPositionZ(), true, target->GetCollisionHeight()))
            { ++badLos; continue; }
            candidates.push_back(point);
        }
    searchDetail = "|floor_reject=" + std::to_string(badFloor) + "|hazard_reject=" + std::to_string(badHazard) +
        "|range_reject=" + std::to_string(badRange) + "|los_reject=" + std::to_string(badLos) +
        "|candidates=" + std::to_string(candidates.size());
    std::sort(candidates.begin(), candidates.end(), [&start](WorldPosition const& a, WorldPosition const& b)
        { return start.distance(a) < start.distance(b); });
    // Bound expensive path searches. A failed route never falls back to chasing
    // an airborne XYZ or starts a generic unreachable/drop-target timer.
    if (candidates.empty()) return fail("no candidate: range/LOS/z/hazard");
    size_t attempts = 0;
    for (WorldPosition const& point : candidates)
    {
        if (++attempts > 8) break;
        std::vector<WorldPosition> const path = start.GetPathTo(point, bot);
        if (path.size() < 2 || !point.isPathTo(path, 1.0f, 1.0f)) continue;
        bool safe = true;
        bool reachedFloor = !entering;
        WorldPosition previous = start;
        for (WorldPosition const& next : path)
        {
            unsigned const steps = std::max(1u, unsigned(std::ceil(previous.distance(next) / 2.0f)));
            for (unsigned i = 0; i <= steps; ++i)
            {
                float const t = float(i) / steps;
                WorldPosition sample(249, previous.getX() + t * (next.getX() - previous.getX()),
                    previous.getY() + t * (next.getY() - previous.getY()),
                    previous.getZ() + t * (next.getZ() - previous.getZ()));
                bool const central = floor(sample);
                bool const ingress = entering && !reachedFloor &&
                    OnyxiaAirApproachFloor(sample.getX(), sample.getY(), sample.getZ()) &&
                    radius(sample) <= radius(start) + 2.0f;
                if ((!central && !ingress) || !eggSafe(sample) ||
                    !IsMovingAllowed(249, sample.getX(), sample.getY(), sample.getZ()) || IsHazardNearPosition(sample))
                { safe = false; break; }
                if (central) reachedFloor = true;
            }
            if (!safe) break;
            previous = next;
        }
        if (!safe) continue;
        Movement::PointsArray points;
        for (WorldPosition const& step : path) points.emplace_back(step.getX(), step.getY(), step.getZ());
        Movement::MoveSplineInit init(*bot, "onyxia air reach");
        init.MovebyPath(points);
        init.SetWalk(false);
        init.SetFacingGUID(target->GetObjectGuid().GetRawValue());
        init.Launch();
        LogOnyxiaAir(ai, OnyxiaAirLog::Reach, entering ? "returning to floor" : "started", target, "spell=" + spellName);
        return true;
    }
    return fail("paths rejected: incomplete/floor/egg/hazard/movement gate");
}

void ai::LogOnyxiaAir(PlayerbotAI* ai, OnyxiaAirLog kind, char const* reason, Unit* target, std::string const& detail)
{
    Player* bot = ai->GetBot();
    if (!bot || bot->GetMapId() != 249) return;
    AiObjectContext* context = ai->GetAiObjectContext();
    OnyxiaAirCombatState& state = AI_VALUE(OnyxiaAirCombatState&, "onyxia air combat state");
    uint32 const now = WorldTimer::getMSTime();
    auto& last = state.lastLog[static_cast<unsigned>(kind)];
    uint32 const interval = kind == OnyxiaAirLog::State ? 10000 : 2000;
    if (last && uint32(now - last) < interval) return;
    last = now;
    static char const* const names[] = {"OnyxiaAirRecover", "OnyxiaAirState", "OnyxiaAirReach", "OnyxiaAirCast"};
    std::ostringstream out;
    out << "map=" << bot->GetMapId() << "|instance=" << bot->GetInstanceId()
        << "|xyz=" << bot->GetPositionX() << '/' << bot->GetPositionY() << '/' << bot->GetPositionZ()
        << "|combat=" << bot->IsInCombat() << "|engine=" << unsigned(ai->GetState())
        << "|selection=" << bot->GetSelectionGuid().GetRawValue()
        << "|victim=" << (bot->GetVictim() ? bot->GetVictim()->GetObjectGuid().GetRawValue() : 0);
    if (target)
        out << "|entry=" << target->GetEntry() << "|target=" << target->GetObjectGuid().GetRawValue()
            << "|dist=" << bot->GetCombatDistance(target) << "|dz=" << target->GetPositionZ() - bot->GetPositionZ()
            << "|los=" << bot->IsWithinLOSInMap(target, true);
    if (!detail.empty()) out << '|' << detail;
    sPlayerbotAIConfig.logEvent(ai, names[static_cast<unsigned>(kind)], reason, out.str());
}

void ai::TraceOnyxiaAirAction(PlayerbotAI* ai, Action* action, char const* stage, bool success)
{
    if (ai->GetBot()->GetMapId() != 249 || !action || !OnyxiaRangedFocusEnabled(ai)) return;
    auto* cast = dynamic_cast<CastSpellAction*>(action);
    auto* reach = dynamic_cast<ReachTargetAction*>(action);
    if ((!cast && !reach) || (cast && action->GetTargetName() != "current target")) return;
    if (success && std::string(stage) != "executed") return;
    // Ordinary rotation predicates (aura already present, cooldown not due)
    // are not failed cast attempts. Retain the shoot usefulness gate, where
    // missing ammo or a stale auto-repeat otherwise leaves no explanation.
    if (!success && std::string(stage) == "useful" && cast &&
        action->getName() != "auto shot" && action->getName() != "shoot") return;
    Unit* target = action->GetTarget();
    std::string const spell = cast ? action->getName() : reach->GetSpellName();
    float maximum = 0, minimum = 0;
    bool const rangeKnown = ai->GetSpellRange(spell, &maximum, &minimum);
    std::ostringstream detail;
    detail << "stage=" << stage << "|action=" << action->getName() << "|spell=" << spell
        << "|range=" << minimum << '/' << maximum << "|core_result=" << unsigned(ai->GetLastCastCheckFail());
    char const* reason = success ? "started" : !target ? "missing target" :
        !ai->GetBot()->IsWithinLOSInMap(target, true) ? "LOS" :
        (rangeKnown && (ai->GetBot()->GetCombatDistance(target) < minimum || ai->GetBot()->GetCombatDistance(target) > maximum)) ? "range" : "gate";
    // Reach execution names its precise path/gate failure itself.
    if (reach && std::string(stage) == "executed") return;
    LogOnyxiaAir(ai, cast ? OnyxiaAirLog::Cast : OnyxiaAirLog::Reach, reason, target, detail.str());
}
