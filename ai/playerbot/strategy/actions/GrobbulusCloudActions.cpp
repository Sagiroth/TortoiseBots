#include "playerbot/playerbot.h"
#include "NaxxramasDungeonActions.h"
#include "playerbot/policy/raid/GrobbulusCloudPolicy.h"
#include "playerbot/strategy/values/GrobbulusCloudHazards.h"
#include "playerbot/strategy/values/PositionValue.h"

using namespace ai;

std::list<HazardPosition> ai::CalculateGrobbulusCloudHazards(PlayerbotAI* ai)
{
    Player* bot=ai->GetBot();
    std::list<HazardPosition> hazards;
    if (bot->GetMapId()!=533 || !bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported() ||
        !ai->HasStrategy("grobbulus clouds",BotState::BOT_STATE_COMBAT)) return hazards;
    AiObjectContext* context=ai->GetAiObjectContext();
    // Clouds are untargetable: the normal attack-target filter removes them.
    // Both boss and Injection clouds use SUMMON_WILD and retain template
    // faction14. Reuse the cached unvalidated nearby hostile list.
    auto candidates=AI_VALUE2(std::list<ObjectGuid>,"all targets","60:1");
    for (ObjectGuid const& guid:candidates)
    {
        Unit* cloud=ai->GetUnit(guid);
        if (!cloud || cloud->GetEntry()!=16363 || !cloud->IsAlive() || !cloud->IsInMap(bot)) continue;
        Aura* aura=ai->GetAura(28158,cloud);
        float radius=GrobbulusCloudRadius(aura ? aura->GetAuraMaxDuration() : 0,
            aura ? aura->GetAuraDuration() : 0);
        hazards.emplace_back(WorldPosition(cloud),radius);
    }
    return hazards;
}

bool ai::GrobbulusCloudMovementAllowed(PlayerbotAI* ai)
{
    if (ai->GetBot()->GetMapId()!=533 || !ai->HasStrategy("grobbulus clouds",BotState::BOT_STATE_COMBAT)) return false;
    AiObjectContext* context=ai->GetAiObjectContext();
    PositionMap& positions=AI_VALUE(PositionMap&,"position");
    auto hold=positions.find("pull hold");
    return !ai->HasStrategy("stay",BotState::BOT_STATE_COMBAT) &&
        !ai->HasStrategy("follow",BotState::BOT_STATE_COMBAT) &&
        !ai->HasStrategy("wait for attack",BotState::BOT_STATE_COMBAT) &&
        !ai->HasStrategy("grind",BotState::BOT_STATE_COMBAT) &&
        (hold==positions.end() || !hold->second.isSet());
}

bool ai::InsideGrobbulusCloud(PlayerbotAI* ai)
{
    if (!GrobbulusCloudMovementAllowed(ai)) return false;
    Player* bot=ai->GetBot();
    for (auto const& hazard:GetGrobbulusCloudHazards(ai))
        if (WorldPosition(bot).distance(hazard.first)<=hazard.second) return true;
    return false;
}

bool GrobbulusCloudEscapeAction::isUseful()
{
    return MovementAction::isUseful() && InsideGrobbulusCloud(ai);
}

bool GrobbulusCloudEscapeAction::Execute(Event& event)
{
    if (!InsideGrobbulusCloud(ai)) return false;
    WorldPosition const start(bot);
    ai->StopMoving();
    auto const hazards=AI_VALUE(std::list<HazardPosition>,"hazards");
    std::vector<GrobbulusCloudCircle> circles;
    for (auto const& hazard:hazards)
        if (fabs(hazard.first.getZ()-start.getZ())<=5)
            circles.push_back({hazard.first.getX(),hazard.first.getY(),hazard.second});
    unsigned attempts=0;
    for (float radius:{6.0f,12.0f,18.0f,26.0f,34.0f,42.0f})
        for (unsigned heading=0;heading<16;++heading)
        {
            float angle=2*M_PI_F*heading/16;
            WorldPosition point=start+WorldPosition(0,radius*cos(angle),radius*sin(angle),0);
            if (GrobbulusCloudContains(point.getX(),point.getY(),circles)) continue;
            if (++attempts>24) return false;
            point.setZ(point.GetHeight());
            if (!std::isfinite(point.getZ()) || fabs(point.getZ()-start.getZ())>5 || IsHazardNearPosition(point) ||
                !bot->IsWithinLOS(point.getX(),point.getY(),point.getZ()+bot->GetCollisionHeight())) continue;
            auto const path=start.GetPathTo(point,bot);
            if (!point.isPathTo(path)) continue;
            bool safe=true;
            float length=0;
            WorldPosition previous=start;
            for (auto const& next:path)
            {
                float distance=previous.distance(next);
                length+=distance;
                if (!std::isfinite(distance) || length>55) { safe=false; break; }
                unsigned steps=std::max(1u,unsigned(std::ceil(distance)));
                for (unsigned i=1;i<=steps;++i)
                {
                    float t=float(i)/steps;
                    float x=previous.getX()+(next.getX()-previous.getX())*t;
                    float y=previous.getY()+(next.getY()-previous.getY())*t;
                    float z=previous.getZ()+(next.getZ()-previous.getZ())*t;
                    if (!std::isfinite(z) || fabs(z-start.getZ())>5 ||
                        !GrobbulusEscapeSample(start.getX(),start.getY(),x,y,circles)) safe=false;
                }
                previous=next;
            }
            if (!safe) continue;
            bool unpulled=false;
            for (ObjectGuid const& guid:AI_VALUE(std::list<ObjectGuid>,"possible targets"))
            {
                Unit* unit=ai->GetUnit(guid);
                if (unit && unit->IsAlive() && !unit->IsInCombat() &&
                    sServerFacade.getDistance2d(unit,point.getX(),point.getY())<sPlayerbotAIConfig.aggroDistance)
                    unpulled=true;
            }
            if (!unpulled && MoveTo(533,point.getX(),point.getY(),point.getZ(),false,IsReaction(),false,true))
            {
                if (IsReaction()) WaitForReach(length);
                return true;
            }
        }
    return false;
}

std::list<HazardPosition> ai::GetGrobbulusCloudHazards(PlayerbotAI* ai)
{
    Player* bot=ai->GetBot();
    if (bot->GetMapId()!=533 || !bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported() ||
        !ai->HasStrategy("grobbulus clouds",BotState::BOT_STATE_COMBAT)) return {};
    AiObjectContext* context=ai->GetAiObjectContext();
    return AI_VALUE(std::list<HazardPosition>,"grobbulus cloud hazards");
}
