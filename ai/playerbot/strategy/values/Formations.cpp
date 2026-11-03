
#include "playerbot/GroupMembers.h"
#include "playerbot/playerbot.h"
#include "Formations.h"

#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/FollowSpotSafetyPolicy.h"
#include "playerbot/RaidFollowWedgePolicy.h"
#include "Arrow.h"
#include "../../../../runtime/BotManager.h"

using namespace ai;

WorldLocation Formation::NullLocation = WorldLocation();
unsigned int Formation::lastSerial = 0;

// Follow-spot ground safety (issue #217): a missed floor lookup must not drag
// the bot into the subterranean terrain below a crypt. A steep stair or slope
// moves the follow spot a couple of yards above the target; a missed crypt
// floor gives a lot more. The numbers and the drop decision are shared by every
// formation (FollowSpotSafetyPolicy.h).
static constexpr float kMaxGroundDrop = ai::kFollowSpotMaxGroundDropYd;
static constexpr float kStairSearchHeadroom = ai::kFollowSpotStairHeadroomYd;

// The raid/dungeon wedge (AiPlayerbot.RaidFollowWedge) is on for a group inside
// an instance. Outside it the configured formation and the open-world follow
// stay exactly as before.
bool ai::UsesRaidFollowWedge(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!bot || !sPlayerbotAIConfig.raidFollowWedge)
        return false;
    if (!bot->GetGroup())
        return false;
    Map* map = bot->GetMap();
    return map && (map->IsDungeon() || map->IsRaid());
}

// One ground/LOS guard for every formation. The spot's floor is searched from
// above the center's elevation (crypt stairs), a floor far below the center is
// a ledge and refuses the spot, and the result is kept within the same window
// and clear of terrain. NearFormation carried these; chaos, circle, line,
// shield, arrow, custom and the plain follow formations now share them.
bool ai::FinalizeFollowSpot(Player* bot, Map* map, float fromX, float fromY, float fromZ,
    float x, float y, float& z, bool refuseVoid)
{
    if (!map || !bot)
        return false;

    float const ground = map->GetHeight(x, y, fromZ + kStairSearchHeadroom);
    // A spot with no floor under it hangs over a void: the formations that always refused one keep refusing it.
    if (refuseVoid && ground <= INVALID_HEIGHT)
        return false;
    if (ai::FollowSpotGroundRejected(fromZ, ground))
        return false;
    if (ground > INVALID_HEIGHT && fabs(ground - fromZ) <= kMaxGroundDrop)
        z = ground;

    // Prevent going into terrain.
    map->GetLosHitPosition(fromX, fromY, fromZ + bot->GetCollisionHeight(), x, y, z, -0.5f);

    if (!bot->IsFlying() && !bot->IsSwimming())
    {
        z += CONTACT_DISTANCE;
        bot->UpdateAllowedPositionZ(x, y, z);

        // UpdateAllowedPositionZ clamps with the same downward raycast and can
        // drag the spot into the subterranean fallback. The window must be
        // symmetric around the center: walking DOWN stairs the follow spot sits
        // a couple of yards ABOVE the leader, walking UP stairs a couple of
        // yards BELOW it (issue #217).
        float const maxZ = fromZ + kMaxGroundDrop;
        float const minZ = fromZ - kMaxGroundDrop;
        if (z > maxZ || z < minZ)
            z = std::clamp(z, minZ, maxZ);
    }

    return true;
}

bool IsSameLocation(WorldLocation const &a, WorldLocation const &b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z && a.mapId == b.mapId;
}

float Formation::GetMaxDistance()
{
    return ai->GetRange("follow");
}

bool Formation::IsNullLocation(WorldLocation const& loc)
{
	return IsSameLocation(loc, Formation::NullLocation);
}

float Formation::GetAngle()
{
    WorldLocation loc = GetLocation();
    if (Formation::IsNullLocation(loc) || loc.mapId == -1)
        return 0.0f;

    Unit* followTarget = AI_VALUE(Unit*, "follow target");

    float angle = WorldPosition(followTarget).GetAngleTo(loc) - followTarget->getOrientation();
    if (angle < 0) angle += 2 * M_PI_F;

    return angle;
}

float Formation::GetOffset()
{
    WorldLocation loc = GetLocation();
    if (Formation::IsNullLocation(loc) || loc.mapId == -1)
        return 0.0f;

    Unit* followTarget = AI_VALUE(Unit*, "follow target");

    float distance = sqrt(WorldPosition(followTarget).sqDistance2d(loc));

    return distance;
}

WorldLocation FollowFormation::GetLocation()
{
    float range = ai->GetRange("follow");

    Unit* followTarget = AI_VALUE(Unit*, "follow target");
    if (!followTarget)
        return Formation::NullLocation;

    float angle = GetFollowAngle();
    float x = followTarget->getPositionX() + cos(angle) * range;
    float y = followTarget->getPositionY() + sin(angle) * range;
    float z = followTarget->getPositionZ();

    if (UsesRaidFollowWedge(ai) && !FinalizeFollowSpot(bot, followTarget->GetMap(),
            followTarget->getPositionX(), followTarget->getPositionY(), followTarget->getPositionZ(), x, y, z))
        return Formation::NullLocation;

    return WorldLocation(bot->GetMapId(), x, y, z);
}

WorldLocation MoveAheadFormation::GetLocation()
{
    Unit* followTarget = AI_VALUE(Unit*, "follow target");
    if (!followTarget)
        return WorldLocation();

    WorldLocation loc = GetLocationInternal();

    return loc;

    /*
    if (Formation::IsNullLocation(loc))
        return loc;

    float x = loc.x;
    float y = loc.y;
    float z = loc.z;

    if (sServerFacade.isMoving(master)) {
        float ori = master->getOrientation();

        float aheadDistance = sPlayerbotAIConfig.tooCloseDistance;//std::min(std::max(sPlayerbotAIConfig.tooCloseDistance,sqrt(WorldPosition(0, x, y, z).sqDistance2d(bot))), sPlayerbotAIConfig.sightDistance);

        float x1 = x + aheadDistance * cos(ori);
        float y1 = y + aheadDistance * sin(ori);
        float ground = master->GetMap()->GetHeight(x1, y1, z);
        if (ground > INVALID_HEIGHT)
        {
            x = x1;
            y = y1;
        }
    }
    float ground = master->GetMap()->GetHeight(x, y, z);
    if (ground <= INVALID_HEIGHT)
        return Formation::NullLocation;

    //z += CONTACT_DISTANCE;
    //bot->UpdateAllowedPositionZ(x, y, z);
    return WorldLocation(master->GetMapId(), x, y, z);
    */
}

namespace ai
{
    class MeleeFormation : public FollowFormation
    {
    public:
        MeleeFormation(PlayerbotAI* ai) : FollowFormation(ai, "melee") {}
        virtual std::string GetTargetName() override { return "master target"; }
        virtual float GetAngle() override { return GetFollowAngle(); }
        virtual float GetOffset() override { return ai->GetRange("follow"); }
    };

    class QueueFormation : public FollowFormation
    {
    public:
        QueueFormation(PlayerbotAI* ai) : FollowFormation(ai, "queue") {}
        virtual std::string GetTargetName() override { return "line target"; }
        virtual float GetAngle() override { return M_PI_F; }
        virtual float GetOffset() override { return ai->GetRange("follow"); }
    };

    class NearFormation : public MoveAheadFormation
    {
    public:
        NearFormation(PlayerbotAI* ai) : MoveAheadFormation(ai, "near") {}
        virtual WorldLocation GetLocationInternal() override
        {
            Unit* followTarget = AI_VALUE(Unit*, "follow target");
            if (!ai->IsSafe(followTarget))
                return WorldLocation();

            float range = ai->GetRange("follow") + followTarget->GetObjectBoundingRadius();
            if (UsesRaidFollowWedge(ai))
            {
                int index = 1, total = 1;
                GetFollowSlot(index, total);
                int const followers = total > 0 ? total - 1 : 0;
                range = sPlayerbotAIConfig.raidFollowDistance +
                    RaidFollowWedgeRingOffset(followers, ai->IsTank(bot), ai->IsHeal(bot)) +
                    followTarget->GetObjectBoundingRadius();
            }
            float angle = GetFollowAngle();
            float x = followTarget->getPositionX() + cos(angle) * range;
            float y = followTarget->getPositionY() + sin(angle) * range;
            float z = followTarget->getPositionZ();

            if (!FinalizeFollowSpot(bot, followTarget->GetMap(), followTarget->getPositionX(),
                    followTarget->getPositionY(), followTarget->getPositionZ(), x, y, z))
                return WorldLocation();

            return WorldLocation(followTarget->GetMapId(), x, y, z);
        }

        virtual float GetMaxDistance() override
        {
            if (UsesRaidFollowWedge(ai))
            {
                int index = 1, total = 1;
                GetFollowSlot(index, total);
                int const followers = total > 0 ? total - 1 : 0;
                return sPlayerbotAIConfig.raidFollowDistance +
                    RaidFollowWedgeRingOffset(followers, ai->IsTank(bot), ai->IsHeal(bot));
            }
            return ai->GetRange("follow");
        }
    };


    class ChaosFormation : public MoveAheadFormation
    {
    public:
        ChaosFormation(PlayerbotAI* ai) : MoveAheadFormation(ai, "chaos"), lastChangeTime(0) {}
        virtual WorldLocation GetLocationInternal() override
        {
            Unit* followTarget = AI_VALUE(Unit*, "follow target");
            if (!ai->IsSafe(followTarget))
                return WorldLocation();

            float range = ai->GetRange("follow");
			float angle = GetFollowAngle();

            time_t now = time(0);
            if (!lastChangeTime || now - lastChangeTime >= 3)
            {
                lastChangeTime = now;
                dx = (urand(0, 10) / 10.0 - 0.5) * sPlayerbotAIConfig.tooCloseDistance;
                dy = (urand(0, 10) / 10.0 - 0.5) * sPlayerbotAIConfig.tooCloseDistance;
                dr = sqrt(dx*dx + dy*dy);
            }

            float x = followTarget->getPositionX() + cos(angle) * range + dx;
            float y = followTarget->getPositionY() + sin(angle) * range + dy;
            float z = followTarget->getPositionZ();

            if (UsesRaidFollowWedge(ai))
            {
                if (!FinalizeFollowSpot(bot, followTarget->GetMap(), followTarget->getPositionX(),
                        followTarget->getPositionY(), followTarget->getPositionZ(), x, y, z))
                    return WorldLocation();
            }
            else if (!bot->IsFlying())
            {
                z += CONTACT_DISTANCE;
                bot->UpdateAllowedPositionZ(x, y, z);
            }

            return WorldLocation(followTarget->GetMapId(), x, y, z);
        }

        virtual float GetMaxDistance() override { return ai->GetRange("follow") + dr; }

    private:
        time_t lastChangeTime;
        float dx = 0, dy = 0, dr = 0;
    };

    class CircleFormation : public MoveFormation
    {
    public:
        CircleFormation(PlayerbotAI* ai) : MoveFormation(ai, "circle") {}
        virtual WorldLocation GetLocation() override
        {
            float range = ai->GetRange("follow");

            Unit* target = AI_VALUE(Unit*, "current target");
            Unit* followTarget = AI_VALUE(Unit*, "follow target");
            if (!target && target != bot)
                target = followTarget;

            if (!target || !ai->IsSafe(target))
				return Formation::NullLocation;

            float angle = GetFollowAngle();
            float x = target->getPositionX() + cos(angle) * range;
            float y = target->getPositionY() + sin(angle) * range;
            float z = target->getPositionZ();

            if (!FinalizeFollowSpot(bot, target->GetMap(), target->getPositionX(),
                    target->getPositionY(), target->getPositionZ(), x, y, z, true))
                return Formation::NullLocation;

            return WorldLocation(bot->GetMapId(), x, y, z);
        }
    };

    class LineFormation : public MoveAheadFormation
    {
    public:
        LineFormation(PlayerbotAI* ai) : MoveAheadFormation(ai, "line") {}
        virtual WorldLocation GetLocationInternal() override
        {
            Group* group = bot->GetGroup();
            if (!group)
                return Formation::NullLocation;

            float range = ai->GetRange("follow");

            Player* followTarget = (Player*)AI_VALUE(Unit*, "follow target");
            if (!followTarget)
                return Formation::NullLocation;

            float x = followTarget->getPositionX();
            float y = followTarget->getPositionY();
            float z = followTarget->getPositionZ();
            float orientation = followTarget->getOrientation();

            std::vector<Player*> players;
            for (Player* member : LiveGroupMembers(group))
            {
                if (!ai->IsSafe(member)) continue;
                if (member != followTarget)
                    players.push_back(member);
            }

            players.insert(players.begin() + players.size() / 2, followTarget);

            return MoveLine(players, 0.0f, x, y, z, orientation, range);
        }
    };

    class ShieldFormation : public MoveFormation
    {
    public:
        ShieldFormation(PlayerbotAI* ai) : MoveFormation(ai, "shield") {}
        virtual WorldLocation GetLocation() override
        {
            Group* group = bot->GetGroup();
            if (!group)
                return Formation::NullLocation;

            float range = ai->GetRange("follow");

            Player* followTarget = (Player*)AI_VALUE(Unit*, "follow target");
            if (!followTarget)
                return Formation::NullLocation;

            float x = followTarget->getPositionX();
            float y = followTarget->getPositionY();
            float z = followTarget->getPositionZ();
            float orientation = followTarget->getOrientation();

            std::vector<Player*> tanks;
            std::vector<Player*> dps;
            for (Player* member : LiveGroupMembers(group))
            {
                if (!ai->IsSafe(member)) continue;
                if (member != followTarget)
                {
                    if (ai->IsTank(member))
                        tanks.push_back(member);
                    else
                        dps.push_back(member);
                }
            }

            if (ai->IsTank(followTarget))
                tanks.insert(tanks.begin() + (tanks.size() + 1) / 2, followTarget);
            else
                dps.insert(dps.begin() + (dps.size() + 1) / 2, followTarget);

            if (ai->IsTank(bot) && ai->IsTank(followTarget))
            {
                return MoveLine(tanks, 0.0f, x, y, z, orientation, range);
            }
            if (!ai->IsTank(bot) && !ai->IsTank(followTarget))
            {
                return MoveLine(dps, 0.0f, x, y, z, orientation, range);
            }
            if (ai->IsTank(bot) && !ai->IsTank(followTarget))
            {
                float diff = tanks.size() % 2 == 0 ? -sPlayerbotAIConfig.tooCloseDistance / 2.0f : 0.0f;
                return MoveLine(tanks, diff, x + cos(orientation) * range, y + sin(orientation) * range, z, orientation, range);
            }
            if (!ai->IsTank(bot) && ai->IsTank(followTarget))
            {
                float diff = dps.size() % 2 == 0 ? -sPlayerbotAIConfig.tooCloseDistance / 2.0f : 0.0f;
                return MoveLine(dps, diff, x - cos(orientation) * range, y - sin(orientation) * range, z, orientation, range);
            }
            return Formation::NullLocation;
        }
    };

    class FarFormation : public FollowFormation
    {
    public:
        FarFormation(PlayerbotAI* ai) : FollowFormation(ai, "far") {}
        virtual std::string GetTargetName() override { return "master target"; }
        virtual float GetAngle() override
        {
            Unit* followTarget = AI_VALUE(Unit*, "follow target");

            float currentAngle = WorldPosition(followTarget).GetAngleTo(bot) - followTarget->getOrientation();
            float followAngle = currentAngle;

            if(bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
                followAngle = sServerFacade.GetChaseAngle(bot);

            float delta = (currentAngle - followAngle);

            if (delta > M_PI_F)
                delta -= M_PI_F * 2.0f;

            if (fabs(delta) > 0.2)
                return followAngle + delta;

            if (followAngle < 0)
                followAngle += M_PI_F * 2.0f;

            delta = (M_PI_F - followAngle) * 0.5;

            if (fabs(delta) < 0.01)
                delta = 0;

            return followAngle + delta;
        }
        virtual float GetOffset() override { return ai->GetRange("follow"); }
    };

    class CustomFormation : public MoveAheadFormation
    {
    public:
        CustomFormation(PlayerbotAI* ai) : MoveAheadFormation(ai, "custom")
        {
            PositionMap& posMap = AI_VALUE(PositionMap&, "position");
            PositionEntry followPosition = posMap["follow"];

            if (followPosition.isSet())
                return;

            Unit* followTarget = AI_VALUE(Unit*, "follow target");

            if (!ai->IsSafe(followTarget) || sServerFacade.getDistance2d(bot, followTarget) > sPlayerbotAIConfig.reactDistance)
            {
                WorldPosition pos(bot->GetMapId(), cos(GetFollowAngle()) * ai->GetRange("follow"), sin(GetFollowAngle()) * ai->GetRange("follow"), 0);
                followPosition.Set(pos);
            }
            else
            {
                WorldPosition relPos(bot);
                relPos -= WorldPosition(ai->GetMaster());
                relPos.rotateXY(-1 * ai->GetMaster()->getOrientation());

                followPosition.Set(relPos.getX(), relPos.getY(), relPos.getZ(), relPos.GetMapId());
            }

            posMap["follow"] = followPosition;
        }

        virtual WorldLocation GetLocationInternal() override
        {
            Unit* followTarget = AI_VALUE(Unit*, "follow target");

            if (!ai->IsSafe(followTarget))
                return Formation::NullLocation;

            PositionMap& posMap = AI_VALUE(PositionMap&, "position");
            PositionEntry followPosition = posMap["follow"];

            if (!followPosition.isSet())
                return Formation::NullLocation;

            WorldPosition relPos(followPosition.mapId, followPosition.x, followPosition.y, followPosition.z);

            relPos.rotateXY(followTarget->getOrientation());

            WorldPosition spot = WorldPosition(followTarget) + relPos;
            if (!UsesRaidFollowWedge(ai))
                return spot;
            float z = spot.getZ();
            if (!FinalizeFollowSpot(bot, followTarget->GetMap(), followTarget->getPositionX(),
                    followTarget->getPositionY(), followTarget->getPositionZ(),
                    spot.getX(), spot.getY(), z))
                return Formation::NullLocation;

            return WorldPosition(spot.GetMapId(), spot.getX(), spot.getY(), z);
        }
    };
};

void Formation::GetFollowSlot(int& index, int& total)
{
    Player* followTarget = (Player*)AI_VALUE(Unit*, "follow target");

    Group* group = bot->GetGroup();
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    index = 1;
    total = 1;

    if (!group && followTarget && !PlayerbotAIStorage::Instance().GetAI(followTarget))
    {
        for (Player* player : TortoiseBots::BotManager::Instance().GetBotsForMaster(followTarget->GetObjectGuid()))
        {
            if (player == bot) index = total;
            total++;
        }
    }
    else if (group)
    {
        std::vector<Player*> roster;
        for (Player* member : LiveGroupMembers(group))
        {
            if (!ai->IsSafe(member) || !sServerFacade.IsAlive(member)) continue;
            if (member && member != followTarget && !ai->IsTank(member) && !ai->IsHeal(member))
            {
                roster.insert(roster.begin() + roster.size() / 2, member);
            }
        }
        for (Player* member : LiveGroupMembers(group))
        {
            if (!ai->IsSafe(member) || !sServerFacade.IsAlive(member)) continue;
            if (member && member != followTarget && ai->IsHeal(member))
            {
                roster.insert(roster.begin() + roster.size() / 2, member);
            }
        }
        bool left = true;
        for (Player* member : LiveGroupMembers(group))
        {
            if (!ai->IsSafe(member) || !sServerFacade.IsAlive(member)) continue;
            if (member && member != followTarget && ai->IsTank(member))
            {
                if (left) roster.push_back(member); else roster.insert(roster.begin(), member);
                left = !left;
            }
        }

        for (std::vector<Player*>::iterator i = roster.begin(); i != roster.end(); ++i)
        {
            if (*i == bot) break;
            index++;
        }
        total = roster.size() + 1;
    }

}

float Formation::GetFollowAngle()
{
    Player* followTarget = (Player*)AI_VALUE(Unit*, "follow target");

    int index = 1, total = 1;
    GetFollowSlot(index, total);

    float start = (followTarget ? followTarget->getOrientation() : 0.0f);
    if (UsesRaidFollowWedge(ai) && GetName() == "near" && followTarget && total > 1)
        return RaidFollowWedgeAngle(start, index - 1, total - 1);

    return start + (0.125f + 1.75f * index / total + (total == 2 ? 0.125f : 0.0f)) * M_PI;
}

FormationValue::FormationValue(PlayerbotAI* ai) : ManualSetValue<Formation*>(ai, nullptr, "formation")
{
    if (!Load(sPlayerbotAIConfig.defaultFormation))
    {
        Load("near");
    }
}

// In a dungeon or raid group with the wedge on, the configured formation gives
// way to the near formation behind the leader. An explicit player choice
// (LoadUserChoice) is kept, and the open world keeps the configured formation.
Formation* FormationValue::Get()
{
    if (!userChosen && UsesRaidFollowWedge(ai))
    {
        if (!raidValue)
            raidValue = new NearFormation(ai);
        return raidValue;
    }
    return value;
}

void FormationValue::Reset()
{
    if (value) delete value;
    value = nullptr;
    if (raidValue) delete raidValue;
    raidValue = nullptr;
    userChosen = false;
    if (!Load(sPlayerbotAIConfig.defaultFormation))
    {
        Load("near");
    }
}

bool FormationValue::LoadUserChoice(std::string formation)
{
    bool const loaded = Load(formation);
    if (loaded)
        userChosen = true;
    return loaded;
}

std::string FormationValue::Save()
{
    return value ? value->GetName() : "?";
}

bool FormationValue::Load(std::string formation)
{
    if (formation == "melee")
    {
        if (value) delete value;
        value = new MeleeFormation(ai);
    }
    else if (formation == "queue")
    {
        if (value) delete value;
        value = new QueueFormation(ai);
    }
    else if (formation == "chaos")
    {
        if (value) delete value;
        value = new ChaosFormation(ai);
    }
    else if (formation == "circle")
    {
        if (value) delete value;
        value = new CircleFormation(ai);
    }
    else if (formation == "line")
    {
        if (value) delete value;
        value = new LineFormation(ai);
    }
    else if (formation == "shield")
    {
        if (value) delete value;
        value = new ShieldFormation(ai);
    }
    else if (formation == "arrow")
    {
        if (value) delete value;
        value = new ArrowFormation(ai);
    }
    else if (formation == "near" || formation == "default")
    {
        if (value) delete value;
        value = new NearFormation(ai);
    }
    else if (formation == "far")
    {
        if (value) delete value;
        value = new FarFormation(ai);
    }
    else if (formation == "custom")
    {
        if (value) delete value;
        value = new CustomFormation(ai);
    }
    else return false;

    return true;
}

bool SetFormationAction::Execute(Event& event)
{
    std::string formation = event.GetParam();
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();

    FormationValue* value = (FormationValue*)context->GetValue<Formation*>("formation");
    if (formation == "?" || formation.empty())
    {
        std::ostringstream str; str << "Formation: |cff00ff00" << value->Get()->GetName();
        ai->TellPlayer(requester, str);
        return true;
    }

    if (formation == "show")
    {
        WorldLocation loc = value->Get()->GetLocation();
        if (!Formation::IsNullLocation(loc))
            ai->Ping(loc.x, loc.y);

        return true;
    }

    if (formation == "default" || formation == "reset")
    {
        value->Reset();
        std::ostringstream str; str << "Formation reset to: " << value->Get()->GetName();
        ai->TellPlayer(requester, str);
        return true;
    }

    if (!value->LoadUserChoice(formation))
    {
        std::ostringstream str; str << "Invalid formation: |cffff0000" << formation;
        ai->TellPlayer(requester, str);
        ai->TellPlayer(requester, "Please set to any of: |cffffffffnear, queue, chaos, circle, line, shield, arrow, melee, far, default");
        return false;
    }

    std::ostringstream str; str << "Formation set to: " << formation;
    ai->TellPlayer(requester, str);

    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
    {
        bot->GetMotionMaster()->Clear();
    }

    return true;
}

WorldLocation MoveFormation::MoveLine(std::vector<Player*> line, float diff, float cx, float cy, float cz, float orientation, float range)
{
    if (line.size() < 5)
    {
        return MoveSingleLine(line, diff, cx, cy, cz, orientation, range);
    }

    int lines = ceil((double)line.size() / 5.0);
    for (int i = 0; i < lines; i++)
    {
        float radius = range * i;
        float x = cx + cos(orientation) * radius;
        float y = cy + sin(orientation) * radius;
        std::vector<Player*> singleLine;
        for (int j = 0; j < 5 && !line.empty(); j++)
        {
            singleLine.push_back(line[line.size() - 1]);
            line.pop_back();
        }

        WorldLocation loc = MoveSingleLine(singleLine, diff, x, y,cz, orientation, range);
        if (!Formation::IsNullLocation(loc))
            return loc;
    }

    return Formation::NullLocation;
}

WorldLocation MoveFormation::MoveSingleLine(std::vector<Player*> line, float diff, float cx, float cy, float cz, float orientation, float range)
{
    float count = line.size();
    float angle = orientation - M_PI / 2.0f;
    float x = cx + cos(angle) * (range * floor(count / 2.0f) + diff);
    float y = cy + sin(angle) * (range * floor(count / 2.0f) + diff);

    int index = 0;
    for (std::vector<Player*>::iterator i = line.begin(); i != line.end(); i++)
    {
        Player* member = *i;

        if (member == bot)
        {
            float angle = orientation + M_PI / 2.0f;
            float radius = range * index;

            float lx = x + cos(angle) * radius;
            float ly = y + sin(angle) * radius;
            float lz = cz;

            if (!FinalizeFollowSpot(bot, bot->GetMap(), cx, cy, cz, lx, ly, lz, true))
                return Formation::NullLocation;

            return WorldLocation(bot->GetMapId(), lx, ly, lz);
        }

        index++;
    }

    return Formation::NullLocation;
}
