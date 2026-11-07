#pragma once

// The seek_aura fight record (one per map instance, boss and row) and its handoff across `.bot run reloadplans`.
//
// The record's clock and gather latch are shared by every bot of the instance (SeekCycleSnapshot). Its key is the
// row's stable identity, never its address: a reload frees the old table. The record copies every row column it reads,
// so nothing in it may dereference the row it came from.
//
// A reload replaces the tactic table with a freshly loaded one. The records survive it only while their row is still
// there with the same cycle columns (`cycle_spell`, `cycle_s`, `window_s`): then the combat start (t0), the last
// accepted siphon (t1) and the dose window stay untouched, so a reload between two siphons does not restart the
// poison cycle. A record whose row was removed or retimed is dropped (the new row's clock is a new fight).
//
// No core includes: tested in tools/test_seek_fight_state_policy.cpp.

#include "BossTacticsPolicy.h"
#include "SeekAuraPolicy.h"

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace ai
{
    // One Son's entry in the healer-held clock: `sinceMs` is the first read that saw it fighting a non-tank, `seenMs`
    // the last one that did.
    struct SeekHeldSon
    {
        std::uint32_t sinceMs = 0;
        std::uint32_t seenMs = 0;
    };

    // What one bot's read saw of one Son it can reach: alive and fighting, and whether its victim is a player tank or a
    // player non-tank (neither: no victim, or a pet or other non-player).
    struct SeekSonSeen
    {
        std::uint64_t guid = 0;
        bool alive = true;
        bool inCombat = true;
        bool victimTank = false;
        bool victimNonTank = false;
    };

    // An entry nobody has seen on a non-tank for this long is dropped (a Son whose reads stopped: it left every bot's
    // reach, or the raid left). Far above the 3 s take, far below a fight.
    constexpr std::uint32_t kSeekHealerStaleMs = 20000;

    // One Son's hold read: whether this read saw it held (SeekHoldsUnit).
    struct SeekHoldSeen
    {
        std::uint64_t guid = 0;
        bool held = false;
    };

    // A hold entry no read refreshed for this long is dropped. A read only sees the Sons inside its reach (a grid walk
    // around the bot), so one bot not seeing a Son says nothing; the sweep only removes Sons that left every bot's
    // view or died out of it, and a Son respawning under the same guid then starts a clean hold instead of inheriting
    // the old release. One cap long, so a genuinely held Son is never dropped between two reads.
    constexpr std::uint32_t kSeekHoldStaleMs = 20000;

    // The Son-on-a-non-tank clock after one bot's read. Every bot of the fight feeds the one record, and a read only
    // sees the Sons inside ITS reach (a range-limited grid search), so a Son the read did not see says nothing: a Son
    // this read saw anywhere but on a non-tank (dead, out of combat, on a tank, with no victim or on a pet) leaves the
    // clock; one it saw on a non-tank enters it (or refreshes `seenMs`, never `sinceMs`); a Son it did not see stays
    // untouched until the sweep drops what no read has seen on a non-tank for kSeekHealerStaleMs. Erasing every guid
    // the read did not list kept the clock from ever reaching the take, since the bots that did not see the Son erased
    // it between the ones that did.
    inline void SeekHealerClockTick(std::map<std::uint64_t, SeekHeldSon>& held, const std::vector<SeekSonSeen>& seen,
                                    std::uint32_t nowMs)
    {
        for (const SeekSonSeen& son : seen)
        {
            // Seen off a non-tank (dead, out of combat, on a tank, no victim, a pet): the victim changed, so its clock
            // goes. Only a read that does not see the Son at all keeps the entry.
            if (!son.alive || !son.inCombat || !son.victimNonTank)
            {
                held.erase(son.guid);
                continue;
            }
            auto it = held.find(son.guid);
            if (it == held.end())
                held.insert(std::make_pair(son.guid, SeekHeldSon{nowMs, nowMs}));
            else
                it->second.seenMs = nowMs;
        }
        for (auto it = held.begin(); it != held.end();)
            it = nowMs - it->second.seenMs > kSeekHealerStaleMs ? held.erase(it) : std::next(it);
    }

    // The Son the first off-tank takes off a non-tank now: the first (lowest guid) of the clock's entries that has been
    // held long enough, 0 when none.
    inline std::uint64_t SeekHealerTakeGuid(const std::map<std::uint64_t, SeekHeldSon>& held, std::uint32_t nowMs)
    {
        for (const auto& entry : held)
        {
            if (SeekTakeFightingSonOnHealer(nowMs - entry.second.sinceMs))
                return entry.first;
        }
        return 0;
    }

    // How long the Son has fought a non-tank by the clock (0: no entry, it is on a tank or out of the fight). The hold
    // reads it (SeekHoldDecision).
    inline std::uint32_t SeekNonTankHeldMs(const std::map<std::uint64_t, SeekHeldSon>& held, std::uint64_t son,
                                           std::uint32_t nowMs)
    {
        auto const it = held.find(son);
        return it == held.end() ? 0 : nowMs - it->second.sinceMs;
    }

    // The hold clock after one bot's read: a Son seen held enters it (or keeps `sinceMs`), one seen not held leaves.
    // `sinceMs` is the first read that saw it held; SeekHoldDecision releases it at kSeekHoldMaxMs. The read reports a
    // Son already past the cap as held (SeekSonHeld), so the release survives a closing window and is not restarted;
    // a dead or gone Son is reported not held. A bot's read only reaches the Sons around it, so a Son it did not see
    // keeps its entry until the sweep drops what no read refreshed for kSeekHoldStaleMs.
    //
    // Known limits of the shared record (documented, not handled):
    // - The source a read selects is that read's own answer (the shared gather latch when it holds, else the search
    //   around the spot), so a read can select another Son or none where another selected this one. A read that sees
    //   that Son without ticking it reports it not held and drops a below-cap record: the engaged clock restarts (a
    //   longer hold, never a shorter one).
    // - `nowMs` is each bot's own WorldTimer sample and a read keeps its sample while waiting for the lock, so
    //   samples can arrive out of order. An older sample makes `nowMs - sinceMs` and `nowMs - seenMs` unsigned-wrap:
    //   for that read the Son can look capped (released) and an entry can be swept. The record keeps its `sinceMs`, so
    //   the next current sample reads the true elapsed again; a swept entry restarts from the next sighting.
    inline void SeekHoldClockTick(std::map<std::uint64_t, SeekHeldSon>& held, const std::vector<SeekHoldSeen>& seen,
                                  std::uint32_t nowMs)
    {
        for (const SeekHoldSeen& son : seen)
        {
            if (!son.held)
            {
                held.erase(son.guid);
                continue;
            }
            SeekHeldSon& entry = held[son.guid];
            if (entry.sinceMs == 0)
                entry.sinceMs = nowMs;
            entry.seenMs = nowMs;
        }
        for (auto it = held.begin(); it != held.end();)
            it = nowMs - it->second.seenMs > kSeekHoldStaleMs ? held.erase(it) : std::next(it);
    }

    // How long the fight has held this Son (0: no entry, it is not held). The hold reads it (SeekHoldDecision).
    inline std::uint32_t SeekHeldMs(const std::map<std::uint64_t, SeekHeldSon>& held, std::uint64_t son,
                                    std::uint32_t nowMs)
    {
        auto const it = held.find(son);
        return it == held.end() ? 0 : nowMs - it->second.sinceMs;
    }

    struct SeekCycleRecord
    {
        std::uint32_t engageMs = 0;     // the fight's combat start (the boss first seen in combat with the group)
        std::uint32_t lastCycleMs = 0;  // the last accepted cycle spell sighting (0: none)
        std::uint32_t lastSeenMs = 0;
        // The row's own clock, copied at every read: the record outlives the row pointer, and SeekCycleSnapshot reads
        // the due time without a row.
        std::uint32_t cycleMs = 0;
        std::uint32_t leadMs = 0;
        std::uint32_t cycleSpell = 0;
        std::uint64_t gatherSourceGuid = 0;
        std::uint32_t gatherStartMs = 0;
        // The pull's latch: the Son the running pull goes to and when it started. The pickup search reaches 60 yd of
        // B, but the Son patrols from 32 to 112 yd out, so the tank kept dropping a Son it was already walking to.
        std::uint64_t pullTargetGuid = 0;
        std::uint32_t pullStartMs = 0;
        // When each Son of the fight was first seen fighting a non-tank, and when a read last saw it so (guid -> clock).
        // The first off-tank takes such a Son off a healer once the third off-tank cannot, and the healer has had it
        // for a while. See SeekHealerClockTick for who may leave the map.
        std::map<std::uint64_t, SeekHeldSon> healerHeldSince;
        // When each Son of the fight entered the hold: the 20 s cap releases it to the damage dealers (SeekHeldMs).
        // Only fighting time counts and a release sticks while the Son lives (SeekHoldClockTicks, SeekSonHeld).
        std::map<std::uint64_t, SeekHeldSon> holdSince;
        bool ready = false;
        std::uint32_t cycleNo = 0;
        int missing = 0;
        std::uint32_t siphonAtMs = 0;   // the +1 s dose count is due then (0: none pending)
        std::uint32_t siphonSecondAtMs = 0; // the +3 s read, which settles the heal the first one cannot explain
        std::uint32_t siphonThirdAtMs = 0;  // the +9 s read, after the 8 s heal has run its course
        std::uint32_t siphonSeenMs = 0;     // the cycle's first 24324 sighting: the note's clock starts here
        std::uint32_t siphonCycle = 0;
        int siphonDosed = 0;            // the +1 s read, which the run line reports
        int siphonUndosed = 0;
        int siphonLateDosed = 0;        // the +3 s read, kept beside it
        int siphonLateUndosed = 0;
    };

    // Where a siphon read goes: the +1 s read is the cycle's own (the run line reads it), the +3 s read is kept beside
    // it. The second read settles a heal the first cannot explain and must never erase the first; the +9 s read (the
    // end of the 8 s heal) is the log's own and replaces neither.
    inline void SeekStoreSiphonRead(SeekCycleRecord& record, std::uint32_t atSec, int dosed, int undosed)
    {
        if (atSec <= 1)
        {
            record.siphonDosed = dosed;
            record.siphonUndosed = undosed;
        }
        else if (atSec <= 3)
        {
            record.siphonLateDosed = dosed;
            record.siphonLateUndosed = undosed;
        }
    }

    // The siphon note's clock label: the real milliseconds between the cycle's first sighting and the read, not the
    // read's nominal delay. A read lands a few ticks late, and the note is read against the siphon it explains.
    inline std::string SeekSiphonAtLabel(std::uint32_t elapsedMs)
    {
        char label[32];
        snprintf(label, sizeof(label), "at=+%ums", static_cast<unsigned>(elapsedMs));
        return label;
    }

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

    // A record outlives a reload only when its row still exists and its clock columns did not change. The combat start
    // and the last siphon are the record's own: the caller keeps them as they are.
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
