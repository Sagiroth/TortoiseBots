#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/TravelNode.h"

namespace ai
{
    // Which return-false site fired on the last failed MoveTo/MoveTo2/
    // DispatchMovement call. Recorded in LastMovement::moveFailReason right
    // before each `return false` so the TravelMoveFailed row can say more
    // than the standalone navmesh probe bucket; 0 = none yet / last move
    // dispatched fine. Numbers are log codes, never reordered.
    enum MoveFailReason : uint32
    {
        MOVE_FAIL_NONE = 0,
        MOVE_FAIL_INVALID_DEST = 1,   // MoveTo2: destination failed isValid()
        MOVE_FAIL_CANT_MOVE = 2,      // MoveTo2: ai->CanMove() (root/stun/fear/fall/taxi/...)
        MOVE_FAIL_ARRIVED = 3,        // MoveTo2: already within targetPosRecalcDistance
        MOVE_FAIL_EMPTY_ROUTE = 4,    // MoveTo2: ResolveMovePath came back empty
        MOVE_FAIL_SHORTCUT_EMPTY = 5, // MoveTo2: makeShortCut cleared the path (far from route)
        MOVE_FAIL_SPECIAL = 6,        // MoveTo2: HandleSpecialMovement declined (portal/taxi/hearth)
        MOVE_FAIL_TRANSPORT = 7,      // MoveTo2: still on a transport past the special leg
        MOVE_FAIL_CLIPPED_EMPTY = 8,  // MoveTo2: ClipPath emptied the path (enemy/hazard/window)
        MOVE_FAIL_DISPATCH_SHORT = 9, // DispatchMovement: fewer than 2 points, nothing launched
        MOVE_FAIL_HAZARD_SHORT = 10,  // DispatchMovement: hazard rewrite left nothing to walk
        MOVE_FAIL_BAD_UNIT_TARGET = 11, // MoveTo(Unit*): null target or not in world
        MOVE_FAIL_NO_FORMATION = 12,  // MoveTo(Unit*): hostile target with no formation slot
    };

    inline const char* MoveFailReasonName(uint32 reason)
    {
        switch (reason)
        {
            case MOVE_FAIL_INVALID_DEST: return "invalid-dest";
            case MOVE_FAIL_CANT_MOVE: return "cantmove";
            case MOVE_FAIL_ARRIVED: return "arrived-recalc";
            case MOVE_FAIL_EMPTY_ROUTE: return "empty-path";
            case MOVE_FAIL_SHORTCUT_EMPTY: return "shortcut-empty";
            case MOVE_FAIL_SPECIAL: return "special";
            case MOVE_FAIL_TRANSPORT: return "transport";
            case MOVE_FAIL_CLIPPED_EMPTY: return "clipped-empty";
            case MOVE_FAIL_DISPATCH_SHORT: return "dispatch-short";
            case MOVE_FAIL_HAZARD_SHORT: return "hazard-short";
            case MOVE_FAIL_BAD_UNIT_TARGET: return "bad-unit-target";
            case MOVE_FAIL_NO_FORMATION: return "no-formation";
            default: return "none";
        }
    }

    class LastMovement
    {
    public:
        LastMovement()
        {
            clear();
        }

        LastMovement(LastMovement& other)
        {
            taxiNodes = other.taxiNodes;
            taxiMaster = other.taxiMaster;
            lastFollow = other.lastFollow;
            lastAreaTrigger = other.lastAreaTrigger;
            lastTransportEntry = other.lastTransportEntry;
            lastPath = other.lastPath;
            lastMoveShort = other.lastMoveShort;
            nextTeleport = other.nextTeleport;
            fleeCount = other.fleeCount;
            lastFleeAttempt = other.lastFleeAttempt;
            lastFleeAngles[0] = other.lastFleeAngles[0];
            lastFleeAngles[1] = other.lastFleeAngles[1];
            lastFleeAngleCount = other.lastFleeAngleCount;
            lastSpreadStepMs = other.lastSpreadStepMs;
            moveFailReason = other.moveFailReason;
            moveEvent = Event();
        }

        void clear()
        {
            lastPath.clear();
            lastFollow = NULL;
            lastAreaTrigger = 0;
            lastTransportEntry = 0;
            lastFlee = 0;
            fleeCount = 0;
            lastFleeAttempt = 0;
            lastFleeAngles[0] = 10.0f;
            lastFleeAngles[1] = 10.0f;
            lastFleeAngleCount = 0;
            lastSpreadStepMs = 0;
            lastMoveShort = WorldPosition();
            nextTeleport = 0;
            moveFailReason = 0;
            moveEvent = Event();
        }

        void Set(Unit* lastFollow)
        {
            setPath(TravelPath());
            this->lastFollow = lastFollow;
        }

        void setPath(TravelPath path) { lastPath = path; }
    public:
        std::vector<uint32> taxiNodes;
        ObjectGuid taxiMaster;
        Unit* lastFollow;
        uint32 lastAreaTrigger;
        uint32 lastTransportEntry;
        time_t lastFlee;
        // Number of flee actions dispatched in quick succession (within returnDelay of each
        // other). Used to detect a "subsequent" flee loop so spellcasting can take priority.
        uint32 fleeCount;
        // Wall-clock of the last dispatched flee, used to decide whether the next flee is
        // "subsequent" (close in time) or a fresh flee (window lapsed -> count resets).
        time_t lastFleeAttempt;
        // Donor "recently flee info" (mod-playerbots MovementAction::FleePosition/
        // CheckLastFlee): last two flee destination angles, so a repeated flee
        // steps somewhere else instead of re-picking the same bad vector.
        // Angles are absolute world headings like FleeManager's ring uses;
        // 10.0f marks an empty slot. Written only on a dispatched combat flee.
        float lastFleeAngles[2] = { 10.0f, 10.0f };
        uint32 lastFleeAngleCount = 0;
        // WorldTimer ms of the last spread/flee step-out dispatch. Throttles
        // RaidSpreadAction to one step per kSpreadStepCooldownMs so stacked
        // ranged bots settle instead of ping-ponging every tick. 0 = none yet.
        uint32 lastSpreadStepMs = 0;
        TravelPath lastPath;
        WorldPosition lastMoveShort;
        time_t nextTeleport;
        // MoveFailReason above: which return-false site fired last (0 = none
        // yet / last move dispatched fine). Stamped right before each
        // `return false` so the TravelMoveFailed row can say more than the
        // navmesh probe bucket; cleared on success paths.
        uint32 moveFailReason;
        Event moveEvent;
    };

    class LastMovementValue : public ManualSetValue<LastMovement&>
    {
    public:
        LastMovementValue(PlayerbotAI* ai) : ManualSetValue<LastMovement&>(ai, data) {}
    private:
        LastMovement data = LastMovement();
    };

    class StayTimeValue : public ManualSetValue<time_t>
    {
    public:
        StayTimeValue(PlayerbotAI* ai) : ManualSetValue<time_t>(ai, 0) {}
    };

    class LastLongMoveValue : public CalculatedValue<WorldPosition>
    {
    public:
        LastLongMoveValue(PlayerbotAI* ai) : CalculatedValue<WorldPosition>(ai, "last long move", 30) {}

        WorldPosition Calculate() override;
    };


    class HomeBindValue : public CalculatedValue<WorldPosition>
    {
    public:
        HomeBindValue(PlayerbotAI* ai) : CalculatedValue<WorldPosition>(ai, "home bind", 30) {}

        WorldPosition Calculate() override;

        virtual std::string Format() override;
    };
}
