#pragma once
#include "playerbot/PlayerbotAI.h"

#include "playerbot/strategy/Action.h"
#include "playerbot/strategy/NamedObjectContext.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/TravelNode.h"
#include "Movement/WaypointMovementGenerator.h"
#include "playerbot/strategy/values/HazardsValue.h"
#include "playerbot/strategy/values/LastMovementValue.h"

namespace ai
{
    class MovementAction : public Action
    {
    public:
        MovementAction(PlayerbotAI* ai, std::string name) : Action(ai, name) {}

        static bool MinimalMove(PlayerbotAI* ai);
        static bool UseTransport(PlayerbotAI* ai, uint32 entry, WorldPosition dockPosition, WorldPosition exitPosition, bool doTeleport);
    protected:
        static bool MoveOnTransport(PlayerbotAI* ai, GenericTransport* transport, bool doTeleport);
        static bool MoveOffTransport(PlayerbotAI* ai, WorldPosition exitPos, bool doTeleport);

        bool ChaseTo(WorldObject *obj, float distance = 0.0f, float angle = 0.0f);
        bool MoveNear(uint32 mapId, float x, float y, float z, float distance = sPlayerbotAIConfig.contactDistance);
        bool FlyDirect(const WorldPosition &startPosition,  const WorldPosition &endPosition , WorldPosition& movePosition, TravelPath movePath);

        inline bool MoveTo(const WorldLocation& location, bool idle = false, bool react = false, bool noPath = false, bool ignoreEnemyTargets = false)
        {
           return MoveTo(location.mapId, location.x, location.y, location.z, idle, react, noPath, ignoreEnemyTargets);
        }

        static bool UseTaxi(PlayerbotAI* ai, uint32 entry = 0, bool needNpc = true);


        bool WaitForTransport();


        TravelPath ResolveMovePath(const WorldPosition& startPosition,
            const WorldPosition& endPosition,
            Unit* mover,
            LastMovement& lastMove);

        bool HandleSpecialMovement(TravelPath& path);

        void UpdateFlyingState(WorldPosition& movePosition,
            float totalDistance,
            float originalZ,
            float maxDist,
            bool isWalking);

        // False when the path leads nowhere (fewer than two points): nothing was launched.
        bool DispatchMovement(TravelPath movePath, bool generatePath, bool masterWalking);

        Unit* GetMover(Player* bot);

        bool MoveTo2(const WorldPosition& endPos, bool idle = false, bool react = false, bool noPath = false, bool ignoreEnemyTargets = false);

        bool MoveTo(uint32 mapId, float x, float y, float z, bool idle = false, bool react = false, bool noPath = false, bool ignoreEnemyTargets = false);
        bool MoveTo(Unit* target, float distance = 0.0f);
        bool MoveNear(WorldObject* target, float distance = sPlayerbotAIConfig.contactDistance);
        float GetFollowAngle();
        bool Follow(Unit* target, float distance = 0);
        bool Follow(Unit* target, float distance, float angle);
        float MoveDelay(float distance);
        bool FollowOnTransport(Unit* target);

        void WaitForReach(float distance);
        void WaitForReach(const Movement::PointsArray& path);

        bool IsMovingAllowed(Unit* target);
        bool IsMovingAllowed(uint32 mapId, float x, float y, float z);
        // A place the bot can stand, path to and see from: valid map
        // coordinates, a navmesh path, line of sight to visibleFromPosition and
        // no hazard. Shared by point moves that must not end inside geometry.
        bool IsValidPosition(const WorldPosition& position, const WorldPosition& visibleFromPosition);
        bool Flee(Unit *target);
        void ClearIdleState();
        void UpdateMovementState();

        bool isPossible() override;
        bool isUseful() override;

        void CreateWp(Player* wpOwner, float x, float y, float z, float o, uint32 entry, bool important = false);
        float GetAngle(const float x1, const float y1, const float x2, const float y2);

        // Used when this action is executed as a reaction
        bool ShouldReactionInterruptCast() const override { return true; }
        bool ShouldReactionInterruptMovement() const override { return true; }

    private:
        bool IsHazardNearPosition(const WorldPosition& position, HazardPosition* outHazard = nullptr);
        bool GeneratePathAvoidingHazards(std::vector<WorldPosition>& movePath);
    };

    class FleeAction : public MovementAction
    {
    public:
        FleeAction(PlayerbotAI* ai, float distance = sPlayerbotAIConfig.spellDistance) : MovementAction(ai, "flee"), distance(distance) {}
        virtual bool Execute(Event& event) override;

    private:
        float distance;
    };

    class FleeWithPetAction : public MovementAction
    {
    public:
        FleeWithPetAction(PlayerbotAI* ai) : MovementAction(ai, "flee with pet") {}
        virtual bool Execute(Event& event) override;
    };

    class RunAwayAction : public MovementAction
    {
    public:
        RunAwayAction(PlayerbotAI* ai) : MovementAction(ai, "runaway") {}
        virtual bool Execute(Event& event) override;
    };

    class MoveToLootAction : public MovementAction
    {
    public:
        MoveToLootAction(PlayerbotAI* ai) : MovementAction(ai, "move to loot") {}
        virtual bool Execute(Event& event) override;

    private:
        // A launched move can still lead nowhere (navmesh path degraded to a direct spline), so
        // the bounded give-up also needs a no-progress watchdog. See the implementation.
        bool StuckOnApproach(ObjectGuid guid);

        ObjectGuid approachGuid;
        float approachX = 0.0f;
        float approachY = 0.0f;
        time_t approachSince = 0;
    };

    class MoveOutOfEnemyContactAction : public MovementAction
    {
    public:
        MoveOutOfEnemyContactAction(PlayerbotAI* ai) : MovementAction(ai, "move out of enemy contact") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
    };

    class SetFacingTargetAction : public Action
    {
    public:
        SetFacingTargetAction(PlayerbotAI* ai) : Action(ai, "set facing") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
        virtual bool isPossible() override;
    };

    class SetBehindTargetAction : public MovementAction
    {
    public:
        SetBehindTargetAction(PlayerbotAI* ai) : MovementAction(ai, "set behind") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
        virtual bool isPossible() override;
    };

    // Generic rear-flank (mod-playerbots RearFlankAction shape): a melee
    // bot in the mob's frontal arc or tail cone sidesteps to the nearer
    // flank instead of walking straight through the cleave to the exact
    // rear point. Set-behind keeps the final rear point; this owns the
    // approach. Dragon raid geometry untouched (entry-gated, tighter cones).
    class RearFlankAction : public MovementAction
    {
    public:
        RearFlankAction(PlayerbotAI* ai) : MovementAction(ai, "rear flank") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
        virtual bool isPossible() override;
    };

    // Party tank-face (night2 research): sidesteps to the far side of the
    // held mob so its front points away from the party. Generic version of
    // the raid-dragon TankFaceAway; raid paths untouched.
    class TankFaceAwayAction : public MovementAction
    {
    public:
        TankFaceAwayAction(PlayerbotAI* ai) : MovementAction(ai, "tank face away") {}
        virtual bool Execute(Event& event) override;
        virtual bool isPossible() override { return MovementAction::isPossible() && ai->CanMove(); }
    };

    class MoveOutOfCollisionAction : public MovementAction
    {
    public:
        MoveOutOfCollisionAction(PlayerbotAI* ai) : MovementAction(ai, "move out of collision") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
    };

    class MoveRandomAction : public MovementAction
    {
    public:
        MoveRandomAction(PlayerbotAI* ai) : MovementAction(ai, "move random") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
    };

    class IdleWanderAction : public MovementAction
    {
    public:
        IdleWanderAction(PlayerbotAI* ai) : MovementAction(ai, "idle wander") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
    };

    class MoveToAction : public MovementAction, public Qualified
    {
    public:
        MoveToAction(PlayerbotAI* ai, std::string name = "move to") : MovementAction(ai, "name"), Qualified() {}
        virtual bool Execute(Event& event) override;
    };

    class JumpAction : public MovementAction, public Qualified
    {
    public:
        JumpAction(PlayerbotAI* ai) : MovementAction(ai, "jump"), Qualified() {}
        bool Execute(Event& event) override;
        bool isUseful() override;

        static WorldPosition CalculateJumpParameters(const WorldPosition& src, Unit* jumper, float angle, float vSpeed, float hSpeed, float &timeToLand, float &distanceToLand, float &maxHeight, bool &goodLanding, std::vector<WorldPosition> &path, float maxJumpHeight = sPlayerbotAIConfig.jumpHeightLimit);

    private:
        bool DoJump(const WorldPosition& dest, const WorldPosition& highestPoint, float angle, float vSpeed, float hSpeed, float timeToLand, float distanceToLand, float maxHeight, bool goodLanding, bool jumpInPlace, bool jumpBackward, bool showOnly);
        bool JumpTowards(const WorldPosition& src, const WorldPosition& dest, Unit* jumper, float jumpSpeed, bool preSetLanding = false);
        static float CalculateJumpTime(float srcZ, float destZ, float vSpeed, float hSpeed, float distance);
        static float CalculateJumpTime(float z_diff, float vSpeed, bool ascending);

        WorldPosition GetPossibleJumpStartFor(const WorldPosition& src, const WorldPosition& dest, WorldPosition& possiblelanding, Unit* jumper, float &requiredSpeed, float distanceTo, float distanceFrom);
        WorldPosition GetPossibleJumpStartForInRange(const WorldPosition& src, const WorldPosition& dest, WorldPosition& possiblelanding, Unit* jumper, float& requiredSpeed, float distanceTo, float distanceFrom);
        bool CanJumpTo(const WorldPosition& src, const WorldPosition& dest, WorldPosition& possiblelanding, float& jumpAngle, Unit* jumper, float jumpSpeed, float maxDistance = 10.0f);
        bool CanWalkTo(const WorldPosition& src, const WorldPosition& dest, Unit* jumper, float maxDistance = sPlayerbotAIConfig.sightDistance);
        bool IsJumpFasterThanWalking(const WorldPosition& src, const WorldPosition& dest, const WorldPosition& jumpLanding, Unit* jumper, float maxDistance = sPlayerbotAIConfig.sightDistance);

        static bool IsJumpSafe(const WorldPosition& src, const WorldPosition& dest, Unit* jumper);
        static bool CanLand(const WorldPosition& dest, Unit* jumper);
        static bool IsNotMagmaSlime(const WorldPosition& dest, Unit* jumper);
    };
}
