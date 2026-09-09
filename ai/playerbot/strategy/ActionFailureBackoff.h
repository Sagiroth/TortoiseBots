#pragma once

// Bounded failure backoff for autonomous background work (issue #84).
//
// Pure-standard-header policy math, deliberately free of core types so the
// transition/backoff behavior stays unit-testable without a server build:
// the Engine translates game objects to plain ids at the boundary.
//
// Semantics (ported from the Shyalya runtime reference, adapted to the
// module-owned engine):
// - One entry per (action, source, target, destination, result). Repeated
//   failures back off exponentially: base, 2x, 4x, 8x, 16x, capped at max.
// - The cache is bounded twice: entries expire after a TTL, and overflow
//   evicts the stalest entry first. All times are caller milliseconds; the
//   Engine passes WorldTimer::getMSTime().
// - Success clears the entry; map changes and explicit commands clear
//   through Clear()/ClearAll() at the Engine layer.

#include <cstdint>
#include <map>
#include <string>

namespace ai
{
    struct ActionFailureState
    {
        uint32_t failures = 0;
        uint32_t retryAfterMs = 0;
        uint32_t lastFailureMs = 0;
    };

    class ActionFailureBackoff
    {
    public:
        static std::string Key(const std::string& action, const std::string& source,
            uint64_t targetGuid, uint64_t destGuid, uint32_t result)
        {
            return action + '|' + source + '|' + std::to_string(targetGuid) +
                '|' + std::to_string(destGuid) + '|' + std::to_string(result);
        }

        bool IsBackedOff(const std::string& key, uint32_t nowMs) const
        {
            if (failures.empty())
                return false;
            std::map<std::string, ActionFailureState>::const_iterator existing = failures.find(key);
            if (existing == failures.end())
                return false;
            return static_cast<int32_t>(existing->second.retryAfterMs - nowMs) > 0;
        }

        // Record one failure. Zero base/max disables (and empties) the cache.
        void Record(const std::string& key, uint32_t nowMs, uint32_t baseMs, uint32_t maxMs,
            uint32_t maxEntries, uint32_t ttlMs)
        {
            if (!baseMs || !maxMs)
            {
                ClearAll();
                return;
            }
            if (maxEntries < 1)
                maxEntries = 1;
            Prune(nowMs, ttlMs, failures.size() >= maxEntries);

            std::map<std::string, ActionFailureState>::iterator existing = failures.find(key);
            if (existing == failures.end())
            {
                if (failures.size() >= maxEntries)
                    EvictOldest(nowMs);
                existing = failures.emplace(key, ActionFailureState()).first;
            }

            ActionFailureState& state = existing->second;
            state.failures = state.failures + 1 > 16 ? 16 : state.failures + 1;
            uint32_t shift = state.failures - 1 > 4 ? 4 : state.failures - 1;
            uint64_t delay = static_cast<uint64_t>(baseMs) << shift;
            state.retryAfterMs = nowMs + static_cast<uint32_t>(delay > maxMs ? maxMs : delay);
            state.lastFailureMs = nowMs;
        }

        // Drop the entries for both result variants of one action event.
        void Clear(const std::string& first, const std::string& second)
        {
            failures.erase(first);
            failures.erase(second);
        }

        void ClearAll()
        {
            failures.clear();
            lastPruneMs = 0;
        }

        size_t Size() const { return failures.size(); }

        void Prune(uint32_t nowMs, uint32_t ttlMs, bool enforceLimit = false)
        {
            if (failures.empty())
                return;
            uint32_t interval = ttlMs < 5000 ? ttlMs : 5000;
            if (!enforceLimit && lastPruneMs &&
                nowMs - lastPruneMs < interval)
                return;
            lastPruneMs = nowMs;
            for (std::map<std::string, ActionFailureState>::iterator it = failures.begin(); it != failures.end();)
            {
                if (nowMs - it->second.lastFailureMs < ttlMs)
                {
                    ++it;
                    continue;
                }
                it = failures.erase(it);
            }
        }

    private:
        void EvictOldest(uint32_t nowMs)
        {
            std::map<std::string, ActionFailureState>::iterator oldest = failures.end();
            uint32_t oldestAge = 0;
            for (std::map<std::string, ActionFailureState>::iterator it = failures.begin(); it != failures.end(); ++it)
            {
                uint32_t age = nowMs - it->second.lastFailureMs;
                if (oldest == failures.end() || age > oldestAge)
                {
                    oldest = it;
                    oldestAge = age;
                }
            }
            if (oldest != failures.end())
                failures.erase(oldest);
        }

        std::map<std::string, ActionFailureState> failures;
        uint32_t lastPruneMs = 0;
    };

    // Module-owned transition tracking (issue #84, P2). The donor keys this
    // off core generation counters absent from Penqle core, so the Engine
    // derives the same signal from map id + position continuity instead:
    // a bot that was unreachable and is back, on a new map, or jumped an
    // impossible distance between ticks has transitioned, and queued work
    // from before is stale. Walking across zone lines moves continuously
    // and never trips the jump detector.
    class TransitionTracker
    {
    public:
        enum Event { NONE, AWAY, ARRIVED, MAP_CHANGED, JUMPED };
        static float constexpr JumpThresholdYd = 100.0f;

        Event Update(bool inWorld, bool teleporting, uint32_t mapId, float x, float y, float z)
        {
            if (!inWorld || teleporting)
            {
                wasAway = true;
                return AWAY;
            }
            if (!initialized)
            {
                Snapshot(mapId, x, y, z);
                initialized = true;
                wasAway = false;
                return NONE;
            }
            if (wasAway)
            {
                Snapshot(mapId, x, y, z);
                wasAway = false;
                return ARRIVED;
            }
            if (mapId != lastMap)
            {
                Snapshot(mapId, x, y, z);
                return MAP_CHANGED;
            }
            float dx = x - lastX, dy = y - lastY;
            if (dx * dx + dy * dy > JumpThresholdYd * JumpThresholdYd)
            {
                Snapshot(mapId, x, y, z);
                return JUMPED;
            }
            Snapshot(mapId, x, y, z);
            return NONE;
        }

        uint32_t LastMap() const { return lastMap; }

    private:
        void Snapshot(uint32_t mapId, float x, float y, float z)
        {
            lastMap = mapId; lastX = x; lastY = y; lastZ = z;
        }

        bool initialized = false;
        bool wasAway = false;
        uint32_t lastMap = 0;
        float lastX = 0.0f, lastY = 0.0f, lastZ = 0.0f;
    };
}
