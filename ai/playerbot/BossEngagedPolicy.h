#pragma once

// AiPlayerbot.BossTacticsKeepsEngagedBoss: the raid's engaged bosses, kept in a bot's fight after its own attacker
// list lost them. A melee bot that cannot reach an airborne boss gives up on it (AttackersValue::IgnoreTarget), so
// the boss vanishes from that bot's attacker list: no boss-tactic row and no stand seat for it. The bots that still
// see the boss note it in a registry (BossTacticsActions.cpp), and a bot that lost it reads it back while the
// registry entry is fresh, the boss still fights, and the bot itself is in combat within reach.
//
// No core includes: the translation reads the game state into EngagedBossView, and the rules stay testable in
// tools/test_boss_engaged_policy.cpp.

#include <algorithm>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace ai
{
    // A boss no bot saw in its attackers for this long is dropped.
    constexpr std::uint32_t kEngagedBossFreshMs = 10000;
    // At most one registry write per boss per half second.
    constexpr std::uint32_t kEngagedBossNoteMs = 500;
    // The bot reads a registered boss within this (3D).
    constexpr float kEngagedBossReachYd = 150.0f;

    inline bool EngagedBossNoteDue(bool have, std::uint32_t nowMs, std::uint32_t lastSeenMs)
    {
        return !have || static_cast<std::uint32_t>(nowMs - lastSeenMs) >= kEngagedBossNoteMs;
    }

    struct EngagedBossView
    {
        bool known = false;
        std::uint32_t lastSeenMs = 0;
        bool alive = false;
        bool inCombat = false;
        bool evading = false;
        bool botInCombat = false;
        float botToBossYd = 0.0f;
    };

    inline bool EngagedBossUsable(const EngagedBossView& v, std::uint32_t nowMs)
    {
        return v.known && static_cast<std::uint32_t>(nowMs - v.lastSeenMs) < kEngagedBossFreshMs && v.alive &&
               v.inCombat && !v.evading && v.botInCombat && v.botToBossYd <= kEngagedBossReachYd;
    }

    // What the bots remember of one boss: the unit last seen and when.
    struct EngagedBossNote
    {
        std::uint64_t guid = 0;
        std::uint32_t lastSeenMs = 0;
    };

    // The raid's engaged bosses, partitioned by map instance: a note, a read and the sweep of expired entries touch
    // only the caller's own bucket, so one instance's fight never costs another instance's bots a walk over its rows.
    // The one full walk is SweepAbandoned (a map instance with no bots left can never be touched by its own holders
    // again), which the caller throttles. The caller holds its own lock (BossTacticsActions.cpp) and passes the clock
    // it read under that lock.
    struct EngagedBossRegistry
    {
        // An entry no bot refreshed for three freshness windows belongs to a fight that is over.
        static constexpr std::uint32_t kExpireMs = 3 * kEngagedBossFreshMs;

        typedef std::pair<std::uint32_t, std::uint32_t> InstanceKey;  // (mapId, instanceId)
        typedef std::map<std::uint32_t, EngagedBossNote> InstanceBucket;  // boss entry -> note
        std::map<InstanceKey, InstanceBucket> buckets;
        std::uint32_t size = 0;

        static InstanceKey InstanceOf(std::uint32_t mapId, std::uint32_t instanceId)
        {
            return InstanceKey(mapId, instanceId);
        }

        // Records that a bot in this instance saw the boss; then drops the bucket's expired entries and the bucket
        // itself when it empties. Nothing outside this instance is read, written or expired.
        void Note(std::uint32_t mapId, std::uint32_t instanceId, std::uint32_t entry, std::uint64_t guid,
                  std::uint32_t nowMs)
        {
            std::map<InstanceKey, InstanceBucket>::iterator instance = buckets.find(InstanceOf(mapId, instanceId));
            if (instance == buckets.end())
                instance = buckets.insert(std::make_pair(InstanceOf(mapId, instanceId), InstanceBucket())).first;
            InstanceBucket& bucket = instance->second;
            std::map<std::uint32_t, EngagedBossNote>::iterator note = bucket.find(entry);
            const bool have = note != bucket.end();
            if (!EngagedBossNoteDue(have, nowMs, have ? note->second.lastSeenMs : 0))
                return;
            if (have)
                note->second = EngagedBossNote{ guid, nowMs };
            else
            {
                bucket[entry] = EngagedBossNote{ guid, nowMs };
                ++size;
            }
            Prune(mapId, instanceId, nowMs);
        }

        // Drops the instance's expired entries and the bucket when none is left; false when no note remains there.
        // Only this instance's bucket is walked.
        bool Prune(std::uint32_t mapId, std::uint32_t instanceId, std::uint32_t nowMs)
        {
            std::map<InstanceKey, InstanceBucket>::iterator instance = buckets.find(InstanceOf(mapId, instanceId));
            if (instance == buckets.end())
                return false;
            InstanceBucket& bucket = instance->second;
            for (std::map<std::uint32_t, EngagedBossNote>::iterator old = bucket.begin(); old != bucket.end();)
            {
                if (static_cast<std::uint32_t>(nowMs - old->second.lastSeenMs) > kExpireMs)
                {
                    old = bucket.erase(old);
                    --size;
                }
                else
                    ++old;
            }
            if (bucket.empty())
            {
                buckets.erase(instance);
                return false;
            }
            return true;
        }

        // The notes of this instance whose entry the caller has not read yet, after its bucket is pruned (and dropped
        // with its last expired row). Other instances are not visited.
        std::vector<EngagedBossNote> Read(std::uint32_t mapId, std::uint32_t instanceId, std::uint32_t nowMs,
                                          const std::vector<std::uint32_t>& seenEntries)
        {
            std::vector<EngagedBossNote> out;
            if (!Prune(mapId, instanceId, nowMs))
                return out;
            const InstanceBucket& bucket = buckets.find(InstanceOf(mapId, instanceId))->second;
            for (const InstanceBucket::value_type& row : bucket)
            {
                if (std::find(seenEntries.begin(), seenEntries.end(), row.first) == seenEntries.end())
                    out.push_back(row.second);
            }
            return out;
        }

        // The registry's only full walk: drops every bucket whose map instance no longer exists (`instanceAlive` is
        // the caller's map-manager lookup) and every entry older than the expiry, so a bucket nobody holds any more
        // cannot outlive its fight; a bucket with one fresh row stays, without them. The caller throttles this.
        template <typename InstanceAlive>
        void SweepAbandoned(std::uint32_t nowMs, InstanceAlive instanceAlive)
        {
            for (std::map<InstanceKey, InstanceBucket>::iterator instance = buckets.begin(); instance != buckets.end();)
            {
                const bool gone = !instanceAlive(instance->first.first, instance->first.second);
                InstanceBucket& bucket = instance->second;
                for (std::map<std::uint32_t, EngagedBossNote>::iterator row = bucket.begin(); row != bucket.end();)
                {
                    if (gone || static_cast<std::uint32_t>(nowMs - row->second.lastSeenMs) > kExpireMs)
                    {
                        row = bucket.erase(row);
                        --size;
                    }
                    else
                        ++row;
                }
                if (bucket.empty())
                    instance = buckets.erase(instance);
                else
                    ++instance;
            }
        }
    };
}
