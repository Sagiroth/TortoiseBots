#include "playerbot/playerbot.h"
#include "OnyxiaTankPositionActions.h"
#include "ReachTargetActions.h"
#include "playerbot/strategy/generic/PullStrategy.h"
#include "Movement/spline/MoveSpline.h"
#include "playerbot/OnyxiaTankPositionPolicy.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/values/LastMovementValue.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "Movement/spline/MoveSplineInit.h"
#include <sstream>

using namespace ai;

Unit* ai::GetOnyxiaTankPositionTarget(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!bot || bot->GetMapId() != 249 || !bot->IsInWorld() || !bot->IsAlive() ||
        bot->IsBeingTeleported() || !ai->IsTank(bot) ||
        !ai->HasStrategy("onyxia landing tank", BotState::BOT_STATE_COMBAT))
        return nullptr;
    auto* pull=PullStrategy::Get(ai);
    Unit* ordered=pull && pull->IsCommandActive() && !pull->IsCommandPullback() ? pull->GetTarget():nullptr;
    if(ordered && ordered->GetEntry()==10184 && ordered->IsAlive() && ordered->IsInMap(bot) &&
        !ordered->IsFlying() && !ordered->HasAura(17131)) return ordered;
    if(!bot->IsInCombat()) return nullptr;
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
    if (active || !MovementAction::isUseful() || !GetOnyxiaTankPositionTarget(ai)) return false;
    OnyxiaTankPoint const here{bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()};
    return OnyxiaTankDistance(here, kOnyxiaTankSpot) > 1.5f || !bot->IsStopped() ||
        bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE;
}

bool OnyxiaTankPositionAction::Execute(Event& /*event*/)
{
    Unit* boss=GetOnyxiaTankPositionTarget(ai);
    if (active || !boss || !ai->CanMove() || bot->GetTransport() || bot->IsTaxiFlying())
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
    auto fail = [&](char const* reason = "unsafe or incomplete route")
    {
        if (!blockedReported)
        {
            std::ostringstream detail;
            detail << "map=249|start=" << here.x << '/' << here.y << '/' << here.z
                << "|corridor=x[-70,35],y[-232,-198],z[-94,-80]";
            sPlayerbotAIConfig.logEvent(ai, "OnyxiaTankPositionBlocked", reason, detail.str());
            ai->TellError(ai->GetMaster(), std::string("Onyxia tank spot: ") + reason +
                ". Stage the tank in the center lane (map 249, x -70 to 35, y -232 to -198, floor z -94 to -80), away from both whelp caves, then retry the pull.");
            blockedReported = true;
        }
        SetDuration(1000);
        return false;
    };
    crossing=!crossed && here.x<boss->GetPositionX()+3.f && boss->GetPositionX()+4.f<kOnyxiaTankSpot.x;
    OnyxiaTankPoint goal=crossing ? OnyxiaTankPoint{boss->GetPositionX()+4.f,boss->GetPositionY(),boss->GetPositionZ()}:kOnyxiaTankSpot;
    WorldPosition destination(bot->GetMapId(), goal.x,goal.y,goal.z);
    destination.setZ(destination.GetHeight());
    OnyxiaTankPoint const end{destination.getX(), destination.getY(), destination.getZ()};
    if (!OnyxiaTankCorridor(here)) return fail("outside validated tank corridor");
    if (!OnyxiaTankCorridor(end) || OnyxiaTankDistance(end, goal) > 1.5f)
        return fail("crossing or tank destination outside validated floor");
    std::vector<WorldPosition> const path = start.GetPathTo(destination, bot);
    std::vector<OnyxiaTankPoint> route;
    for (auto const& point : path) route.push_back({point.getX(), point.getY(), point.getZ()});
    if (!OnyxiaTankRouteSafe(here, route, end) || !destination.isPathTo(path, 1.0f, 1.0f)) return fail("incomplete path or path leaves tank corridor");
    auto hazards = AI_VALUE(std::list<HazardPosition>, "hazards");
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "possible targets no los"))
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->IsAlive() && unit->IsInMap(bot) && unit!=boss && !unit->IsInCombat())
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
            if (!IsMovingAllowed(bot->GetMapId(), sample.getX(), sample.getY(), sample.getZ())) return fail("movement forbidden on tank route");
            for (auto const& hazard : hazards)
                if (sample.distance(hazard.first) <= hazard.second + 2.0f) return fail("hazard or unengaged enemy on tank route");
        }
        previous = point;
    }
    ai->StopMoving();
    openingPath=path;waypoint=1;active=true;awaiting=false;lastUpdate=WorldTimer::getMSTime();
    AI_VALUE(LastMovement&, "last movement").lastPath.clear();
    ClearIdleState();
    blockedReported = false;
    SetDuration(100);
    return true;
}

void ai::UpdateOnyxiaTankOpening(PlayerbotAI* ai)
{
    if(ai->GetBot()->GetMapId()!=249) return;
    auto* action=dynamic_cast<OnyxiaTankPositionAction*>(ai->GetAiObjectContext()->GetAction("onyxia tank position"));
    if(action) action->Update();
}

void OnyxiaTankPositionAction::Update()
{
    Unit* boss=GetOnyxiaTankPositionTarget(ai);
    if(!boss || !ai->CanMove() || bot->GetTransport() || bot->IsTaxiFlying() || bot->IsFalling())
    {
        if(active && bot->movespline->Finalized()) ai->StopMoving();
        active=false;awaiting=false;crossed=false;return;
    }
    uint32 now=WorldTimer::getMSTime();
    if(uint32(now-lastUpdate)<100) return;
    uint32 elapsed=now-lastUpdate;lastUpdate=now;
    SET_AI_VALUE(Unit*,"current target",boss);bot->SetSelectionGuid(boss->GetObjectGuid());
    bot->Attack(boss,true);
    if(bot->GetClass()==CLASS_WARRIOR)
    {
        if(!ai->HasAura("defensive stance",bot) && ai->CanCastSpell("defensive stance",bot))
            ai->CastSpell("defensive stance",bot);
        else if(bot->CanReachWithMeleeAutoAttack(boss))
        {
            // Heroic Strike spends the next white swing's rage generation.
            // Reserve it for surplus rage while instant threat is established.
            for(char const* spell:{"shield slam","revenge","sunder armor"})
                if(ai->CanCastSpell(spell,boss) && ai->CastSpell(spell,boss)) break;
            if(bot->GetPower(POWER_RAGE)>=600 && ai->CanCastSpell("heroic strike",boss)) ai->CastSpell("heroic strike",boss);
        }
    }
    if(!active) return;
    if(!bot->movespline->Finalized()) {active=false;awaiting=false;return;}
    WorldPosition here(bot);
    if(awaiting)
    {
        if(here.distance(expected)>.15f)
        {
            if(uint32(now-sentAt)>1000)
            {ai->StopMoving();active=false;awaiting=false;sPlayerbotAIConfig.logEvent(ai,"OnyxiaTankPositionBlocked","movement packet not accepted","retry from current position");}
            return;
        }
        awaiting=false;
    }
    while(waypoint<openingPath.size() && here.distance(openingPath[waypoint])<.1f) ++waypoint;
    if(waypoint==openingPath.size())
    {ai->StopMoving();active=false;crossed=true;return;}
    auto const& next=openingPath[waypoint];
    auto step=OnyxiaOpeningStep({here.getX(),here.getY(),here.getZ()},{next.getX(),next.getY(),next.getZ()},
        bot->GetSpeed(crossing ? MOVE_RUN:MOVE_RUN_BACK),elapsed);
    WorldPosition point(249,step.x,step.y,step.z);
    if(!IsMovingAllowed(249,step.x,step.y,step.z) || IsHazardNearPosition(point))
    {ai->StopMoving();active=false;return;}
    float facing=crossing ? std::atan2(next.getY()-here.getY(),next.getX()-here.getX()):
        std::atan2(boss->GetPositionY()-step.y,boss->GetPositionX()-step.x);
    if(facing<0) facing+=2*M_PI_F;
    MovementInfo movement=bot->m_movementInfo;
    movement.RemoveMovementFlag(MOVEFLAG_MASK_MOVING | MOVEFLAG_SPLINE_ENABLED);
    movement.AddMovementFlag(crossing ? MOVEFLAG_FORWARD:MOVEFLAG_BACKWARD);
    movement.ChangePosition(step.x,step.y,step.z,facing);
    // MovementInfo::Write serializes stime into the incoming ctime slot.
    // Zero disables core extrapolation beyond our validated, paced step.
    movement.stime=0;movement.ctime=0;
    // QueuePacket's copy constructor drops receive time; movement packets
    // require a fresh timestamp to pass the core's move-reject gate.
    auto* packet=new WorldPacket(MSG_MOVE_HEARTBEAT);
    *packet<<movement;packet->FillPacketTime(now);bot->GetSession()->QueuePacket(packet);
    expected=point;sentAt=now;awaiting=true;
}

float OnyxiaTankPositionMultiplier::GetValue(Action* action)
{
    if (!action || !GetOnyxiaTankPositionTarget(ai)) return 1.0f;
    std::string const& name = action->getName();
    if (name == "onyxia tank position" || name == "set facing" || name == "flee" ||
        name == "move away from hazard" || name == "onyxia deep breath escape") return 1.0f;
    return dynamic_cast<MovementAction*>(action) || dynamic_cast<CastReachTargetSpellAction*>(action) ? 0.0f : 1.0f;
}
