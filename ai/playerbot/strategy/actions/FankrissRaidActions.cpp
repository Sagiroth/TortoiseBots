#include "playerbot/playerbot.h"
#include "FankrissRaidActions.h"
#include "playerbot/FankrissGauntletPolicy.h"
#include "FankrissFocusActions.h"
#include "playerbot/EbonrocSwapPolicy.h"
#include "playerbot/strategy/generic/FankrissStrategy.h"
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
        if(FankrissSingleTargetActive(ai)&&(name=="dps aoe"||action->GetThreatType()==ActionThreatType::ACTION_THREAT_AOE))return 0.0f;
        // Rear-stack spare tanks must not turn the boss onto the raid. The
        // dedicated Mortal Wound swap is not an ordinary taunt action.
        if (EbonrocTauntAction(name))
            if (Unit* boss = GetFankrissStackBoss(ai))
                if (name == "challenging shout" || name == "challenging roar" || action->GetTarget() == boss ||
                    (name == "righteous defense" && action->GetTarget() == boss->GetVictim())) return 0.0f;
        if (!FankrissOrdinaryMovement(name) && name != "dps aoe" && name != "reach party member to heal" &&
            name != "charge" && name != "intercept" && name != "feral charge") return 1.0f;
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
bool ai::IgnoreFankrissGauntlet(Player* bot, Unit* target)
{
    if (!bot || !target || bot->GetMapId() != 531 || !bot->GetGroup() || !target->IsInMap(bot) ||
        (target->GetEntry() != 15229 && target->GetEntry() != 15300)) return false;
    PlayerbotAI* brain = PlayerbotAIStorage::Instance().GetAI(bot);
    return brain && FankrissDpsOptedIn(brain) && IsGroupBoss(bot, ActiveBoss(brain)) && !brain->IsTank(bot) &&
        GetFankrissRescueTarget(brain) != target;
}
bool ai::FankrissSpawnAtStack(PlayerbotAI* ai, Unit* spawn)
{
    if (!ai || !spawn || spawn->GetEntry() != 15630 || !spawn->IsInMap(ai->GetBot())) return false;
    Unit* boss = ActiveBoss(ai);
    if (IsGroupBoss(ai->GetBot(), boss) &&
        FankrissSpawnInRadius(spawn->GetPositionX(), spawn->GetPositionY(), boss->GetPositionX(), boss->GetPositionY()) &&
        std::abs(spawn->GetPositionZ() - boss->GetPositionZ()) <= 10.0f) return true;
    // The raid can be displaced from the boss (or still fighting his Spawns
    // after a reset). Accept inside its actual compact stack too. A lone
    // webbed/outlying player must never invite a chase into the tunnels.
    if (!spawn->IsAlive() || !spawn->IsInCombat() || !ai->GetBot()->GetGroup()) return false;
    unsigned living = 0, nearby = 0;
    for (Player* member : LiveGroupMembers(ai->GetBot()->GetGroup()))
    {
        if (!member->IsAlive() || !member->IsInMap(ai->GetBot()) ||
            member->HasAura(720) || member->HasAura(731) || member->HasAura(1121)) continue;
        ++living;
        if (FankrissSpawnInRadius(spawn->GetPositionX(), spawn->GetPositionY(), member->GetPositionX(), member->GetPositionY()) &&
            std::abs(spawn->GetPositionZ() - member->GetPositionZ()) <= 10.0f) ++nearby;
    }
    return nearby >= 3 && nearby * 2 > living;
}
bool FankrissStackAction::OwnsMovement() const
{
    return stackSpline && bot->movespline->GetId() == stackSpline && !bot->movespline->Finalized();
}
bool ai::FankrissStackMoving(PlayerbotAI* ai)
{
    auto* action = dynamic_cast<FankrissStackAction*>(ai->GetAiObjectContext()->GetAction("fankriss rear stack"));
    return action && action->OwnsMovement() && GetFankrissStackBoss(ai);
}
namespace
{
bool Webbed(Player* bot)
{
    return bot->HasAura(720) || bot->HasAura(731) || bot->HasAura(1121);
}
FankrissRearPoint StackGoal(PlayerbotAI* ai, Unit* boss)
{
    Player* bot = ai->GetBot();
    if (ai->IsTank(bot) && ai->HasStrategy("fankriss tank", BotState::BOT_STATE_COMBAT))
        return FankrissCenterTank(bot->GetCombatReach(boss, true, 0));
    auto goal = FankrissRaidSlot(boss->GetPositionX(), boss->GetPositionY(), boss->GetOrientation(),
        bot->GetCombatReach(boss, true, 0), bot->GetClass() == CLASS_HUNTER);
    Unit* target = bot->GetVictim();
    if (!ai->IsHeal(bot) && ai->IsMelee(bot) && FankrissSpawnAtStack(ai, target))
        goal = FankrissStackEdge(goal, target->GetPositionX(), target->GetPositionY(),
            boss->GetPositionX(), boss->GetPositionY(), boss->GetOrientation(), bot->GetCombatReach(target, true, 0));
    return goal;
}
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
    if (!FankrissHealingActive(ai) || FankrissStackMoving(ai)) return;
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
    if (!boss || !ai->CanMove() || Webbed(bot) || bot->IsNonMeleeSpellCasted(true, false, true)) return false;
    auto goal = StackGoal(ai, boss);
    if (std::hypot(bot->GetPositionX() - goal.x, bot->GetPositionY() - goal.y) <= 1.5f)
        return OwnsMovement();
    // Do not replace the same spline every AI tick or starve the class rotation.
    return bot->movespline->Finalized() || std::hypot(goal.x - lastX, goal.y - lastY) > 2.0f ||
        WorldTimer::getMSTimeDiff(lastMove, WorldTimer::getMSTime()) > 5000;
}
bool FankrissStackAction::Execute(Event& event)
{
    Unit* boss = GetFankrissStackBoss(ai);
    if (!boss || !ai->CanMove() || Webbed(bot) || bot->IsFalling() || bot->GetTransport() || bot->IsTaxiFlying() ||
        bot->IsNonMeleeSpellCasted(true, false, true)) return false;
    auto goal = StackGoal(ai, boss);
    if (std::hypot(bot->GetPositionX() - goal.x, bot->GetPositionY() - goal.y) <= 1.5f)
    {
        if (!OwnsMovement()) return false;
        ai->StopMoving();
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
    Movement::PointsArray points;
    for (auto const& point : path) points.push_back(G3D::Vector3(point.getX(), point.getY(), point.getZ()));
    Movement::MoveSplineInit init(*bot, "FankrissRearStack");
    init.MovebyPath(points);
    init.SetWalk(false);
    init.SetFacingGUID(boss->GetObjectGuid().GetRawValue());
    init.Launch();
    stackSpline = bot->movespline->GetId();
    sPlayerbotAIConfig.logEvent(ai, "FankrissStackMove", "checked center formation",
        "x=" + std::to_string(goal.x) + "|y=" + std::to_string(goal.y) +
        "|boss_center_distance=" + std::to_string(std::hypot(boss->GetPositionX() + 8074.88f, boss->GetPositionY() - 1193.64f)));
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
    triggers.push_back(new TriggerNode("fankriss control needed",NextAction::array(0,new NextAction("fankriss control spawn",ACTION_HIGH+10),NULL)));
    triggers.push_back(new TriggerNode("fankriss focus needed",NextAction::array(0,new NextAction("fankriss focus spawn",ACTION_HIGH+9),NULL)));
    triggers.push_back(new TriggerNode("fankriss rear needed",
        NextAction::array(0, new NextAction("fankriss rear stack", ACTION_HIGH + 8), NULL)));

}
void FankrissStackStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{ multipliers.push_back(new FankrissRaidMultiplier(ai)); }
void FankrissHealingStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{ multipliers.push_back(new FankrissRaidMultiplier(ai)); }

void FankrissStackStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{ FankrissSpawnStrategy(ai).InitReactionTriggers(triggers); }
void FankrissSpawnStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{ multipliers.push_back(new FankrissRaidMultiplier(ai)); }
