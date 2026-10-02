#pragma once

#include <cstdint>
#include <ctime>
#include <mutex>
#include <unordered_map>

namespace ai
{
    // Process-wide memory of quest-taker spawns the realm's navmesh cannot be
    // walked to (the Tower of Azora's Antonas Riftgaze: the tile has walkable
    // polygons on both sides, but nothing connects the tower base to the NPC's
    // spot, so every bot stalls 18-29 yd away). A navmesh probe is a detour
    // query, so one bot's answer is shared: from then on every other bot settles
    // that taker's hand-in as soon as it is in range, without probing again.
    //
    // The mark expires - a regenerated tile or a moved spawn should be believed
    // again - and is only ever set by a fresh probe, never by a count.
    // Reachable takers are deliberately not remembered: reachability depends on
    // where the bot stands, and the movement layer re-paths every trip anyway.
    //
    // Thread-safe: travel partitions are also built on worker threads.
    class TakerReachabilityCache
    {
    public:
        // Long enough to cover a realm day's play, short enough that a navmesh
        // fix or a content change is believed again without a restart.
        static constexpr std::time_t UNREACHABLE_TTL = 6 * 3600;

        static TakerReachabilityCache& Instance()
        {
            static TakerReachabilityCache instance;
            return instance;
        }

        bool IsUnreachable(std::int32_t entry, std::time_t now) const
        {
            std::lock_guard<std::mutex> lock(mutex);

            auto const it = unreachableUntil.find(entry);
            return it != unreachableUntil.end() && it->second > now;
        }

        void MarkUnreachable(std::int32_t entry, std::time_t now)
        {
            std::lock_guard<std::mutex> lock(mutex);
            unreachableUntil[entry] = now + UNREACHABLE_TTL;
        }

        void Clear(std::int32_t entry)
        {
            std::lock_guard<std::mutex> lock(mutex);
            unreachableUntil.erase(entry);
        }

    private:
        TakerReachabilityCache() = default;

        mutable std::mutex mutex;
        std::unordered_map<std::int32_t, std::time_t> unreachableUntil;
    };
}
