#pragma once

#include "WorldPosition.h"

class Player;

namespace ai
{
    class Engine;
    class FleeFailureMemory;



	class FleePoint {
	public:
        FleePoint(PlayerbotAI* ai, float x, float y, float z) : ai(ai), sumDistance(0.0f), minDistance(0.0f) {
			this->x = x;
			this->y = y;
			this->z = z;
		}

	public:
		float x;
		float y;
		float z;

        float sumDistance;
        float minDistance;

    private:
        PlayerbotAI* ai;
	};

    // Hostile mobs that are not fighting yet and that one back-off step could
    // wake. Built once per flee from a cached target list, then asked per
    // candidate landing spot.
    class IdleMobVeto
    {
    public:
        IdleMobVeto(PlayerbotAI* ai, Player* bot, std::list<ObjectGuid> const& units, Unit* ignore, float stepLength);
        bool Wakes(float x, float y) const;

    private:
        struct Sleeper { Unit* unit; float radius; float toBot; };
        static constexpr int kMaxSleepers = 16;
        Sleeper sleepers[kMaxSleepers];
        int count = 0;
    };

	class FleeManager
	{
	public:
        FleeManager(Player* bot, float maxAllowedDistance, float followAngle, bool forceMaxDistance = false, WorldPosition startPosition = WorldPosition()) {
			this->bot = bot;
			this->maxAllowedDistance = maxAllowedDistance;
			this->followAngle = followAngle;
            this->forceMaxDistance = forceMaxDistance;
			this->startPosition = startPosition ? startPosition : WorldPosition(bot);
		}

	public:
		bool CalculateDestination(float* rx, float* ry, float* rz,
            FleeFailureMemory const* failures = nullptr, uint32 nowMs = 0);
        bool isUseful();
        bool IsUseful() { return isUseful(); }
        void SetIdleMobVeto(IdleMobVeto const* veto) { idleMobVeto = veto; }

	private:
		void calculatePossibleDestinations(std::list<FleePoint*> &points);
		void calculateDistanceToCreatures(FleePoint *point);
		void cleanup(std::list<FleePoint*> &points);
		FleePoint* selectOptimalDestination(std::list<FleePoint*> &points,
            FleeFailureMemory const* failures, uint32 nowMs);
		bool isBetterThan(FleePoint* point, FleePoint* other);
		bool isTooCloseToEdge(float x, float y, float z, float angle);

	private:
		Player* bot;
		float maxAllowedDistance;
		float followAngle;
        bool forceMaxDistance;
		WorldPosition startPosition;
        IdleMobVeto const* idleMobVeto = nullptr;
	};

};
