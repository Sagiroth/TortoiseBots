#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

// Pure policy for the sliced travel pick (issue #642): ChooseTravelTargetAction
// synchronously walks every candidate with expensive per-point checks (area
// lookups, hostile-town and fishing spawn scans, route A*, navmesh probe),
// and one 11 s pick landed whole in a single world tick. The pick now runs
// under a per-visit deadline and resumes on the next visit without losing
// work: the scan cursor (partition key + point index) and the per-destination
// active verdicts persist per bot, and a resumed visit fast-forwards through
// already-evaluated candidates using cached verdicts. The candidate order is
// never re-sorted, so the resumed pick accepts the same target as an
// uninterrupted scan - only spread across visits.
//
// The verdict cache is sound because one pick cannot observe a change
// mid-scan (no tick passes inside a visit, values are per-bot and the visit
// owns the thread) and resume visits are seconds apart; a bot that moved
// cells or a refreshed destination list invalidates the scan and restarts it.
// The world-facing parts (AI values, travel target, logEvent) live in
// ChooseTravelTargetAction; the decisions below are pure so they can be
// tested on their own.

namespace ai
{
    // Scan cursor: position in the ordered partition map. Partitions iterate
    // in map order and points in list order on every visit, so replaying from
    // the cursor reproduces the uninterrupted order exactly.
    struct TravelPickCursor
    {
        uint32_t partitionKey = 0;
        size_t pointIndex = 0;
        bool started = false;         // first candidate seen (distance gate done)
        bool distanceChecked = false; // moved-too-far gate evaluated
    };

    // Advance the cursor to the next candidate. Returns false when the scan is
    // exhausted (no more partitions after the current one).
    template <typename PartitionMap>
    inline bool TravelPickAdvance(TravelPickCursor& cursor, PartitionMap const& partitions)
    {
        auto it = partitions.find(cursor.partitionKey);
        if (it == partitions.end())
            return false;
        if (cursor.pointIndex + 1 < it->second.size())
        {
            ++cursor.pointIndex;
            return true;
        }
        auto next = std::next(it);
        if (next == partitions.end())
            return false;
        cursor.partitionKey = next->first;
        cursor.pointIndex = 0;
        return true;
    }

    // Resume validity: the scan restarts (rather than resumes) when the bot
    // left its 200 yd cell or the destination list was refreshed. The list
    // identity is its size plus the first partition key: a refresh rebuilds
    // the map, and two different searches never share both.
    inline bool TravelPickScanValid(uint32_t storedMap, int32_t storedCellX, int32_t storedCellY,
        size_t storedListSize, uint32_t storedFirstPartition,
        uint32_t botMap, int32_t botCellX, int32_t botCellY,
        size_t listSize, uint32_t firstPartition)
    {
        return storedMap == botMap && storedCellX == botCellX && storedCellY == botCellY &&
            storedListSize == listSize && storedFirstPartition == firstPartition;
    }

    inline int32_t TravelPickCellCoord(float pos) { return int32_t(pos / 200.0f); }

    // Deadline: steady-clock microseconds so a 3-5 ms budget is measurable
    // (WorldTimer's ms resolution is too coarse to slice with).
    inline bool TravelPickOverBudget(
        std::chrono::steady_clock::time_point visitStart, uint64_t budgetUs)
    {
        if (!budgetUs)
            return false;
        uint64_t const elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - visitStart).count());
        return elapsed >= budgetUs;
    }

    // Scan outcome for the caller: found a target, exhausted the list with
    // none, or hit the deadline and yielded (resume next visit).
    enum class TravelPickOutcome { Found, NotFound, Yielded };
} // namespace ai
