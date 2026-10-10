
#include "playerbot/playerbot.h"
#include "NearestGameObjects.h"

#include "playerbot/ServerFacade.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

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

std::list<ObjectGuid> NearestDamagingTrapsValue::Calculate()
{
    std::list<GameObject*> targets;
    AnyGameObjectInObjectRangeCheck u_check(bot, kMaxAoeAvoidRadiusYd);
    GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> searcher(targets, u_check);
    Cell::VisitAllObjects((const WorldObject*)bot, searcher, kMaxAoeAvoidRadiusYd);

    std::list<ObjectGuid> result;
    for (std::list<GameObject*>::iterator tIter = targets.begin(); tIter != targets.end(); ++tIter)
    {
        GameObject* go = *tIter;
        if (!go)
            continue;
        GameObjectInfo const* goInfo = go->GetGOInfo();
        if (!goInfo || goInfo->type != GAMEOBJECT_TYPE_TRAP)
            continue;
        // Hunter/snare traps owned by a friendly stay; only ownerless (or
        // hostile-owned) damage traps mark a zone.
        if (!go->GetOwnerGuid().IsEmpty())
        {
            Unit* owner = go->GetOwner();
            if (owner && sServerFacade.IsFriendlyTo(owner, bot))
                continue;
        }
        uint32 spellId = goInfo->trap.spellId;
        if (!spellId)
            continue;
        SpellEntry const* spellProto = sServerFacade.LookupSpellInfo(spellId);
        if (!spellProto || spellProto->IsPositiveEffect(EFFECT_INDEX_0))
            continue;
        for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            if (spellProto->Effect[i] == SPELL_EFFECT_SCHOOL_DAMAGE)
            {
                result.push_back(go->getObjectGuid());
                break;
            }
            if (spellProto->Effect[i] == SPELL_EFFECT_APPLY_AURA &&
                spellProto->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE)
            {
                result.push_back(go->getObjectGuid());
                break;
            }
        }
    }

    return result;
}
