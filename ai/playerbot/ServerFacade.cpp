
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"

#include "Database/DatabaseEnv.h"
#include "PlayerbotAI.h"

#include "Movement/TargetedMovementGenerator.h"

ServerFacade::ServerFacade() {}
ServerFacade::~ServerFacade() {}

float ServerFacade::GetDistance2d(Unit *unit, WorldObject* wo)
{
    if (!unit || !wo)
        return false;

    float dist =
#ifdef MANGOS
    unit->getDistance2d(wo);
#endif
#ifdef CMANGOS
    unit->GetDistance2d(wo);
#endif
    return round(dist * 10.0f) / 10.0f;
}

float ServerFacade::GetDistance2d(Unit *unit, float x, float y)
{
    float dist =
#ifdef MANGOS
    unit->getDistance2d(x, y);
#endif
#ifdef CMANGOS
    unit->GetDistance2d(x, y);
#endif
    return round(dist * 10.0f) / 10.0f;
}

bool ServerFacade::IsDistanceLessThan(float dist1, float dist2)
{
    return dist1 - dist2 < sPlayerbotAIConfig.targetPosRecalcDistance;
}

bool ServerFacade::IsDistanceGreaterThan(float dist1, float dist2)
{
    return dist1 - dist2 > sPlayerbotAIConfig.targetPosRecalcDistance;
}

bool ServerFacade::IsDistanceGreaterOrEqualThan(float dist1, float dist2)
{
    return !IsDistanceLessThan(dist1, dist2);
}

bool ServerFacade::IsDistanceLessOrEqualThan(float dist1, float dist2)
{
    return !IsDistanceGreaterThan(dist1, dist2);
}

void ServerFacade::SetFacingTo(Unit* unit, float angle, bool force)
{
    MotionMaster &mm = *unit->GetMotionMaster();
    if (!force && !unit->IsStopped()) unit->SetFacingTo(angle);
    else
    {
        unit->SetOrientation(angle);
        unit->SendHeartBeat();
    }
    //unit->m_movementInfo.RemoveMovementFlag(MovementFlags(MOVEFLAG_SPLINE_ENABLED | MOVEFLAG_FORWARD));
}

bool ServerFacade::IsFriendlyTo(Unit* bot, Unit* to)
{
#ifdef MANGOS
    return bot->IsFriendlyTo(to);
#endif
#ifdef CMANGOS
    return bot->IsFriendlyTo(to);
#endif
}

bool ServerFacade::IsHostileTo(Unit* bot, Unit* to)
{
#ifdef MANGOS
    return bot->IsHostileTo(to);
#endif
#ifdef CMANGOS
    return bot->IsHostileTo(to);
#endif
}

bool ServerFacade::IsFriendlyTo(WorldObject* bot, Unit* to)
{
#ifdef MANGOS
    return bot->IsFriendlyTo(to);
#endif
#ifdef CMANGOS
    return bot->IsFriendlyTo(to);
#endif
}

bool ServerFacade::IsHostileTo(WorldObject* bot, Unit* to)
{
#ifdef MANGOS
    return bot->IsHostileTo(to);
#endif
#ifdef CMANGOS
    return bot->IsHostileTo(to);
#endif
}


bool ServerFacade::IsSpellReady(Player* bot, uint32 spell, uint32 itemId)
{
#ifdef MANGOS
    return !bot->HasSpellCooldown(spell);
#endif
#ifdef CMANGOS
    if (itemId)
    {
        const ItemPrototype* proto = sObjectMgr.GetItemPrototype(itemId);
        return !bot->HasSpellCooldown(spell);
    }
    else
        return !bot->HasSpellCooldown(spell);
#endif
}



bool ServerFacade::IsUnderwater(Unit *unit)
{
#ifdef MANGOS
    return unit->IsUnderWater();
#endif
#ifdef CMANGOS
    return unit->IsUnderwater();
#endif
}

FactionTemplateEntry const* ServerFacade::GetFactionTemplateEntry(Unit *unit)
{
#ifdef MANGOS
    return unit->GetFactionTemplateEntry();
#endif
#ifdef CMANGOS
    return unit->GetFactionTemplateEntry();
#endif
}

namespace
{
template<class T>
Unit* GetTargetedMovementTarget(T* unit)
{
    if (!unit || !unit->GetMotionMaster())
        return nullptr;

    MovementGenerator const* generator = unit->GetMotionMaster()->GetCurrent();
    if (!generator)
        return nullptr;

    switch (generator->GetMovementGeneratorType())
    {
        case CHASE_MOTION_TYPE:
            return static_cast<ChaseMovementGenerator<T> const*>(generator)->GetTarget();
        case FOLLOW_MOTION_TYPE:
            return static_cast<FollowMovementGenerator<T> const*>(generator)->GetTarget();
        default:
            return nullptr;
    }
}

float RelativeTargetAngle(Unit* unit, Unit* target)
{
    if (!unit || !target)
        return 0.0f;

    float angle = target->GetAngle(unit) - target->GetOrientation();
    while (angle > M_PI_F)
        angle -= 2.0f * M_PI_F;
    while (angle < -M_PI_F)
        angle += 2.0f * M_PI_F;
    return angle;
}
}

Unit* ServerFacade::GetChaseTarget(Unit* target)
{
    if (!target)
        return nullptr;

    if (target->GetTypeId() == TYPEID_PLAYER)
        return GetTargetedMovementTarget(static_cast<Player*>(target));
    if (target->GetTypeId() == TYPEID_UNIT)
        return GetTargetedMovementTarget(static_cast<Creature*>(target));
    return nullptr;
}

float ServerFacade::GetChaseAngle(Unit* target)
{
    return RelativeTargetAngle(target, GetChaseTarget(target));
}

float ServerFacade::GetChaseOffset(Unit* target)
{
    Unit* chaseTarget = GetChaseTarget(target);
    return target && chaseTarget ? target->GetDistance2d(chaseTarget) : 0.0f;
}

bool ServerFacade::isMoving(Unit *unit)
{
#ifdef MANGOS
    return unit->m_movementInfo.HasMovementFlag(movementFlagsMask);
#endif
#ifdef CMANGOS
    return !unit->IsStopped();
#endif
}
