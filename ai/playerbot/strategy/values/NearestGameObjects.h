#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/AvoidAoePolicy.h"

namespace ai
{
    enum LineOfSight
    {
        LOS_IGNORE = 0,
        LOS_STATIC = 1,
        LOS_FULL   = 2,
    };

    class AnyGameObjectInObjectRangeCheck
    {
    public:
        AnyGameObjectInObjectRangeCheck(WorldObject const* obj, float range) : i_obj(obj), i_range(range) {}
        WorldObject const& GetFocusObject() const { return *i_obj; }
        bool operator()(GameObject* u);
    private:
        WorldObject const* i_obj;
        float i_range;
    };

    class GameObjectsInObjectRangeCheck
    {
    public:
        GameObjectsInObjectRangeCheck(WorldObject const* obj, float range, uint32 gameObjectID) : i_obj(obj), i_range(range), i_gameObjectID(gameObjectID) {}
        WorldObject const& GetFocusObject() const { return *i_obj; }
        bool operator()(GameObject* u);
    private:
        WorldObject const* i_obj;
        float i_range;
        uint32 i_gameObjectID;
    };

    class NearestGameObjects : public ObjectGuidListCalculatedValue, public Qualified
	{
	public:
        NearestGameObjects(PlayerbotAI* ai, float range = sPlayerbotAIConfig.sightDistance, LineOfSight lineOfSight = LOS_FULL, std::string name = "nearest game objects") :
            ObjectGuidListCalculatedValue(ai, name), range(range), lineOfSight(lineOfSight), Qualified() {}

    protected:
        virtual std::list<ObjectGuid> Calculate() override;

    private:
        float range;
        LineOfSight lineOfSight;
	};

    // Proactive AoE sensor (donor NearestTrapWithDamageValue shape):
    // ownerless GAMEOBJECT_TYPE_TRAP GOs within kTrapScanRangeYd (30 yd) whose
    // trap spell is non-positive and deals periodic or school damage. LoS
    // ignored — the trap sits inside the zone it marks.
    class NearestDamagingTrapsValue : public ObjectGuidListCalculatedValue
	{
	public:
        NearestDamagingTrapsValue(PlayerbotAI* ai) :
            ObjectGuidListCalculatedValue(ai, "nearest damaging traps", 2) { this->keepLegacyRefresh = true; }

    protected:
        virtual std::list<ObjectGuid> Calculate() override;
	};

    // The circle a listed trap actually hits: its spell's radius when the spell
    // is an AoE at the source (SpellRadius lookup, + the policy margin), the GO's
    // data2 trap radius when it is not (or has no radius).
    float DamagingTrapHazardRadius(GameObject* go);

    // Every listed damaging trap around the bot as a hazard circle (the
    // "nearest damaging traps" value), for a step-out landing and the trap hold.
    void CollectDamagingTrapHazards(PlayerbotAI* ai, Player* bot, std::vector<AoeHazardCircle>& out);

    // The damaging traps that can reach a destination point: the cached list
    // for a point in the bot's neighbourhood, one bounded query around the
    // point itself for a destination beyond it (a held walk can end there).
    void CollectDamagingTrapHazardsAt(PlayerbotAI* ai, Player* bot, float x, float y, float z,
        std::vector<AoeHazardCircle>& out);

    // Every listed hostile ground cloud around the bot as a hazard circle: a
    // persistent-area-aura dynamic object (Poison Cloud, Blizzard, Rain of
    // Fire, Consecration). A positive effect's circle is skipped, and so is a
    // player-controlled caster's friendly circle (a group mage's Blizzard),
    // while a creature's damaging cloud stays a hazard whatever its faction.
    // With `creatureCastersOnly` a player-controlled caster is skipped
    // altogether: the step-out and the hold leave the open world and PvP
    // against players unchanged.
    void CollectDamagingDynObjHazards(PlayerbotAI* ai, Player* bot, std::vector<AoeHazardCircle>& out,
        bool creatureCastersOnly = false);

    // The hostile dynamic objects that can reach a destination point: the
    // cached list for a point in the bot's neighbourhood, one bounded query
    // around the point itself for a destination beyond it.
    void CollectDamagingDynObjHazardsAt(PlayerbotAI* ai, Player* bot, float x, float y, float z,
        std::vector<AoeHazardCircle>& out, bool creatureCastersOnly = false);

    class NearestDynamicObjects : public ObjectGuidListCalculatedValue
    {
    public:
        NearestDynamicObjects(PlayerbotAI* ai, float range = sPlayerbotAIConfig.farDistance, LineOfSight lineOfSight = LOS_FULL, std::string name = "nearest dynamic objects") :
            ObjectGuidListCalculatedValue(ai, name), range(range), lineOfSight(lineOfSight) {}

    protected:
        virtual std::list<ObjectGuid> Calculate() override;

    private:
        float range;
        LineOfSight lineOfSight;
    };
}
