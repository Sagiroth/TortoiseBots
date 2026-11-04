#pragma once

// The seek_aura fight record (one per map instance, boss and row) and its handoff across `.bot run reloadplans`.
//
// The record's clock and gather latch are shared by every bot of the instance (SeekCycleSnapshot). Its key is the
// row's stable identity, never its address: a reload frees the old table. The record copies every row column it reads,
// so nothing in it may dereference the row it came from.
//
// A reload replaces the tactic table with a freshly loaded one. The records survive it only while their row is still
// there with the same cycle columns (`cycle_spell`, `cycle_s`, `window_s`): then the engage (t0), the last accepted
// siphon (t1) and the dose window stay untouched, so a reload between two siphons does not restart the poison cycle.
// A record whose row was removed or retimed is dropped (the new row's clock is a new fight).
//
// No core includes: tested in tools/test_seek_fight_state_policy.cpp.

#include "BossTacticsPolicy.h"

#include <cstdint>
#include <map>
#include <tuple>

namespace ai
{
    struct SeekCycleRecord
    {
        std::uint32_t engageMs = 0;     // the first sighting of the boss in combat with a victim
        std::uint32_t lastCycleMs = 0;  // the last accepted cycle spell sighting (0: none)
        std::uint32_t lastSeenMs = 0;
        // The row's own clock, copied at every read: the record outlives the row pointer, and SeekCycleSnapshot reads
        // the due time without a row.
        std::uint32_t cycleMs = 0;
        std::uint32_t leadMs = 0;
        std::uint32_t cycleSpell = 0;
        std::uint64_t gatherSourceGuid = 0;
        std::uint32_t gatherStartMs = 0;
        bool ready = false;
        std::uint32_t cycleNo = 0;
        int missing = 0;
        std::uint32_t siphonAtMs = 0;   // the dosed/undosed count is due then (0: none pending)
        std::uint32_t siphonCycle = 0;
        int siphonDosed = 0;
        int siphonUndosed = 0;
    };

    // The stable identity of a row: the table's primary key (map, boss entry, kind, target, spell, band) plus the fight it
    // belongs to (instance, boss guid), never the row's address. The key does not carry the cycle columns: a retimed
    // row has the same identity, and SeekRecordSurvivesReload drops the record instead.
    using SeekRecordKey = std::tuple<std::uint32_t, std::uint64_t, std::uint32_t, std::uint32_t, std::uint8_t,
                                     std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t>;

    inline SeekRecordKey SeekRecordKeyOf(std::uint32_t instanceId, std::uint64_t bossGuid, const BossTacticRow& row)
    {
        return std::make_tuple(instanceId, bossGuid, row.map, row.bossEntry, static_cast<std::uint8_t>(row.kind),
                               row.targetEntry, row.spellId, row.hpMin, row.hpMax);
    }

    using SeekRecordMap = std::map<SeekRecordKey, SeekCycleRecord>;

    // The row's seconds as milliseconds, the same rounding the read uses, so a reload compares the values the record
    // was built from rather than two castings of the same float.
    inline std::uint32_t SeekSecondsToMs(float seconds)
    {
        return static_cast<std::uint32_t>(seconds * 1000.0f + 0.5f);
    }

    // The row of a record's key in a table, or null. The row's primary key is (kind, target, spell, hp band) within
    // (map, boss entry), the same identity the table dedupes on (BossTacticTable::Add).
    inline const BossTacticRow* SeekRowOfRecordKey(const BossTacticTable& table, const SeekRecordKey& key)
    {
        const std::vector<BossTacticRow>* rows = table.Rows(std::get<2>(key), std::get<3>(key));
        if (!rows)
            return nullptr;
        for (const BossTacticRow& row : *rows)
        {
            if (static_cast<std::uint8_t>(row.kind) == std::get<4>(key) && row.targetEntry == std::get<5>(key) &&
                row.spellId == std::get<6>(key) && row.hpMin == std::get<7>(key) && row.hpMax == std::get<8>(key))
                return &row;
        }
        return nullptr;
    }

    // A record outlives a reload only when its row still exists and its clock columns did not change. The engage and
    // the last siphon are the record's own: the caller keeps them as they are.
    inline bool SeekRecordSurvivesReload(const SeekCycleRecord& record, const BossTacticRow& row)
    {
        return record.cycleSpell == row.cycleSpell && record.cycleMs == SeekSecondsToMs(row.cycleSec) &&
            record.leadMs == SeekSecondsToMs(row.windowSec);
    }

    // The reload's handoff, before the new table is installed: keep every record whose row still exists with the same
    // cycle spell, cycle length and window, drop the records of removed or retimed rows.
    inline void ReconcileSeekRecords(SeekRecordMap& records, const BossTacticTable& next)
    {
        for (SeekRecordMap::iterator it = records.begin(); it != records.end();)
        {
            const BossTacticRow* row = SeekRowOfRecordKey(next, it->first);
            if (row && SeekRecordSurvivesReload(it->second, *row))
                ++it;
            else
                it = records.erase(it);
        }
    }
}
