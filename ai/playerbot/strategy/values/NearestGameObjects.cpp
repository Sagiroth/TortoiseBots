
#include "playerbot/playerbot.h"
#include "NearestGameObjects.h"

#include "playerbot/ServerFacade.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"
#include "Database/DBCStores.h"

using namespace ai;
using namespace MaNGOS;

bool AnyGameObjectInObjectRangeCheck::operator()(GameObject* u)
{
    if (u && i_obj->IsWithinDistInMap(u, i_range) && sServerFacade.isSpawned(u) && u->GetGOInfo())
        return true;

    return false;
}

bool GameObjectsInObjectRangeCheck::operator()(GameObject* u)
{
    if (u && i_obj->IsWithinDistInMap(u, i_range) && sServerFacade.isSpawned(u) && u->GetGOInfo() && u->GetEntry() == i_gameObjectID)
        return true;

    return false;
}

class AnyDynamicObjectInObjectRangeCheck
{
public:
    AnyDynamicObjectInObjectRangeCheck(WorldObject const* obj, float range) : i_obj(obj), i_range(range) {}
    WorldObject const& GetFocusObject() const { return *i_obj; }
    bool operator()(DynamicObject* u)
    {
        if (u && i_obj->IsWithinDistInMap(u, i_range))
            return true;

        return false;
    }

private:
    WorldObject const* i_obj;
    float i_range;
};

std::list<ObjectGuid> NearestGameObjects::Calculate()
{
    std::list<GameObject*> targets;

    if (!qualifier.empty())
    {
        uint32 gameObjectID = stoi(qualifier);
        GameObjectsInObjectRangeCheck u_check(bot, range, gameObjectID);
        GameObjectListSearcher<GameObjectsInObjectRangeCheck> searcher(targets, u_check);
        Cell::VisitAllObjects((const WorldObject*)bot, searcher, range);
    }
    else
    {
        AnyGameObjectInObjectRangeCheck u_check(bot, range);
        GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> searcher(targets, u_check);
        Cell::VisitAllObjects((const WorldObject*)bot, searcher, range);
    }

    std::list<ObjectGuid> result;
    for(std::list<GameObject*>::iterator tIter = targets.begin(); tIter != targets.end(); ++tIter)
    {
		GameObject* go = *tIter;

        switch (lineOfSight)
        {
            case LOS_STATIC:
                if (!sServerFacade.IsWithinStaticLOSInMap(bot, go))
                {
                    continue;
                }
                break;
            case LOS_FULL:
                if (!sServerFacade.IsWithinLOSInMap(bot, go))
                {
                    continue;
                }
                break;
        }

        result.push_back(go->getObjectGuid());
    }

    return result;
}

std::list<ObjectGuid> NearestDynamicObjects::Calculate()
{
    std::list<DynamicObject*> targets;

    // Penqle has no DynamicObjectListSearcher grid template — dynamic objects
    // are not iterated via grid searchers in this codebase. Leave the list
    // empty; the bot strategy degrades gracefully (returns empty list) until
    // a host hook lands.

    std::list<ObjectGuid> result;
    for (std::list<DynamicObject*>::iterator tIter = targets.begin(); tIter != targets.end(); ++tIter)
    {
        DynamicObject* go = *tIter;

        switch (lineOfSight)
        {
        case LOS_STATIC:
            if (!sServerFacade.IsWithinStaticLOSInMap(bot, go))
            {
                continue;
            }
            break;
        case LOS_FULL:
            if (!sServerFacade.IsWithinLOSInMap(bot, go))
            {
                continue;
            }
            break;
        }

        result.push_back(go->getObjectGuid());
    }

    return result;
}

float ai::DamagingTrapHazardRadius(GameObject* go)
{
    if (!go)
        return 0.0f;
    GameObjectInfo const* goInfo = go->GetGOInfo();
    if (!goInfo || goInfo->type != GAMEOBJECT_TYPE_TRAP)
        return 0.0f;
    float const trapRadius = (float)goInfo->trap.radius + go->GetCombatReach();
    SpellEntry const* spellProto = sServerFacade.LookupSpellInfo(goInfo->trap.spellId);
    if (!spellProto)
        return trapRadius;
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        bool const damaging = spellProto->Effect[i] == SPELL_EFFECT_SCHOOL_DAMAGE ||
            (spellProto->Effect[i] == SPELL_EFFECT_APPLY_AURA &&
                spellProto->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE);
        if (!damaging || spellProto->EffectImplicitTargetA[i] != kAoeAtSourceTargetA ||
            spellProto->EffectImplicitTargetB[i] != kAoeAtSourceTargetB)
            continue;
        SpellRadiusEntry const* radiusEntry = sSpellRadiusStore.LookupEntry(spellProto->EffectRadiusIndex[i]);
        float const spellRadius = radiusEntry ? radiusEntry->Radius : 0.0f;
        if (spellRadius > 0.0f)
            return TrapHazardRadius(spellRadius, trapRadius);
    }
    return trapRadius;
}

namespace
{
// A trap GO that hurts: a non-positive spell with a school-damage or
// periodic-damage effect, not a hunter/snare trap owned by a friend of the bot.
bool IsDamagingTrap(GameObject* go, Player* bot)
{
    if (!go)
        return false;
    GameObjectInfo const* goInfo = go->GetGOInfo();
    if (!goInfo || goInfo->type != GAMEOBJECT_TYPE_TRAP)
        return false;
    // Hunter/snare traps owned by a friendly stay; only ownerless (or
    // hostile-owned) damage traps mark a zone.
    if (!go->GetOwnerGuid().IsEmpty())
    {
        Unit* owner = go->GetOwner();
        if (owner && sServerFacade.IsFriendlyTo(owner, bot))
            return false;
    }
    uint32 const spellId = goInfo->trap.spellId;
    if (!spellId)
        return false;
    SpellEntry const* spellProto = sServerFacade.LookupSpellInfo(spellId);
    if (!spellProto || spellProto->IsPositiveEffect(EFFECT_INDEX_0))
        return false;
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        if (spellProto->Effect[i] == SPELL_EFFECT_SCHOOL_DAMAGE)
            return true;
        if (spellProto->Effect[i] == SPELL_EFFECT_APPLY_AURA &&
            spellProto->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE)
            return true;
    }
    return false;
}

// Trap GOs within `range` of a map point (one bounded visit; the grid is
// never loaded for it).
class TrapsNearPointCheck
{
public:
    TrapsNearPointCheck(uint32 mapId, float x, float y, float z, float range)
        : mapId(mapId), x(x), y(y), z(z), range(range) {}
    bool operator()(GameObject* u)
    {
        if (!u || u->GetMapId() != mapId || !sServerFacade.isSpawned(u) || !u->GetGOInfo())
            return false;
        float const dx = u->GetPositionX() - x;
        float const dy = u->GetPositionY() - y;
        float const dz = u->GetPositionZ() - z;
        return dx * dx + dy * dy + dz * dz <= range * range;
    }

private:
    uint32 mapId;
    float x, y, z, range;
};
}

void ai::CollectDamagingTrapHazards(PlayerbotAI* ai, Player* bot, std::vector<AoeHazardCircle>& out)
{
    out.clear();
    if (!ai || !bot)
        return;
    AiObjectContext* context = ai->GetAiObjectContext();
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "nearest damaging traps"))
    {
        GameObject* go = ai->GetGameObject(guid);
        if (!go || !go->IsInWorld())
            continue;
        float const radius = DamagingTrapHazardRadius(go);
        if (radius <= 0.0f)
            continue;
        AoeHazardCircle hazard;
        hazard.x = go->GetPositionX();
        hazard.y = go->GetPositionY();
        hazard.z = go->GetPositionZ();
        hazard.radius = radius;
        out.push_back(hazard);
    }
}

// The largest hazard reach a destination check allows for past the cached list's edge.
static constexpr float kTrapDestMarginYd = 15.0f;

void ai::CollectDamagingTrapHazardsAt(PlayerbotAI* ai, Player* bot, float x, float y, float z,
    std::vector<AoeHazardCircle>& out)
{
    out.clear();
    if (!ai || !bot)
        return;
    float const dx = bot->GetPositionX() - x;
    float const dy = bot->GetPositionY() - y;
    float const dz = bot->GetPositionZ() - z;
    // The cached list spans every fire that can reach a point only while the
    // point is a hazard's reach inside the list's edge (a 7 yd fire 31 yd from
    // the bot can cover a point 25 yd away); farther points get their own query.
    float const cachedYd = kTrapScanRangeYd - kTrapDestMarginYd;
    if (dx * dx + dy * dy + dz * dz <= cachedYd * cachedYd)
    {
        CollectDamagingTrapHazards(ai, bot, out);
        return;
    }
    // A held destination beyond the list gets its own query: a fire beside it
    // is not in the bot's list, and walking to it would be walking into fire.
    Map* map = bot->GetMap();
    if (!map)
        return;
    std::list<GameObject*> targets;
    TrapsNearPointCheck u_check(bot->GetMapId(), x, y, z, kTrapScanRangeYd);
    GameObjectListSearcher<TrapsNearPointCheck> searcher(targets, u_check);
    Cell::VisitAllObjects(x, y, map, searcher, kTrapScanRangeYd, true);
    for (GameObject* go : targets)
    {
        if (!go->IsInWorld() || !IsDamagingTrap(go, bot))
            continue;
        float const radius = DamagingTrapHazardRadius(go);
        if (radius <= 0.0f)
            continue;
        AoeHazardCircle hazard;
        hazard.x = go->GetPositionX();
        hazard.y = go->GetPositionY();
        hazard.z = go->GetPositionZ();
        hazard.radius = radius;
        out.push_back(hazard);
    }
}

std::list<ObjectGuid> NearestDamagingTrapsValue::Calculate()
{
    std::list<GameObject*> targets;
    AnyGameObjectInObjectRangeCheck u_check(bot, kTrapScanRangeYd);
    GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> searcher(targets, u_check);
    Cell::VisitAllObjects((const WorldObject*)bot, searcher, kTrapScanRangeYd);

    std::list<ObjectGuid> result;
    for (std::list<GameObject*>::iterator tIter = targets.begin(); tIter != targets.end(); ++tIter)
    {
        GameObject* go = *tIter;
        if (IsDamagingTrap(go, bot))
            result.push_back(go->getObjectGuid());
    }

    return result;
}
