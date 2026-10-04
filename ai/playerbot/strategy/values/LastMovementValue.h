#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/TravelNode.h"

namespace ai
{
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
