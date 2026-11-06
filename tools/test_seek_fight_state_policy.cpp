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
    // clock of a Son on a healer reaches 8 s through reads of bots that did not see it in between (the old code erased
    // every guid the current read lacked, so the clock never filled with 20 bots reading); only a Son seen dead, out
    // of combat or on a tank leaves, and an entry nobody has seen for kSeekHealerStaleMs is swept.
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
        // A bot that sees it again refreshes seenMs and keeps sinceMs, so the 8 s clock runs from the first read.
        SeekHealerClockTick(held, {onHealer}, 7000);
        CHECK(held[7].sinceMs == 1000 && held[7].seenMs == 7000);
        CHECK(SeekHealerTakeGuid(held, 8999) == 0);
        CHECK(SeekHealerTakeGuid(held, 9000) == 7);
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
        // A Son whose victim is neither (no victim, a pet) keeps its entry; the sweep drops what nobody sees.
        SeekHealerClockTick(held, {onHealer}, 20000);
        SeekHealerClockTick(held, {SeekSonSeen{7, true, true, false, false}}, 30000);
        CHECK(held.count(7) == 1);
        SeekHealerClockTick(held, {}, 20000 + kSeekHealerStaleMs + 1);
        CHECK(held.empty());
        // The lowest guid among the due entries is the take.
        SeekHealerClockTick(held, {SeekSonSeen{12, true, true, false, true}, SeekSonSeen{5, true, true, false, true}}, 40000);
        CHECK(SeekHealerTakeGuid(held, 48000) == 5);
    }

    std::cout << "seek fight state policy: all tests passed\n";
    return 0;
}
