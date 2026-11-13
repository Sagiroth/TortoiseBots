// Standalone check for ai/playerbot/SeekFightStatePolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_seek_fight_state_policy.cpp -o /tmp/test_seek_fight_state_policy
//   /tmp/test_seek_fight_state_policy
#include "../ai/playerbot/SeekAuraPolicy.h"
#include "../ai/playerbot/SeekFightStatePolicy.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

namespace
{
    constexpr std::uint32_t kMap = 309;      // Zul'Gurub
    constexpr std::uint32_t kBoss = 14834;   // Hakkar
    constexpr std::uint32_t kSon = 11357;    // Son of Hakkar
    constexpr std::uint32_t kPoison = 24321; // Poisonous Blood
    constexpr std::uint64_t kHakkarGuid = 0x0000000000F00001ull;
    constexpr std::uint32_t kInstance = 7;

    BossTacticRow HakkarRow(float cycleSec, float windowSec, std::uint32_t cycleSpell = kPoison)
    {
        BossTacticRow row;
        row.map = kMap;
        row.bossEntry = kBoss;
        row.kind = BossTacticKind::SeekAura;
        row.targetEntry = kSon;
        row.spellId = kPoison;
        row.priority = 30;
        row.param = 5.0f;
        row.cycleSpell = cycleSpell;
        row.cycleSec = cycleSec;
        row.windowSec = windowSec;
        row.spotX = -11763.0f;
        row.spotY = -1600.0f;
        row.spotZ = 30.0f;
        return row;
    }

    std::shared_ptr<BossTacticTable> TableWith(const BossTacticRow& row)
    {
        std::shared_ptr<BossTacticTable> table = std::make_shared<BossTacticTable>();
        CHECK(table->Add(row));
        table->Seal();
        return table;
    }

    const BossTacticRow& FirstRow(const BossTacticTable& table)
    {
        const std::vector<BossTacticRow>* rows = table.Rows(kMap, kBoss);
        CHECK(rows && !rows->empty());
        return rows->front();
    }

    SeekCycleRecord MidCycleRecord(const BossTacticRow& row)
    {
        SeekCycleRecord record;
        record.engageMs = 1000;    // t0
        record.lastCycleMs = 41000; // t1: one accepted siphon
        record.lastSeenMs = 50000;
        record.cycleNo = 1;
        record.cycleMs = SeekSecondsToMs(row.cycleSec);
        record.leadMs = SeekSecondsToMs(row.windowSec);
        record.cycleSpell = row.cycleSpell;
        record.gatherSourceGuid = 0x0000000000F00002ull;
        record.gatherStartMs = 45000;
        record.ready = true;
        record.missing = 0;
        record.siphonCycle = 1;
        record.siphonDosed = 12;
        record.siphonUndosed = 3;
        return record;
    }
}

int main()
{
    std::cout << "Starting TortoiseBots seek fight state policy tests...\n";

    // A reload between two siphons keeps the live record: the row is still there with the same cycle spell, cycle and
    // window, so the engage, the last siphon, the cycle count, the gather latch and the dose window come through
    // untouched. The snapshot the run leader reads is the one it had before the reload.
    {
        std::shared_ptr<BossTacticTable> oldTable = TableWith(HakkarRow(90.0f, 80.0f));
        SeekRecordMap records;
        SeekRecordKey const key = SeekRecordKeyOf(kInstance, kHakkarGuid, FirstRow(*oldTable));
        SeekCycleRecord const midCycle = MidCycleRecord(FirstRow(*oldTable));
        records[key] = midCycle;

        std::shared_ptr<BossTacticTable> reloaded = TableWith(HakkarRow(90.0f, 80.0f));
        ReconcileSeekRecords(records, *reloaded);
        CHECK(records.size() == 1);
        SeekCycleRecord const& kept = records[key];
        CHECK(kept.engageMs == 1000);
        CHECK(kept.lastCycleMs == 41000);
        CHECK(kept.cycleNo == 1);
        CHECK(kept.cycleMs == 90000);
        CHECK(kept.leadMs == 80000);
        CHECK(kept.cycleSpell == kPoison);
        CHECK(kept.gatherSourceGuid == 0x0000000000F00002ull);
        CHECK(kept.gatherStartMs == 45000);
        CHECK(kept.ready);
        CHECK(kept.siphonCycle == 1 && kept.siphonDosed == 12 && kept.siphonUndosed == 3);

        std::uint32_t const due = SeekCycleDueMs(kept.engageMs, kept.lastCycleMs, kept.cycleMs);
        CHECK(due == 131000);
        CHECK(SeekWindowOpen(52000, due, kept.leadMs));
        CHECK(!SeekWindowOpen(30000, due, kept.leadMs)); // before the window: not a reload's effect, the fight's

        // The old table and the reloaded one can go: the record kept no row pointer.
        oldTable.reset();
        reloaded.reset();
        CHECK(records[key].engageMs == 1000 && records[key].lastCycleMs == 41000);
    }

    // The row's cycle changed: the old clock cannot time the new row, so the record is dropped.
    {
        std::shared_ptr<BossTacticTable> oldTable = TableWith(HakkarRow(90.0f, 80.0f));
        SeekRecordMap records;
        SeekRecordKey const key = SeekRecordKeyOf(kInstance, kHakkarGuid, FirstRow(*oldTable));
        records[key] = MidCycleRecord(FirstRow(*oldTable));

        ReconcileSeekRecords(records, *TableWith(HakkarRow(60.0f, 50.0f)));
        CHECK(records.empty());
    }

    // The window changed: same identity, but the dose window the record holds is not the new row's, so it drops.
    {
        std::shared_ptr<BossTacticTable> oldTable = TableWith(HakkarRow(90.0f, 80.0f));
        SeekRecordMap records;
        SeekRecordKey const key = SeekRecordKeyOf(kInstance, kHakkarGuid, FirstRow(*oldTable));
        records[key] = MidCycleRecord(FirstRow(*oldTable));

        ReconcileSeekRecords(records, *TableWith(HakkarRow(90.0f, 70.0f)));
        CHECK(records.empty());
    }

    // The cycle spell changed: the aura that restarts the clock is another, so it drops too.
    {
        std::shared_ptr<BossTacticTable> oldTable = TableWith(HakkarRow(90.0f, 80.0f));
        SeekRecordMap records;
        SeekRecordKey const key = SeekRecordKeyOf(kInstance, kHakkarGuid, FirstRow(*oldTable));
        records[key] = MidCycleRecord(FirstRow(*oldTable));

        ReconcileSeekRecords(records, *TableWith(HakkarRow(90.0f, 80.0f, 24399)));
        CHECK(records.empty());
    }

    // The row is gone from the new table (another source, or a map without the row): dropped, and other fights' records
    // stay.
    {
        std::shared_ptr<BossTacticTable> oldTable = TableWith(HakkarRow(90.0f, 80.0f));
        SeekRecordMap records;
        SeekRecordKey const hakkarKey = SeekRecordKeyOf(kInstance, kHakkarGuid, FirstRow(*oldTable));
        records[hakkarKey] = MidCycleRecord(FirstRow(*oldTable));

        BossTacticRow other = HakkarRow(90.0f, 80.0f);
        other.bossEntry = 14835;
        other.targetEntry = 11358;
        SeekRecordKey const otherKey = SeekRecordKeyOf(kInstance, kHakkarGuid + 1, other);
        records[otherKey] = MidCycleRecord(other);

        ReconcileSeekRecords(records, *TableWith(other));
        CHECK(records.size() == 1);
        CHECK(records.count(hakkarKey) == 0);
        CHECK(records.count(otherKey) == 1);
    }

    // Two instances fighting the same row keep their own records: the key carries the instance and the boss guid as
    // well as the row identity, and a reload reconciles each one against the same (unchanged) row.
    {
        std::shared_ptr<BossTacticTable> table = TableWith(HakkarRow(90.0f, 80.0f));
        SeekRecordMap records;
        SeekRecordKey const first = SeekRecordKeyOf(kInstance, kHakkarGuid, FirstRow(*table));
        SeekRecordKey const second = SeekRecordKeyOf(kInstance + 1, kHakkarGuid, FirstRow(*table));
        records[first] = MidCycleRecord(FirstRow(*table));
        records[second] = MidCycleRecord(FirstRow(*table));
        records[second].engageMs = 2000;
        ReconcileSeekRecords(records, *table);
        CHECK(records.size() == 2);
        CHECK(records[first].engageMs == 1000);
        CHECK(records[second].engageMs == 2000);
    }

    // SeekSiphonAtLabelShowsRealElapsed: the note's clock is the real time since the cycle's first 24324 sighting,
    // not the nominal +1 s / +3 s of the read.
    CHECK(SeekSiphonAtLabel(1012) == "at=+1012ms");
    CHECK(SeekSiphonAtLabel(3004) == "at=+3004ms");

    // SeekSiphonSecondReadKept: the +3 s read lands beside the +1 s one; the record's siphonDosed/siphonUndosed (the
    // run line's) are never overwritten by it. The +9 s read (the end of the 8 s heal) only logs: it replaces neither.
    {
        SeekCycleRecord record;
        SeekStoreSiphonRead(record, 1, 12, 3);
        SeekStoreSiphonRead(record, 3, 14, 1);
        CHECK(record.siphonDosed == 12 && record.siphonUndosed == 3);
        CHECK(record.siphonLateDosed == 14 && record.siphonLateUndosed == 1);
        SeekStoreSiphonRead(record, 9, 0, 0);
        CHECK(record.siphonDosed == 12 && record.siphonUndosed == 3);
        CHECK(record.siphonLateDosed == 14 && record.siphonLateUndosed == 1);
    }

    // SeekSiphonAtLabel at the third read: the label carries the real elapsed time of the +9 s read.
    CHECK(SeekSiphonAtLabel(9007) == "at=+9007ms");

    // SeekHealerClockTick: a read only sees the Sons inside its own reach, so a Son it did not list says nothing. The
    // clock of a Son on a healer reaches its take through reads of bots that did not see it in between (the old code
    // erased every guid the current read lacked, so the clock never filled with 20 bots reading); only a Son seen dead,
    // out of combat or on a tank leaves, and an entry nobody has seen for kSeekHealerStaleMs is swept.
    {
        std::map<std::uint64_t, SeekHeldSon> held;
        const SeekSonSeen onHealer{7, true, true, false, true};
        SeekHealerClockTick(held, {onHealer}, 1000);
        CHECK(held.size() == 1 && held[7].sinceMs == 1000);
        // Another bot's read, out of reach of Son 7 (it lists nothing of it, or only another Son): the entry stays.
        SeekHealerClockTick(held, {}, 3000);
        CHECK(held.count(7) == 1);
        SeekHealerClockTick(held, {SeekSonSeen{9, true, true, false, false}}, 5000);
        CHECK(held.count(7) == 1);
        // A bot that sees it again refreshes seenMs and keeps sinceMs, so the take clock runs from the first read.
        SeekHealerClockTick(held, {onHealer}, 7000);
        CHECK(held[7].sinceMs == 1000 && held[7].seenMs == 7000);
        CHECK(SeekHealerTakeGuid(held, 3999) == 0);
        CHECK(SeekHealerTakeGuid(held, 4000) == 7);
        // The non-tank time the hold reads (0 for a Son not on a non-tank).
        CHECK(SeekNonTankHeldMs(held, 7, 9000) == 8000);
        CHECK(SeekNonTankHeldMs(held, 8, 9000) == 0);
        // Seen on a tank, dead or out of combat: gone.
        SeekHealerClockTick(held, {SeekSonSeen{7, true, true, true, false}}, 9500);
        CHECK(held.empty());
        SeekHealerClockTick(held, {onHealer}, 10000);
        SeekHealerClockTick(held, {SeekSonSeen{7, false, true, false, true}}, 10500);
        CHECK(held.empty());
        SeekHealerClockTick(held, {onHealer}, 11000);
        SeekHealerClockTick(held, {SeekSonSeen{7, true, false, false, true}}, 11500);
        CHECK(held.empty());
        // A read that SEES the Son off a non-tank (no victim, a pet) erases its clock: the victim changed. Only a read
        // that does not see it at all (out of the bot's range) keeps the entry; the sweep drops what nobody sees.
        SeekHealerClockTick(held, {onHealer}, 20000);
        SeekHealerClockTick(held, {SeekSonSeen{7, true, true, false, false}}, 30000);
        CHECK(held.empty());
        SeekHealerClockTick(held, {onHealer}, 20000);
        SeekHealerClockTick(held, {}, 25000);
        CHECK(held.count(7) == 1);
        SeekHealerClockTick(held, {}, 20000 + kSeekHealerStaleMs + 1);
        CHECK(held.empty());
        // Codex: 6 s on a healer, then seen on a tank, then back on a healer: the release counts from the new
        // start, never from the first one.
        SeekHealerClockTick(held, {onHealer}, 100000);
        SeekHealerClockTick(held, {SeekSonSeen{7, true, true, true, false}}, 106000);
        CHECK(held.empty());
        CHECK(SeekNonTankHeldMs(held, 7, 112000) == 0);
        SeekHealerClockTick(held, {onHealer}, 110000);
        CHECK(SeekNonTankHeldMs(held, 7, 112000) == 2000);
        // The lowest guid among the due entries is the take.
        SeekHealerClockTick(held, {SeekSonSeen{12, true, true, false, true}, SeekSonSeen{5, true, true, false, true}}, 40000);
        CHECK(SeekHealerTakeGuid(held, 48000) == 5);
    }

    // SeekHoldClockTick, interleaved reads: the hold record is fight-wide, so a read that does not tick this Son must
    // not erase or restart it. Bot A ticks Son X every second while bot B, which selected another Son (or none), reads
    // X as alive and not held every half second. B's reads only refresh the seen stamp and X reaches the 20 s cap.
    // A read that erased or restarted a below-cap record here (the old per-read rule) leaves X at 0 s.
    {
        std::map<std::uint64_t, SeekHeldSon> held;
        for (std::uint32_t nowMs = 1000; nowMs <= 21000; nowMs += 500)
        {
            const bool aTicks = nowMs % 1000 == 0 && SeekHeldMs(held, 7, nowMs) < kSeekHoldMaxMs;
            SeekHoldClockTick(held, {SeekHoldSeen{7, true, aTicks}}, nowMs);
            CHECK(held.count(7) == 1);
        }
        CHECK(held[7].sinceMs == 1000);
        CHECK(SeekHeldMs(held, 7, 21000) == 20000);
        // Past the cap nothing ticks any more, but the Son is alive and seen: the record stays and still releases.
        SeekHoldClockTick(held, {SeekHoldSeen{7, true, false}}, 22000);
        CHECK(held.count(7) == 1);
        CHECK(SeekHeldMs(held, 7, 22000) == 21000);
        CHECK(!SeekHoldDecision(false, true, true, false, 90.0f, 30.0f, 5.0f, 5, 0, SeekHeldMs(held, 7, 22000)));
    }

    // A below-cap record no read ticked for kSeekHoldUntickedMs is dropped (the fight stopped holding the Son), and
    // the next tick starts a clean clock. With no record the idle Son is held by the rule again, not released.
    {
        std::map<std::uint64_t, SeekHeldSon> held;
        SeekHoldClockTick(held, {SeekHoldSeen{7, true, true}}, 1000);
        SeekHoldClockTick(held, {}, 1000 + kSeekHoldUntickedMs);
        CHECK(held.count(7) == 1);
        SeekHoldClockTick(held, {}, 1000 + kSeekHoldUntickedMs + 1);
        CHECK(held.empty());
        CHECK(SeekHoldDecision(false, true, true, false, 90.0f, 30.0f, 5.0f, 5, 0,
                               SeekHeldMs(held, 7, 1000 + kSeekHoldUntickedMs + 1)));
        SeekHoldClockTick(held, {SeekHoldSeen{7, true, true}}, 7000);
        CHECK(held[7].sinceMs == 7000);
    }

    // SeekHoldClockCountsEngagedTimeOnly: only a Son fighting a victim feeds the cap, so an idle spare standing at its
    // spawn never enters the clock and the 20 s release cannot empty the reservoir the area veto protects. The cap
    // reaches the selected source and a Son on a tank inside the window; a Son on a non-tank is the healer clock's.
    {
        CHECK(!SeekHoldClockTicks(false, false, false, true));  // a spare at its spawn: out of combat
        CHECK(!SeekHoldClockTicks(false, false, true, true));   // no victim
        CHECK(!SeekHoldClockTicks(false, true, false, true));   // engaged on a non-tank: never the hold's cap
        CHECK(!SeekHoldClockTicks(true, false, false, false));  // the selected source still needs a victim
        CHECK(SeekHoldClockTicks(true, true, false, false));    // the selected source off-window still counts
        CHECK(!SeekHoldClockTicks(false, true, true, false));   // a Son on a tank waits out a shut window
        CHECK(SeekHoldClockTicks(false, true, true, true));     // ... and counts inside it
    }
    {
        // An idle unselected Son read once a second for 25 s stays held: no read ticks it (an idle spare never gets a
        // record), so the cap has nothing to release. Dropping the `ticks` term in the helper turns this red.
        std::map<std::uint64_t, SeekHeldSon> held;
        std::uint32_t nowMs = 1000;
        for (; nowMs <= 25000; nowMs += 1000)
        {
            const bool ticks = SeekSonTicks(true, SeekHoldClockTicks(false, false, false, true), true);
            SeekHoldClockTick(held, {SeekHoldSeen{9, true, ticks}}, nowMs);
        }
        CHECK(held.empty());
        CHECK(SeekHoldDecision(false, true, true, false, 90.0f, 30.0f, 5.0f, 5,
                               SeekNonTankHeldMs({}, 9, nowMs), SeekHeldMs(held, 9, nowMs)));
    }
    {
        // An engaged Son on a tank inside the window is released at 20 s and the release sticks.
        std::map<std::uint64_t, SeekHeldSon> held;
        for (std::uint32_t nowMs = 1000; nowMs <= 21000; nowMs += 1000)
        {
            const bool holds = SeekHoldDecision(false, true, true, true, 90.0f, 30.0f, 5.0f, 5, 0,
                                                SeekHeldMs(held, 9, nowMs));
            const bool ticks = SeekSonTicks(true, SeekHoldClockTicks(false, true, true, true), holds);
            SeekHoldClockTick(held, {SeekHoldSeen{9, true, ticks}}, nowMs);
            if (nowMs == 20000)
                CHECK(SeekHoldDecision(false, true, true, false, 90.0f, 30.0f, 5.0f, 5, 0, SeekHeldMs(held, 9, nowMs)));
            if (nowMs == 21000)
                CHECK(!SeekHoldDecision(false, true, true, false, 90.0f, 30.0f, 5.0f, 5, 0, SeekHeldMs(held, 9, nowMs)));
        }
    }
    {
        // Once capped, a Son on a tank stays released when the window shuts: alive and seen, its record is kept even
        // though this read does not tick, so the close cannot start a fresh 20 s hold. It is swept after
        // kSeekHoldStaleMs unseen, and a Son seen dead is erased at once.
        std::map<std::uint64_t, SeekHeldSon> held;
        SeekHoldClockTick(held, {SeekHoldSeen{9, true, true}}, 1000);
        for (std::uint32_t nowMs = 2000; nowMs <= 21000; nowMs += 1000)
        {
            const bool holds = SeekHoldDecision(false, true, true, true, 90.0f, 30.0f, 5.0f, 5, 0,
                                                SeekHeldMs(held, 9, nowMs));
            const bool ticks = SeekSonTicks(true, SeekHoldClockTicks(false, true, true, true), holds);
            SeekHoldClockTick(held, {SeekHoldSeen{9, true, ticks}}, nowMs);
        }
        CHECK(SeekHeldMs(held, 9, 21000) == 20000);
        // The window shuts: SeekHoldClockTicks is false now, the capped record survives and still releases.
        SeekHoldClockTick(held, {SeekHoldSeen{9, true, false}}, 22000);
        CHECK(held.count(9) == 1);
        CHECK(SeekHeldMs(held, 9, 22000) == 21000);
        CHECK(!SeekHoldDecision(false, false, false, false, 90.0f, 30.0f, 5.0f, 5, 0, SeekHeldMs(held, 9, 22000)));
        // Alive and seen once past the cap keeps it; kSeekHoldStaleMs with no read at all sweeps it.
        SeekHoldClockTick(held, {}, 22000 + kSeekHoldStaleMs);
        CHECK(held.count(9) == 1);
        SeekHoldClockTick(held, {}, 22000 + kSeekHoldStaleMs + 1);
        CHECK(held.empty());
        // A Son seen dead is erased before either sweep.
        SeekHoldClockTick(held, {SeekHoldSeen{9, true, true}}, 50000);
        SeekHoldClockTick(held, {SeekHoldSeen{9, false, false}}, 50001);
        CHECK(held.empty());
    }

    // SeekHoldClockOutOfOrder: a bot keeps its nowMs sample while waiting for the lock, so samples arrive out of
    // order. An age against a later stamp reads 0 (no unsigned wrap), an older sample never moves a stamp back, and
    // neither sweep drops the record.
    {
        std::map<std::uint64_t, SeekHeldSon> held;
        SeekHoldClockTick(held, {SeekHoldSeen{7, true, true}}, 10000);
        SeekHoldClockTick(held, {SeekHoldSeen{7, true, false}}, 5000);
        CHECK(held.count(7) == 1);
        CHECK(held[7].sinceMs == 10000);
        CHECK(held[7].tickedMs == 10000);
        CHECK(held[7].seenMs == 10000);
        CHECK(SeekHeldMs(held, 7, 5000) == 0);
        SeekHoldClockTick(held, {}, 0);
        CHECK(held.count(7) == 1);
        CHECK(SeekHeldMs(held, 7, 0) == 0);
    }

    // SeekHoldClockDropsWhatNobodySees: an entry no read ticked for the grace is dropped, so the map cannot grow with
    // Sons that left every bot's reach or died out of it, and a Son respawning under the same guid starts a clean hold
    // instead of inheriting the old release.
    {
        std::map<std::uint64_t, SeekHeldSon> held;
        SeekHoldClockTick(held, {SeekHoldSeen{11, true, true}}, 1000);
        SeekHoldClockTick(held, {}, 1000 + kSeekHoldUntickedMs);
        CHECK(held.count(11) == 1);
        SeekHoldClockTick(held, {}, 1000 + kSeekHoldUntickedMs + 1);
        CHECK(held.empty());
        SeekHoldClockTick(held, {SeekHoldSeen{11, true, true}}, 30000);
        CHECK(held.size() == 1 && held[11].sinceMs == 30000);
    }

    // SeekHoldEvents: the clock reports each transition. A start carries the guid and a 0 age; a below-cap record no read
    // ticked for kSeekHoldUntickedMs ends as Unticked with the record's age at the erase.
    {
        std::map<std::uint64_t, SeekHeldSon> held;
        std::vector<SeekHoldEvent> events;
        SeekHoldClockTick(held, {SeekHoldSeen{7, true, true}}, 1000, &events);
        CHECK(events.size() == 1 && events[0].guid == 7 && events[0].start && events[0].heldMs == 0);
        events.clear();
        SeekHoldClockTick(held, {}, 1000 + kSeekHoldUntickedMs + 100, &events);
        CHECK(events.size() == 1 && !events[0].start && events[0].reason == SeekHoldEndReason::Unticked);
        CHECK(events[0].heldMs == kSeekHoldUntickedMs + 100);
        CHECK(held.empty());
    }

    // A Son seen dead ends as Died with its age at the read.
    {
        std::map<std::uint64_t, SeekHeldSon> held;
        std::vector<SeekHoldEvent> events;
        SeekHoldClockTick(held, {SeekHoldSeen{9, true, true}}, 1000, &events);
        events.clear();
        SeekHoldClockTick(held, {SeekHoldSeen{9, false, false}}, 3000, &events);
        CHECK(events.size() == 1 && !events[0].start && events[0].reason == SeekHoldEndReason::Died);
        CHECK(events[0].heldMs == 2000);
        CHECK(held.empty());
    }

    // A capped record no read saw alive for kSeekHoldStaleMs ends as Unseen; the unticked sweep must not take it first
    // (a capped entry sweeps on the seen stamp, not the tick stamp).
    {
        std::map<std::uint64_t, SeekHeldSon> held;
        std::vector<SeekHoldEvent> events;
        for (std::uint32_t nowMs = 1000; nowMs <= 21000; nowMs += 1000)
            SeekHoldClockTick(held, {SeekHoldSeen{11, true, true}}, nowMs, &events);
        CHECK(events.size() == 1 && events[0].start);
        events.clear();
        SeekHoldClockTick(held, {}, 21000 + kSeekHoldStaleMs + 100, &events);
        CHECK(events.size() == 1 && !events[0].start && events[0].reason == SeekHoldEndReason::Unseen);
        CHECK(events[0].heldMs == kSeekHoldStaleMs + 100 + 20000);
        CHECK(held.empty());
    }

    // A null collector changes nothing: the map takes the same path, and the same events would have been reported.
    {
        std::map<std::uint64_t, SeekHeldSon> with;
        std::map<std::uint64_t, SeekHeldSon> without;
        std::vector<SeekHoldEvent> events;
        SeekHoldClockTick(with, {SeekHoldSeen{3, true, true}}, 1000, &events);
        SeekHoldClockTick(without, {SeekHoldSeen{3, true, true}}, 1000);
        CHECK(events.size() == 1);
        SeekHoldClockTick(with, {SeekHoldSeen{3, false, false}}, 4000, &events);
        SeekHoldClockTick(without, {SeekHoldSeen{3, false, false}}, 4000);
        CHECK(with.empty() && without.empty());
        CHECK(events.size() == 2 && events[1].reason == SeekHoldEndReason::Died);
        SeekHoldClockTick(with, {SeekHoldSeen{5, true, true}}, 10000, &events);
        SeekHoldClockTick(without, {SeekHoldSeen{5, true, true}}, 10000, nullptr);
        SeekHoldClockTick(with, {}, 10000 + kSeekHoldUntickedMs + 1, &events);
        SeekHoldClockTick(without, {}, 10000 + kSeekHoldUntickedMs + 1, nullptr);
        CHECK(with.empty() && without.empty());
        CHECK(events.size() == 4 && events[3].reason == SeekHoldEndReason::Unticked);
    }

    // SeekShouldLogRelease: only a Son the fight still holds a record for can be released, and one row per hold record
    // (the first read that sees the release logs it). A Son never held is not "released after 0.0 s".
    {
        CHECK(!SeekShouldLogRelease(false, false));
        CHECK(!SeekShouldLogRelease(false, true));
        CHECK(SeekShouldLogRelease(true, false));
        CHECK(!SeekShouldLogRelease(true, true));
    }

    // The release latch lives no longer than the record it was logged for. A NonTank release on a healer-only record
    // latches; the healer clock removing that record (with no hold record covering the Son) drops the latch, and the
    // next hold's own release logs again. A latch the hold record still covers survives the healer clock's end, so a
    // hold's release is not logged twice.
    {
        bool latch = false;
        CHECK(SeekShouldLogRelease(true, latch));
        latch = true;
        CHECK(!SeekShouldLogRelease(true, latch));
        const bool healerRecordEnded = true;
        const bool heldByHold = false;
        if (healerRecordEnded && !heldByHold)
            latch = false;
        // The next hold's start drops the latch too (it belongs to the new record).
        latch = false;
        CHECK(SeekShouldLogRelease(true, latch));

        bool holdLatch = true;
        const bool holdCoversIt = true;
        if (healerRecordEnded && !holdCoversIt)
            holdLatch = false;
        CHECK(!SeekShouldLogRelease(true, holdLatch));
    }

    // SeekDeathCandidates: a Son seen alive and missing from this read's living list is looked up once, whether or not
    // a hold record ever existed for it; a Son still alive and a Son already logged are not.
    {
        const std::set<std::uint64_t> seen = {1, 2, 3, 4};
        const std::set<std::uint64_t> live = {1, 3};
        const std::set<std::uint64_t> logged = {2};
        const std::vector<std::uint64_t> candidates = SeekDeathCandidates(seen, live, logged);
        CHECK(candidates.size() == 1 && candidates[0] == 4);
        const std::vector<std::uint64_t> neverHeld = SeekDeathCandidates({9}, {}, {});
        CHECK(neverHeld.size() == 1 && neverHeld[0] == 9);
        CHECK(SeekDeathCandidates({}, {1, 2}, {}).empty());
    }

    // Cause Insanity's edge: a new charmed guid starts its clock and its row is due once, 1.5 s later. A scan that finds
    // nobody charmed forgets the guid, so the same player charmed again after a gap logs a new row.
    {
        SeekCycleRecord record;
        SeekCharmScan(record, 0, 1000);
        CHECK(record.charmGuid == 0 && !SeekCharmRowDue(record, 9000));
        SeekCharmScan(record, 42, 2000);
        CHECK(record.charmGuid == 42 && record.charmSeenMs == 2000 && !record.charmLogged);
        CHECK(!SeekCharmRowDue(record, 3499)); // not before the 1.5 s wait
        CHECK(SeekCharmRowDue(record, 3500));
        CHECK(!SeekCharmRowDue(record, 3600)); // one row per charm
        SeekCharmScan(record, 42, 4000);       // still charmed: the same charm, no new clock
        CHECK(record.charmSeenMs == 2000 && record.charmLogged);
        CHECK(!SeekCharmRowDue(record, 9000));
        SeekCharmScan(record, 0, 5000);        // the gap: nobody charmed
        CHECK(record.charmGuid == 0 && record.charmSeenMs == 0 && !record.charmLogged);
        SeekCharmScan(record, 42, 8000);       // the same player charmed again
        CHECK(record.charmGuid == 42 && record.charmSeenMs == 8000 && !record.charmLogged);
        CHECK(!SeekCharmRowDue(record, 9499));
        CHECK(SeekCharmRowDue(record, 9500));  // a new row
        SeekCharmScan(record, 43, 10000);      // another player charmed with no gap: a new clock too
        CHECK(record.charmGuid == 43 && record.charmSeenMs == 10000 && !record.charmLogged);
    }

    // DungeonRunSeekCycleRoll: a missed sighting rolls the cycle count with the due time. kSeekOverdueGraceMs is 8000.
    CHECK(SeekRolledCycles(9000 + 8000, 9000, 90000) == 0);
    CHECK(SeekRolledCycles(9000 + 8001, 9000, 90000) == 1);
    CHECK(SeekRolledCycles(9000 + 90000 + 8001, 9000, 90000) == 2);
    CHECK(SeekRolledCycles(9000, 0, 90000) == 0);
    CHECK(SeekRolledCycles(9000, 9000, 0) == 0);
    {
        SeekCycleRecord rolled;
        rolled.engageMs = 1000;
        rolled.cycleMs = 90000;
        CHECK(SeekRollMissedCycles(rolled, 1000 + 90000 + 9000) == 1);
        CHECK(rolled.cycleNo == 1);
        CHECK(rolled.lastCycleMs == 91000); // the estimated sighting, not left at 0 nor moved to now
        CHECK(SeekCycleAccepts(rolled.lastCycleMs, 181000, 90000));
        // A due time inside the grace rolls nothing.
        CHECK(SeekRollMissedCycles(rolled, 181000 + 8000) == 0);
        CHECK(rolled.cycleNo == 1 && rolled.lastCycleMs == 91000);
    }
    // A real sighting after the roll: the estimate was a few seconds off, so the clock re-anchors on the sighting and
    // the cycle number the roll counted stays. due = engage 1000 + 90000; roll at due + 8001, sighting at due + 9000.
    {
        SeekCycleRecord late;
        late.engageMs = 1000;
        late.cycleMs = 90000;
        CHECK(SeekRollMissedCycles(late, 91000 + 8001) == 1);
        CHECK(late.cycleNo == 1 && late.lastCycleMs == 91000 && late.lastCycleEstimated);
        CHECK(SeekCycleSightingOf(true, late.lastCycleMs, 100000, late.cycleMs, late.lastCycleEstimated) ==
              SeekCycleSighting::Resync);
        SeekCycleResync(late, 100000);
        CHECK(late.cycleNo == 1); // no second cycle
        CHECK(late.lastCycleMs == 100000);
        CHECK(!late.lastCycleEstimated);
        CHECK(late.siphonAtMs == 101000 && late.siphonSecondAtMs == 103000 && late.siphonThirdAtMs == 109000);
        CHECK(late.siphonSeenMs == 100000 && late.siphonCycle == 1);
        // The raid's second sighting 300 ms later changes nothing.
        CHECK(SeekCycleSightingOf(true, late.lastCycleMs, 100300, late.cycleMs, late.lastCycleEstimated) ==
              SeekCycleSighting::None);
    }
    // Only a sighting of the row's spell on the rolled clock re-syncs; a real next cycle still counts.
    CHECK(SeekCycleSightingOf(false, 91000, 100000, 90000, true) == SeekCycleSighting::None);
    CHECK(SeekCycleSightingOf(true, 91000, 100000, 90000, false) == SeekCycleSighting::None);
    CHECK(SeekCycleSightingOf(true, 91000, 136000, 90000, false) == SeekCycleSighting::Next);

    std::cout << "seek fight state policy: all tests passed\n";
    return 0;
}
