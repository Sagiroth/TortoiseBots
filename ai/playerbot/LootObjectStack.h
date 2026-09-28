#pragma once
#include "playerbot.h"
#include <map>
#include <utility>

namespace ai
{
    class ItemQualifier;

    class LootObject
    {
    public:
        LootObject() : skillId(0), reqSkillValue(0), reqItem(0) {}
        LootObject(Player* bot, ObjectGuid guid);
        LootObject(const LootObject& other);

    public:
        bool IsEmpty() { return !guid; }
        bool IsLootPossible(Player* bot);
        void Refresh(Player* bot, ObjectGuid guid, bool debug = false);
        WorldObject* GetWorldObject(Player* bot);
        ObjectGuid guid;

        uint32 skillId;
        uint32 reqSkillValue;
        uint32 reqItem;
    };

    class LootTarget
    {
    public:
        LootTarget(ObjectGuid guid);
        LootTarget(LootTarget const& other);

    public:
        LootTarget& operator=(LootTarget const& other);
        bool operator< (const LootTarget& other) const;

    public:
        ObjectGuid guid;
        time_t asOfTime;
    };

    class LootTargetList : public std::set<LootTarget>
    {
    public:
        void shrink(time_t fromTime);
    };

    class LootObjectStack
    {
    public:
        LootObjectStack(Player* bot) : bot(bot) {}

    public:
        bool Add(ObjectGuid guid);
        void Remove(ObjectGuid guid);
        void Clear();
        bool CanLoot(float maxDistance);
        LootObject GetLoot(float maxDistance = 0);

        // Bounded give-up for corpses we cannot path to (see MoveToLootAction).
        // A corpse is retried a few times; after that it is ignored until the
        // failure memory ages out, so it cannot pin the loot chain forever.
        void NoteApproachFailure(ObjectGuid guid);
        bool IsAbandoned(ObjectGuid guid);

    public:
        std::vector<LootObject> OrderByDistance(float maxDistance = 0);

    private:
        static const uint32 MAX_APPROACH_FAILURES = 3;
        static const time_t APPROACH_FAILURE_TTL = 120;  // seconds

        Player* bot;
        LootTargetList availableLoot;
        std::map<ObjectGuid, std::pair<uint32, time_t> > approachFailures;
    };

};
