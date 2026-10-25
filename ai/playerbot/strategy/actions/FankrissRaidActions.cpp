#include "playerbot/playerbot.h"
#include "FankrissRaidActions.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/policy/raid/FankrissRaidPolicy.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "Movement/spline/MoveSpline.h"
#include "Movement/spline/MoveSplineInit.h"

using namespace ai;
namespace
{
bool Eligible(PlayerbotAI* ai, char const* strategy)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    return bot && bot->GetMapId() == 531 && bot->IsInWorld() && bot->IsAlive() &&
        !bot->IsBeingTeleported() && bot->GetGroup() &&
        ai->HasStrategy(strategy, BotState::BOT_STATE_COMBAT);
}
Unit* ActiveBoss(PlayerbotAI* ai)
{
    return ai->GetAiObjectContext()->GetValue<Unit*>("fankriss active boss")->Get();
}
bool IsGroupBoss(Player* bot, Unit* boss)
{
    if (!boss || boss->GetEntry() != 15510 || !boss->IsInMap(bot) || !boss->IsAlive() ||
        !boss->IsInCombat() || !boss->IsCreature() || static_cast<Creature*>(boss)->IsInEvadeMode()) return false;
    Unit* victim = boss->GetVictim();
    return victim && victim->IsPlayer() && static_cast<Player*>(victim)->GetGroup() == bot->GetGroup();
}
std::vector<char const*> HealingSpells(Player* bot)
{
    switch (bot->GetClass())
    {
        case CLASS_PRIEST: return {"flash heal", "greater heal", "heal", "lesser heal"};
        case CLASS_PALADIN: return {"flash of light", "holy light"};
        case CLASS_SHAMAN: return {"lesser healing wave", "healing wave"};
        case CLASS_DRUID: return {"healing touch", "regrowth"};
        default: return {};
    }
}
bool HealInRange(PlayerbotAI* ai, Unit* target, char const* name)
{
    Player* bot = ai->GetBot();
    float range = 0;
    return ai->GetSpellRange(name, &range) && range > 0 &&
        bot->GetDistance(target, SizeFactor::CombatReach) <= range && bot->IsWithinLOSInMap(target, true);
}
bool ReadyHealHere(PlayerbotAI* ai)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* patient = AI_VALUE(Unit*, "party member to heal");
    if (!FankrissHealReachable(ai, patient)) patient = ai->GetBot();
    if (!FankrissHealReachable(ai, patient)) return false;
    for (char const* spell : HealingSpells(ai->GetBot()))
    {
        uint32 const id = AI_VALUE2(uint32, "spell id", spell);
        if (!id || !ai->GetBot()->HasSpell(id) || !HealInRange(ai, patient, spell)) continue;
        SpellCastResult reason = SPELL_CAST_OK;
        // MOVING is checked before range in CheckCanCastSpell: geometry above
        // is mandatory. Do not bypass power, cooldown or control restrictions.
        if (ai->CheckCanCastSpell(id, patient, 0, true, nullptr, false, false, false, &reason) ||
            reason == SPELL_FAILED_MOVING)
        {
            // The AI movement guard runs before the core's silence/aura checks.
            // Probe those too without starting a cast or changing movement.
            SpellEntry const* entry = sSpellTemplate.LookupEntry<SpellEntry>(id);
            if (!entry) continue;
            Spell probe(ai->GetBot(), entry, false);
            probe.m_targets.setUnitTarget(patient);
            if (probe.CheckCast(true) == SPELL_CAST_OK) return true;
        }
    }
    return false;
}
class FankrissRaidMultiplier : public Multiplier
{
public:
    FankrissRaidMultiplier(PlayerbotAI* ai) : Multiplier(ai, "fankriss raid positioning") {}
    float GetValue(Action* action) override
    {
        if (!action) return 1.0f;
        std::string const name = action->getName();
        if (!FankrissOrdinaryMovement(name) && name != "dps aoe" && name != "reach party member to heal") return 1.0f;
        bool const stack = GetFankrissStackBoss(ai) != nullptr;
        bool const healing = FankrissHealingActive(ai);
        bool const healHere = healing && (name == "flee" || name == "flee to master" || name == "reach party member to heal") && ReadyHealHere(ai);
        return FankrissMovementBlocked(name, stack, healing, healHere) ? 0.0f : 1.0f;
    }
};
}

Unit* FankrissActiveBossValue::Calculate()
{
    if (!bot->GetGroup() || bot->GetMapId() != 531) return nullptr;
    // A web can put the healer outside its local attackers cache. The raid
    // roster still identifies the engaged boss; never scan for idle creatures.
    for (Player* member : LiveGroupMembers(bot->GetGroup()))
    {
        if (!member->IsInMap(bot)) continue;
        if (IsGroupBoss(bot, member->GetVictim())) return member->GetVictim();
        for (Unit* attacker : member->GetAttackers())
            if (IsGroupBoss(bot, attacker)) return attacker;
    }
    return nullptr;
}
Unit* ai::GetFankrissStackBoss(PlayerbotAI* ai)
{
    if (!Eligible(ai, "fankriss stack")) return nullptr;
    Player* bot = ai->GetBot();
    if (ai->IsTank(bot) || ai->IsHeal(bot) || !ai->IsMelee(bot)) return nullptr;
    AiObjectContext* context = ai->GetAiObjectContext();
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto const hold = positions.find("pull hold");
    if (ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT) ||
        (hold != positions.end() && hold->second.isSet())) return nullptr;
    Unit* boss = ActiveBoss(ai);
    if (!IsGroupBoss(bot, boss)) return nullptr;
    for (Unit* target : {ai->GetUnit(AI_VALUE(ObjectGuid, "explicit attack target")), AI_VALUE(Unit*, "rti target")})
        if (target && target != boss && PossibleAttackTargetsValue::IsValid(target, bot,
            sPlayerbotAIConfig.sightDistance, false, false)) return nullptr;
    return boss;
}
bool ai::FankrissHealingActive(PlayerbotAI* ai)
{
    return Eligible(ai, "fankriss healing") && ai->IsHeal(ai->GetBot()) && IsGroupBoss(ai->GetBot(), ActiveBoss(ai));
}
bool ai::FankrissHealReachable(PlayerbotAI* ai, Unit* target)
{
    Player* bot = ai->GetBot();
    if (!target || !target->IsAlive() || !target->IsInMap(bot) ||
        target->GetHealth() >= target->GetMaxHealth() || !bot->IsFriendlyTo(target)) return false;
    for (char const* name : HealingSpells(bot))
        if (HealInRange(ai, target, name)) return true;
    return false;
}
void ai::UpdateFankrissHealing(PlayerbotAI* ai)
{
    if (!FankrissHealingActive(ai)) return;
    Player* bot = ai->GetBot();
    if (!ai->CanMove() || bot->IsFalling() || bot->IsTaxiFlying() || bot->GetTransport() ||
        (bot->movespline->Finalized() && !sServerFacade.isMoving(bot))) return;
    auto const motion = bot->GetMotionMaster()->GetCurrentMovementGeneratorType();
    if (motion != POINT_MOTION_TYPE && motion != FOLLOW_MOTION_TYPE && motion != CHASE_MOTION_TYPE) return;
    AiObjectContext* context = ai->GetAiObjectContext();
    for (HazardPosition const& hazard : AI_VALUE(std::list<HazardPosition>, "hazards"))
        if (WorldPosition(bot).distance(hazard.first) <= hazard.second + 1.0f) return;
    if (!ReadyHealHere(ai)) return;
    ai->StopMoving();
    ai->ClearActionClock();
}
bool FankrissStackAction::isUseful()
{
    Unit* boss = GetFankrissStackBoss(ai);
    if (!boss || !ai->CanMove()) return false;
    auto goal = FankrissRear(boss->GetPositionX(), boss->GetPositionY(), boss->GetOrientation(),
        bot->GetCombatReach(boss, true, 0));
    bool const atRear = FankrissAtRear(bot->GetPositionX(), bot->GetPositionY(), goal,
        bot->CanReachWithMeleeAutoAttack(boss), !boss->HasInArc(bot, M_PI_F));
    if (atRear) return bot->GetVictim() != boss || !bot->movespline->Finalized();
    // Do not replace the same spline every AI tick or starve the class rotation.
    return bot->movespline->Finalized() || std::hypot(goal.x - lastX, goal.y - lastY) > 2.0f ||
        WorldTimer::getMSTimeDiff(lastMove, WorldTimer::getMSTime()) > 5000;
}
bool FankrissStackAction::Execute(Event& event)
{
    Unit* boss = GetFankrissStackBoss(ai);
    if (!boss || !ai->CanMove() || bot->IsFalling() || bot->GetTransport() || bot->IsTaxiFlying()) return false;
    auto goal = FankrissRear(boss->GetPositionX(), boss->GetPositionY(), boss->GetOrientation(),
        bot->GetCombatReach(boss, true, 0));
    if (FankrissAtRear(bot->GetPositionX(), bot->GetPositionY(), goal,
        bot->CanReachWithMeleeAutoAttack(boss), !boss->HasInArc(bot, M_PI_F)))
    {
        ai->StopMoving();
        SET_AI_VALUE(Unit*, "current target", boss);
        bot->SetSelectionGuid(boss->GetObjectGuid());
        bot->Attack(boss, true);
        bot->SetFacingToObject(boss);
        SetDuration(100);
        return true;
    }
    WorldPosition const start(bot);
    WorldPosition destination(bot->GetMapId(), goal.x, goal.y, boss->GetPositionZ());
    destination.setZ(destination.GetHeight());
    if (std::abs(destination.getZ() - boss->GetPositionZ()) > 3.0f ||
        !IsMovingAllowed(destination.getMapId(), destination.getX(), destination.getY(), destination.getZ())) return false;
    auto const path = start.GetPathTo(destination, bot);
    if (path.size() < 2 || !destination.isPathTo(path, 1.0f, 1.0f)) return false;
    auto hazards = AI_VALUE(std::list<HazardPosition>, "hazards");
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "possible targets no los"))
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit != boss && unit->IsAlive() && unit->IsInMap(bot) && !unit->IsInCombat())
            hazards.emplace_back(WorldPosition(unit), sPlayerbotAIConfig.aggroDistance);
    }
    WorldPosition previous = start;
    for (auto const& point : path)
    {
        unsigned steps = std::max(1u, unsigned(std::ceil(previous.distance(point) / 2.0f)));
        for (unsigned i = 1; i <= steps; ++i)
        {
            float t = float(i) / steps;
            WorldPosition sample(bot->GetMapId(), previous.getX() + t * (point.getX() - previous.getX()),
                previous.getY() + t * (point.getY() - previous.getY()), previous.getZ() + t * (point.getZ() - previous.getZ()));
            for (auto const& hazard : hazards)
                if (sample.distance(hazard.first) <= hazard.second + 1.0f) return false;
        }
        previous = point;
    }
    ai->StopMoving();
    SET_AI_VALUE(Unit*, "current target", boss);
    bot->SetSelectionGuid(boss->GetObjectGuid());
    bot->Attack(boss, true);
    Movement::PointsArray points;
    for (auto const& point : path) points.push_back(G3D::Vector3(point.getX(), point.getY(), point.getZ()));
    Movement::MoveSplineInit init(*bot, "FankrissRearStack");
    init.MovebyPath(points);
    init.SetWalk(false);
    init.SetFacingGUID(boss->GetObjectGuid().GetRawValue());
    init.Launch();
    lastX = goal.x; lastY = goal.y; lastMove = WorldTimer::getMSTime();
    SetDuration(100);
    return true;
}
bool FankrissStackTrigger::IsActive()
{
    Action* action = ai->GetAiObjectContext()->GetAction("fankriss rear stack");
    return action && action->isUseful();
}
void FankrissStackStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("fankriss rear needed",
        NextAction::array(0, new NextAction("fankriss rear stack", ACTION_HIGH + 8), NULL)));
    // The ordinary class AoE strategy owns resource, cooldown and splash/CC
    // checks. Keep it enabled with +aoe; the stack only owns position/target.
}
void FankrissStackStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{ multipliers.push_back(new FankrissRaidMultiplier(ai)); }
void FankrissHealingStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{ multipliers.push_back(new FankrissRaidMultiplier(ai)); }
