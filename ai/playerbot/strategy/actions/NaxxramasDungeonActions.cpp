
#include "playerbot/playerbot.h"
#include "NaxxramasDungeonActions.h"
#include "SapphironBlizzardHazards.h"
#include "Objects/DynamicObject.h"
#include "playerbot/SapphironBlizzardPolicy.h"

#include <vector>

using namespace ai;
#include "playerbot/MaexxnaRescuePolicy.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "playerbot/strategy/values/PossibleTargetsValue.h"
#include "Objects/TemporarySummon.h"

namespace
{
bool MaexxnaRescueActive(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (bot->GetMapId() != 533 || !bot->IsInWorld() || !bot->IsAlive() ||
        bot->IsBeingTeleported() || !bot->IsInCombat() ||
        !ai->HasStrategy("maexxna rescue", BotState::BOT_STATE_COMBAT))
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* order = ai->GetUnit(AI_VALUE(ObjectGuid, "explicit attack target"));
    bool ordered = (order && PossibleTargetsValue::IsValid(order, bot, true)) || AI_VALUE(Unit*, "rti target");
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto hold = positions.find("pull hold");
    ordered = ordered || ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("grind", BotState::BOT_STATE_COMBAT) ||
        (hold != positions.end() && hold->second.isSet());
    bool encounter = false;
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
    {
        Unit* boss = ai->GetUnit(guid);
        if (boss && boss->GetEntry() == 15952 && boss->IsAlive() && boss->IsInCombat() && boss->IsInMap(bot))
            encounter = true;
    }
    return MaexxnaRescueEligible(true, encounter, ai->IsTank(bot), ai->IsHeal(bot), ordered);
}

bool AccessibleMaexxnaWrap(PlayerbotAI* ai, Unit* target)
{
    Player* bot = ai->GetBot();
    if (!target || target->GetEntry() != 16486 || !target->IsAlive() || !target->IsInMap(bot) ||
        !PossibleTargetsValue::IsValid(target, bot, false) ||
        !PossibleAttackTargetsValue::IsPossibleTarget(target, bot, sPlayerbotAIConfig.sightDistance, false))
        return false;
    // Turtle summons the wrap from its prisoner; do not select an empty/test spawn.
    auto* summon = dynamic_cast<TemporarySummon*>(target);
    Player* prisoner = summon ? dynamic_cast<Player*>(ai->GetUnit(summon->GetSummonerGuid())) : nullptr;
    if (!prisoner || !prisoner->IsAlive() || !prisoner->IsInMap(bot) ||
        !bot->GetGroup() || prisoner->GetGroup() != bot->GetGroup() ||
        !prisoner->HasAura(28622) || !prisoner->HasAura(28627))
        return false;
    return MaexxnaWrapAccessible(ai->IsRanged(bot), bot->CanReachWithMeleeAutoAttack(target),
        bot->IsWithinLOSInMap(target), bot->GetDistance(target), ai->GetRange("spell"));
}
}

bool ai::ShouldDropMaexxnaRescueTarget(PlayerbotAI* ai, Unit* target)
{
    return target && target->GetEntry() == 16486 && MaexxnaRescueActive(ai) && !AccessibleMaexxnaWrap(ai, target);
}

bool ai::IsMaexxnaRescueTarget(PlayerbotAI* ai, Unit* target)
{
    return target && target->GetEntry() == 16486 && MaexxnaRescueActive(ai) && AccessibleMaexxnaWrap(ai, target);
}

Unit* ai::GetMaexxnaRescueTarget(PlayerbotAI* ai)
{
    if (!MaexxnaRescueActive(ai))
        return nullptr;
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* current = AI_VALUE(Unit*, "current target");
    if (AccessibleMaexxnaWrap(ai, current))
        return current;
    Unit* best = nullptr;
    float bestScore = -100000.0f;
    // Passive wraps have no victim/threat: use the existing cached nearby list,
    // not possible attack targets, and do not modify global attacker membership.
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "possible targets"))
    {
        Unit* wrap = ai->GetUnit(guid);
        if (!AccessibleMaexxnaWrap(ai, wrap))
            continue;
        float score = MaexxnaWrapScore(false, ai->GetBot()->GetDistance(wrap));
        if (!best || score > bestScore || (score == bestScore && wrap->GetObjectGuid() < best->GetObjectGuid()))
        {
            best = wrap;
            bestScore = score;
        }
    }
    return best;
}

bool MaexxnaRescueAction::isUseful()
{
    Unit* wrap = GetMaexxnaRescueTarget(ai);
    return wrap && wrap != AI_VALUE(Unit*, "current target");
}

#include "playerbot/LocustSwarmPolicy.h"
Unit* ai::GetActiveLocustSwarm(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (bot->GetMapId() != 533 || !ai->HasStrategy("locust swarm retreat", BotState::BOT_STATE_COMBAT))
        return nullptr;
    AiObjectContext* context = ai->GetAiObjectContext();
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto hold = positions.find("pull hold");
    bool const ordered = ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("grind", BotState::BOT_STATE_COMBAT) ||
        (hold != positions.end() && hold->second.isSet());
    if (!LocustSwarmEligible(ai->HasStrategy("locust swarm retreat", BotState::BOT_STATE_COMBAT),
        bot->IsInWorld() && !bot->IsBeingTeleported() && bot->IsAlive(),
        bot->GetMapId() == 533, bot->IsInCombat(), ai->IsTank(bot), ordered))
        return nullptr;

    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
    {
        Unit* boss = ai->GetUnit(guid);
        if (boss && boss->GetEntry() == 15956 && boss->IsInMap(bot) &&
            boss->IsAlive() && boss->IsInCombat() && boss->HasAura(28785))
            return boss;
    }
    return nullptr;
}

bool LocustSwarmRetreatAction::isUseful()
{
    Unit* boss = GetActiveLocustSwarm(ai);
    return MovementAction::isUseful() && boss &&
        LocustSwarmNeedsRetreat(bot->GetCombatDistance(boss));
}

bool LocustSwarmRetreatAction::Execute(Event& event)
{
    Unit* boss = GetActiveLocustSwarm(ai);
    if (!boss || !LocustSwarmNeedsRetreat(bot->GetCombatDistance(boss)))
        return false;

    // Stop an already-running chase even if all escape routes fail validation.
    ai->StopMoving();
    WorldPosition const start(bot);
    WorldPosition const center(boss);
    float const radius = kLocustSwarmClearance + 1.0f + bot->GetCombatReach() + boss->GetCombatReach();
    float const away = center.GetAngleTo(start);
    // Prefer directly outward. Destinations are on one fixed boss-centered
    // ring, not an additional step from the bot on every reaction tick.
    for (float offset : { 0.0f, 0.35f, -0.35f, 0.7f, -0.7f, 1.05f, -1.05f })
    {
        float const angle = away + offset;
        WorldPosition point = center + WorldPosition(0, radius * cos(angle), radius * sin(angle), 0.0f);
        point.setZ(point.GetHeight());
        if (fabs(point.getZ() - start.getZ()) > 5.0f || IsHazardNearPosition(point) ||
            !bot->IsWithinLOS(point.getX(), point.getY(), point.getZ() + bot->GetCollisionHeight()) ||
            !start.canPathTo(point, bot))
            continue;

        // Reject paths that initially dive closer to the moving boss, or cross
        // known hazards. Re-evaluate against the boss's current position each action.
        auto const path = start.GetPathTo(point, bot);
        if (!point.isPathTo(path))
            continue;
        bool safePath = true;
        for (WorldPosition const& waypoint : path)
            if (fabs(waypoint.getZ() - start.getZ()) > 5.0f || IsHazardNearPosition(waypoint) ||
                center.distance(waypoint) + 1.0f < center.distance(start))
                safePath = false;
        if (!safePath)
            continue;
        bool nearUnpulled = false;
        for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "possible targets"))
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->IsAlive() && !unit->IsInCombat() &&
                sServerFacade.getDistance2d(unit, point.getX(), point.getY()) < sPlayerbotAIConfig.aggroDistance)
            {
                nearUnpulled = true;
                break;
            }
        }
        if (!nearUnpulled && MoveTo(bot->GetMapId(), point.getX(), point.getY(), point.getZ(),
            false, IsReaction(), false, true))
        {
            if (IsReaction())
                WaitForReach(start.distance(point));
            return true;
        }
    }
    return false;
}


std::list<HazardPosition> ai::GetSapphironBlizzardHazards(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!bot || bot->GetMapId() != 533 || !bot->IsInWorld() || !bot->IsAlive() ||
        bot->IsBeingTeleported() || !bot->IsInCombat() ||
        !ai->HasStrategy("sapphiron blizzard", BotState::BOT_STATE_COMBAT))
        return {};

    AiObjectContext* context = ai->GetAiObjectContext();
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto hold = positions.find("pull hold");
    if (ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("grind", BotState::BOT_STATE_COMBAT) ||
        (hold != positions.end() && hold->second.isSet()))
        return {};

    bool engaged = false;
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
    {
        Unit* boss = ai->GetUnit(guid);
        if (boss && boss->GetEntry() == 15989 && boss->IsAlive() && boss->IsInCombat() && boss->IsInMap(bot))
        {
            engaged = true;
            break;
        }
    }
    if (!engaged)
        return {};

    auto* value = context->GetValue<SapphironBlizzardSnapshot>("sapphiron blizzard snapshot");
    auto snapshot = value->Get();
    uint32 const now = WorldTimer::getMSTime();
    if (snapshot.NeedsRefresh(now, bot->GetMapId(), bot->GetInstanceId()))
    {
        snapshot.BeginRefresh(now, bot->GetMapId(), bot->GetInstanceId());
        // Share bounded discovery across trigger, movement and hazard consumers.
        std::list<Creature*> blizzards;
        bot->GetCreatureListWithEntryInGrid(blizzards, SAPPHIRON_BLIZZARD_CREATURE_ENTRY,
            MAX_VISIBILITY_DISTANCE);
        for (Creature* blizzard : blizzards)
        {
            if (!blizzard || blizzard->GetEntry() != SAPPHIRON_BLIZZARD_CREATURE_ENTRY ||
                !blizzard->IsInMap(bot))
                continue;
            std::vector<DynamicObject*> damageAreas;
            blizzard->GetDynObjects(SAPPHIRON_BLIZZARD_DAMAGE_SPELL_ID,
                SpellEffectIndex(SAPPHIRON_BLIZZARD_DAMAGE_EFFECT_INDEX), damageAreas);
            for (DynamicObject* area : damageAreas)
                if (area)
                    snapshot.areas.push_back({blizzard->GetObjectGuid().GetRawValue(),
                                              area->GetObjectGuid().GetRawValue()});
        }
        value->Set(snapshot);
    }

    std::list<HazardPosition> hazards;
    for (auto const& id : snapshot.areas)
    {
        // Resolve live objects, not cached geometry: removal, expiration and
        // current bot movement must be visible even between discovery scans.
        DynamicObject* area = bot->GetMap()->GetDynamicObject(ObjectGuid(id.area));
        if (!area || area->GetCasterGuid() != ObjectGuid(id.caster) ||
            !IsActiveSapphironDamageArea(SAPPHIRON_BLIZZARD_CREATURE_ENTRY, area->GetSpellId(),
                area->GetEffIndex(), area->GetDuration(), area->GetRadius()))
            continue;
        hazards.emplace_back(WorldPosition(area), area->GetRadius() + 2.0f);
    }
    return hazards;
}

bool ai::SapphironBlizzardTooClose(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!bot)
        return false;
    for (HazardPosition const& hazard : GetSapphironBlizzardHazards(ai))
        if (WorldPosition(bot).distance(hazard.first) <= hazard.second)
            return true;
    return false;
}
