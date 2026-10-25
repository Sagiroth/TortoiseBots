
#include "playerbot/playerbot.h"
#include "playerbot/strategy/values/AoeValues.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
#include "OnyxiasLairDungeonActions.h"
#include "playerbot/OnyxiaAirCombat.h"

using namespace ai;
#include "playerbot/ServerFacade.h"
#include "playerbot/policy/raid/OnyxiaBreathPolicy.h"
#include "playerbot/policy/raid/OnyxiaWhelpEggPolicy.h"
#include "playerbot/OnyxiaAirPolicy.h"
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
        Player* victim=dynamic_cast<Player*>(unit->GetVictim());
        if(!OnyxiaAirWhelpAllowed(ai->IsRanged(bot),victim && ai->IsHeal(victim),
            victim && bot->GetGroup() && victim->GetGroup()==bot->GetGroup())) continue;
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
    return !PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsHeal(bot);
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
    if (SuppressOnyxiaAirAoe(ai, action)) return 0.0f;
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
    auto scan=ScanOnyxiaWhelps(ai,bot);
    return scan.bossAirborne && (!ai->IsRanged(bot) || scan.whelp==current) ? 0.f:1.f;
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

#include "playerbot/playerbot.h"
#include "OnyxiasLairDungeonActions.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/OnyxiaAirPolicy.h"
#include "playerbot/CombatSpreadPolicy.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/values/RaidDpsTargetPolicy.h"
#include "playerbot/strategy/values/LeastHpTargetValue.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"

using namespace ai;

namespace
{
bool EngagedAirborneOnyxia(PlayerbotAI* ai, Unit* boss)
{
    Player* bot = ai->GetBot();
    if (!bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported() || bot->GetMapId() != 249 ||
        !boss || !boss->IsInWorld() || boss->GetEntry() != 10184 || !boss->IsInMap(bot) ||
        !bot->IsValidAttackTarget(boss) ||
        !OnyxiaAirPhase(boss->IsAlive(), boss->IsInCombat(), boss->HasAura(17131), boss->IsFlying())) return false;
    Creature* creature = dynamic_cast<Creature*>(boss);
    if (!creature || creature->IsInEvadeMode()) return false;
    auto engaged = [boss](Player* member)
    {
        return member && member->IsAlive() && member->IsInMap(boss) &&
            (boss->GetVictim() == member || boss->GetThreatManager().getThreat(member) > 0.0f);
    };
    if (engaged(bot)) return true;
    for (Player* member : LiveGroupMembers(bot->GetGroup()))
        if (engaged(member)) return true;
    return false;
}
bool OnyxiaAirDamageRole(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    return bot->GetMapId() == 249 && ai->IsRanged(bot) && !ai->IsTank(bot) && !ai->IsHeal(bot) &&
        (ai->HasStrategy("onyxia", BotState::BOT_STATE_COMBAT) ||
         ai->HasStrategy("onyxia's lair", BotState::BOT_STATE_COMBAT) ||
         ai->HasStrategy("onyxia air spread", BotState::BOT_STATE_COMBAT) ||
         ai->HasStrategy("onyxia whelps", BotState::BOT_STATE_COMBAT));
}
}

Unit* ai::GetAirborneOnyxia(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (!bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported() || bot->GetMapId() != 249)
        return nullptr;
    // Independent of the bot's combat flag and its short-range attackers cache.
    // The cached boss scan still requires this group's actual threat or victim.
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* boss = AI_VALUE(Unit*, "boss target");
    return EngagedAirborneOnyxia(ai, boss) ? boss : nullptr;
}

bool ai::IsOnyxiaAirCombatTarget(PlayerbotAI* ai, Unit* target)
{
    return OnyxiaAirDamageRole(ai) && EngagedAirborneOnyxia(ai, target);
}

bool ai::OnyxiaRangedFocusEnabled(PlayerbotAI* ai)
{
    return OnyxiaAirDamageRole(ai) && GetAirborneOnyxia(ai);
}

void ai::UpdateOnyxiaAirCombat(PlayerbotAI* ai)
{
    if (!OnyxiaAirDamageRole(ai) || !ai->GetBot()->IsAlive() || !ai->GetBot()->IsInWorld()) return;
    Unit* boss = GetAirborneOnyxia(ai);
    if (!boss)
    {
        // Ground phases are expected, not a failed airborne recovery.
        Unit* known = ai->GetAiObjectContext()->GetValue<Unit*>("boss target")->Get();
        if (known && known->GetEntry() == 10184 && (known->IsFlying() || known->HasAura(17131)))
            LogOnyxiaAir(ai, OnyxiaAirLog::State, "airborne boss not engaged/eligible", known);
        return;
    }
    LogOnyxiaAir(ai, OnyxiaAirLog::State, "active", boss);
    Player* bot = ai->GetBot();
    AiObjectContext* context = ai->GetAiObjectContext();
    // Preserve valid player orders and the existing healer-whelp rescue priority.
    Unit* order = ai->GetUnit(AI_VALUE(ObjectGuid, "explicit attack target"));
    Unit* marked = AI_VALUE(Unit*, "rti target");
    for (Unit* ordered : {order, marked})
        if (ordered && ordered != boss && PossibleAttackTargetsValue::IsValid(ordered, bot,
            sPlayerbotAIConfig.sightDistance, false, false)) return;
    Unit* target = GetOnyxiaRangedTarget(ai);
    if (!target) return;
    bool const coreCombat = bot->IsInCombat();
    bool const combatEngine = ai->GetState() == BotState::BOT_STATE_COMBAT;
    bool const changed = AI_VALUE(Unit*, "current target") != target || bot->GetSelectionGuid() != target->getObjectGuid();
    if (!coreCombat) bot->SetInCombatWith(boss);
    if (!combatEngine)
    {
        ai->OnCombatStarted();
        ai->ClearActionClock();
    }
    if (changed)
    {
        context->GetValue<Unit*>("current target")->Set(target);
        bot->SetSelectionGuid(target->getObjectGuid());
        // These values may still describe the target from before the handoff.
        context->GetValue<bool>("invalid target", "current target")->Reset();
        context->GetValue<Unit*>("dps target")->Reset();
    }
    // Auto-repeat owns its original spell target, not the current selection.
    // Let the normal shot/wand action restart it after a genuine handoff.
    if (Spell* repeat = bot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
        if (repeat->m_targets.getUnitTargetGuid() != target->GetObjectGuid())
        {
            bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
            context->GetValue<uint32>("active spell")->Reset();
        }
    OnyxiaAirCombatState& state = AI_VALUE(OnyxiaAirCombatState&, "onyxia air combat state");
    if ((!combatEngine || !coreCombat || changed) &&
        (!combatEngine || state.recovered != target->GetObjectGuid().GetRawValue()))
    {
        state.recovered = target->GetObjectGuid().GetRawValue();
        LogOnyxiaAir(ai, OnyxiaAirLog::Recovery, combatEngine ? "target/core combat" : "combat engine", target);
    }
}

Unit* ai::GetOnyxiaRangedTarget(PlayerbotAI* ai)
{
    if (!OnyxiaRangedFocusEnabled(ai)) return nullptr;
    AiObjectContext* context = ai->GetAiObjectContext();
    OnyxiaAirCombatState& state = AI_VALUE(OnyxiaAirCombatState&, "onyxia air combat state");
    uint32 const now = WorldTimer::getMSTime();
    // Recovery, assist, multipliers and the rotation share one decision. A
    // changed victim/cache during that decision must not flip boss/whelp back.
    if (state.target && uint32(now - state.choiceAt) < 1000)
        if (Unit* target = ai->GetUnit(ObjectGuid(state.target)))
            if (target->IsAlive() && target->IsInMap(ai->GetBot()) &&
                (target->GetEntry() == 10184 ||
                 (PossibleAttackTargetsValue::IsValid(target, ai->GetBot(), sPlayerbotAIConfig.sightDistance, false, false) &&
                  !IsRaidCrowdControlMarkedTarget(ai, target)))) return target;
    Unit* target = GetOnyxiaWhelpTarget(ai);
    if (!target) target = GetAirborneOnyxia(ai);
    state.target = target ? target->GetObjectGuid().GetRawValue() : 0;
    state.choiceAt = now;
    return target;
}

bool ai::SuppressOnyxiaAirAoe(PlayerbotAI* ai, Action* action)
{
    if (!action || action->GetThreatType() != ActionThreatType::ACTION_THREAT_AOE ||
        !OnyxiaRangedFocusEnabled(ai) || dynamic_cast<CastHealingSpellAction*>(action)) return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    if (!IsOnyxiaAirCombatTarget(ai, AI_VALUE(Unit*, "current target"))) return false;
    OnyxiaAirCombatState& state = AI_VALUE(OnyxiaAirCombatState&, "onyxia air combat state");
    uint32 const now = WorldTimer::getMSTime();
    if (state.aoeAt && uint32(now - state.aoeAt) < 1000) return state.aoeWhelps < 3;
    unsigned whelps = 0;
    for (ObjectGuid const& guid : AoeCountValue::FindMaxDensity(ai->GetBot()))
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->GetEntry() == 11262 && unit->IsAlive() && unit->IsInMap(ai->GetBot()) &&
            unit->IsInCombat() && ai->GetBot()->GetDistance(unit) <= 35.0f &&
            !IsRaidCrowdControlMarkedTarget(ai, unit)) ++whelps;
    }
    state.aoeAt = now;
    state.aoeWhelps = whelps;
    return whelps < 3;
}

bool ai::OnyxiaAirNeedsAssist(PlayerbotAI* ai)
{
    Unit* target = GetOnyxiaRangedTarget(ai);
    if (!target) return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    return AI_VALUE(Unit*, "current target") != target || ai->GetBot()->GetVictim() != target;
}

bool ai::OnyxiaAirSpreadEnabled(PlayerbotAI* ai)
{
    if (OnyxiaBreathActive(ai)) return false;
    Player* bot = ai->GetBot();
    if (!ai->HasStrategy("onyxia air spread", BotState::BOT_STATE_COMBAT) ||
        ai->IsTank(bot) || (!ai->IsRanged(bot) && !ai->IsHeal(bot)) || !GetAirborneOnyxia(ai))
        return false;
    if (!OnyxiaSpreadFloor(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()))
        return false;
    bool hasCompanion = false;
    for (Player* member : LiveGroupMembers(bot->GetGroup()))
        if (member && member != bot && member->IsInWorld() && member->IsAlive() && member->IsInMap(bot))
            hasCompanion = true;
    if (!hasCompanion)
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto hold = positions.find("pull hold");
    return !ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) &&
        !ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT) &&
        !ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT) &&
        !ai->HasStrategy("grind", BotState::BOT_STATE_COMBAT) &&
        (hold == positions.end() || !hold->second.isSet());
}

bool OnyxiaAirSpreadAction::isUseful()
{
    if (!OnyxiaAirSpreadEnabled(ai) || IsSpreadOnCooldown(WorldTimer::getMSTime(), lastStep))
        return false;
    for (Player* member : LiveGroupMembers(bot->GetGroup()))
        if (member && member != bot && member->IsInWorld() && member->IsAlive() && member->IsInMap(bot) &&
            sServerFacade.getDistance2d(bot, member) < kSpreadSettledDistance)
            return true;
    return false;
}

bool OnyxiaAirSpreadAction::Execute(Event& event)
{
    if (!isUseful())
        return false;
    lastStep = WorldTimer::getMSTime(); // Also bound retries when the floor is crowded or paths fail.
    WorldPosition const start(bot);
    if (!OnyxiaSpreadFloor(start.getX(), start.getY(), start.getZ()))
        return false;
    std::vector<RaidRunoutPoint> members;
    for (Player* member : LiveGroupMembers(bot->GetGroup()))
        if (member && member != bot && member->IsInWorld() && member->IsAlive() && member->IsInMap(bot))
            members.push_back({ member->GetPositionX(), member->GetPositionY() });
    float const preferred = (bot->GetGUIDLow() % 16) * (2.0f * M_PI_F / 16.0f);
    for (float radius : { 6.0f, 12.0f, 18.0f })
    {
        for (uint32 step = 0; step < 16; ++step)
        {
            float const angle = preferred + step * (2.0f * M_PI_F / 16.0f);
            WorldPosition point(249, start.getX() + radius * cos(angle), start.getY() + radius * sin(angle), start.getZ());
            point.setZ(point.GetHeight());
            if (!OnyxiaSpreadFloor(point.getX(), point.getY(), point.getZ()) ||
                fabs(point.getZ() - start.getZ()) > 5.0f ||
                !RaidBombClearOfGroup({ point.getX(), point.getY() }, members, kSpreadSettledDistance + 1.0f) ||
                IsHazardNearPosition(point) ||
                !bot->IsWithinLOS(point.getX(), point.getY(), point.getZ() + bot->GetCollisionHeight()))
                continue;
            std::vector<WorldPosition> const path = start.GetPathTo(point, bot);
            if (!point.isPathTo(path))
                continue;
            bool safePath = true;
            for (WorldPosition const& waypoint : path)
                if (!OnyxiaSpreadFloor(waypoint.getX(), waypoint.getY(), waypoint.getZ()) || IsHazardNearPosition(waypoint))
                {
                    safePath = false;
                    break;
                }
            if (safePath && MoveTo(249, point.getX(), point.getY(), point.getZ(), false, IsReaction(), false, true))
            {
                lastStep = WorldTimer::getMSTime();
                if (IsReaction())
                    WaitForReach(start.distance(point));
                return true;
            }
        }
    }
    return false;
}


bool ai::OnyxiaWhelpSupportActive(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if ((!ai->HasStrategy("onyxia whelps", BotState::BOT_STATE_COMBAT) && !OnyxiaRangedFocusEnabled(ai)) ||
        (!OnyxiaWhelpRole(ai->IsTank(bot), ai->IsRanged(bot), ai->IsHeal(bot)) && !OnyxiaRangedFocusEnabled(ai)) || !GetAirborneOnyxia(ai))
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* explicitTarget = ai->GetUnit(AI_VALUE(ObjectGuid, "explicit attack target"));
    if (explicitTarget && PossibleAttackTargetsValue::IsValid(explicitTarget, bot,
        sPlayerbotAIConfig.sightDistance, false, false))
        return false;
    if (AI_VALUE(Unit*, "rti target"))
        return false;
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto hold = positions.find("pull hold");
    return !ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) &&
        !ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT) &&
        !ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT) &&
        !ai->HasStrategy("grind", BotState::BOT_STATE_COMBAT) &&
        (hold == positions.end() || !hold->second.isSet());
}

Unit* ai::GetOnyxiaWhelpTarget(PlayerbotAI* ai)
{
    if (!OnyxiaWhelpSupportActive(ai))
        return nullptr;
    class WhelpCcFilter : public FindLeastHpTargetStrategy
    {
    public:
        WhelpCcFilter(PlayerbotAI* ai) : FindLeastHpTargetStrategy(ai) {}
        using FindNonCcTargetStrategy::IsCcTarget;
    } filter(ai);
    Player* bot = ai->GetBot();
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* current = AI_VALUE(Unit*, "current target");
    Unit* result = nullptr;
    float best = -100000.0f;
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "possible attack targets"))
    {
        Unit* whelp = ai->GetUnit(guid);
        if (!whelp || whelp->GetEntry() != 11262 || !whelp->IsAlive() || !whelp->IsInMap(bot) ||
            !whelp->IsInCombat() || filter.IsCcTarget(whelp) ||
            !PossibleAttackTargetsValue::IsValid(whelp, bot, sPlayerbotAIConfig.sightDistance, false, true))
            continue;
        Unit* victim = whelp->GetVictim();
        Player* victimPlayer = dynamic_cast<Player*>(victim);
        if(!OnyxiaAirWhelpAllowed(ai->IsRanged(bot),victimPlayer && ai->IsHeal(victimPlayer),
            victimPlayer && bot->GetGroup() && victimPlayer->GetGroup()==bot->GetGroup())) continue;
        bool const needsPickup = !victimPlayer || !ai->IsTank(victimPlayer);
        float const score = OnyxiaWhelpScore(ai->IsTank(bot), needsPickup, whelp == current,
            sServerFacade.getDistance2d(bot, whelp));
        if (!result || score > best || (score == best && whelp->GetObjectGuid() < result->GetObjectGuid()))
        {
            result = whelp;
            best = score;
        }
    }
    return result;
}


Unit* ai::GetAssignedOnyxiaLandingTarget(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!bot || bot->GetMapId() != 249 || !bot->IsInWorld() || !bot->IsAlive() ||
        bot->IsBeingTeleported() || !bot->IsInCombat() || !ai->IsTank(bot) ||
        !ai->HasStrategy("onyxia landing tank", BotState::BOT_STATE_COMBAT))
        return nullptr;

    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* order = ai->GetUnit(AI_VALUE(ObjectGuid, "explicit attack target"));
    if (order && PossibleAttackTargetsValue::IsValid(order, bot,
        sPlayerbotAIConfig.sightDistance, false, false))
        return nullptr;
    if (AI_VALUE(Unit*, "rti target"))
        return nullptr;
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto hold = positions.find("pull hold");
    if (ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("grind", BotState::BOT_STATE_COMBAT) ||
        (hold != positions.end() && hold->second.isSet()))
        return nullptr;

    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
    {
        Unit* boss = ai->GetUnit(guid);
        // The assigned tank holds the boss in both ground phases; returning
        // to generic add targeting after a threat loss can drag her sideways.
        // Phase three also needs this priority after the scripted threat reset.
        if (boss && boss->GetEntry() == 10184 && boss->IsAlive() && boss->IsInCombat() &&
            boss->IsInMap(bot) &&
            !boss->IsFlying() && !boss->HasAura(17131) &&
            PossibleAttackTargetsValue::IsValid(boss, bot, sPlayerbotAIConfig.sightDistance, false, false))
            return boss;
    }
    return nullptr;
}

bool OnyxiaLandingAttackAction::isUseful()
{
    Unit* boss = GetTarget();
    return boss && boss != AI_VALUE(Unit*, "current target");
}
