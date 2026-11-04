// Standalone check for ai/playerbot/DungeonPlanReloadPolicy.h (`.bot run reloadplans`):
//   g++ -std=c++17 -Wall -Wextra -Werror tools/test_dungeon_plan_reload_policy.cpp -o /tmp/test_dungeon_plan_reload_policy && /tmp/test_dungeon_plan_reload_policy
// The tables are filled through the same DungeonPlanTable / BossTacticTable calls the loader makes for each row (AddPlan,
// AddLeg, AddStep, AddBoss, Add, Seal), so a refused row here is refused by the same check as on the server.
#include "../ai/playerbot/DungeonPlanReloadPolicy.h"

#include <cstdlib>
#include <iostream>
#include <memory>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

static PlanStep Move(std::uint8_t leg, std::uint16_t seq, float x)
{
    PlanStep step;
    step.leg = leg;
    step.seq = seq;
    step.kind = PlanStepKind::Move;
    step.x = x;
    return step;
}

static PlanStep Boss(std::uint8_t leg, std::uint16_t seq, std::uint32_t entry)
{
    PlanStep step;
    step.leg = leg;
    step.seq = seq;
    step.kind = PlanStepKind::Boss;
    step.entry = entry;
    return step;
}

// Loader stand-in: one step row through AddStep, counted and named as ReadDungeonPlanRows does.
static void StepRow(DungeonPlanTable& table, PlanLoadCounts& counts, const std::string& plan, const PlanStep& step)
{
    PlanStepRefusal const refusal = table.AddStep(plan, step);
    if (refusal != PlanStepRefusal::None)
    {
        ++counts.skippedSteps;
        counts.Note("ai_dungeon_step " + plan + " leg " + std::to_string(step.leg) + " seq " + std::to_string(step.seq) +
            " (" + PlanStepRefusalName(refusal) + ")");
    }
}

// The startup set: bfd (3 steps, a boss) and rfk (2 steps).
static std::shared_ptr<DungeonPlanTable> OldSet()
{
    auto table = std::make_shared<DungeonPlanTable>();
    PlanLoadCounts counts;
    CHECK(table->AddPlan("bfd", 48));
    CHECK(table->AddPlan("rfk", 47));
    StepRow(*table, counts, "bfd", Move(1, 1, 10));
    StepRow(*table, counts, "bfd", Move(1, 2, 20));
    StepRow(*table, counts, "bfd", Boss(1, 3, 4887));
    StepRow(*table, counts, "rfk", Move(1, 1, 5));
    StepRow(*table, counts, "rfk", Move(1, 2, 6));
    CHECK(table->AddBoss(48, 4829));
    table->Seal();
    CHECK(counts.Refused() == 0);
    return table;
}

static std::shared_ptr<BossTacticTable> Tactics(std::uint32_t rows)
{
    auto table = std::make_shared<BossTacticTable>();
    for (std::uint32_t i = 0; i < rows; ++i)
    {
        BossTacticRow row;
        row.map = 48;
        row.bossEntry = 4887;
        row.kind = BossTacticKind::Interrupt;
        row.spellId = 100 + i;
        CHECK(table->Add(row));
    }
    table->Seal();
    return table;
}

// Holders as on the server after startup: the startup tables are the current ones.
struct Server
{
    ReloadableTable<DungeonPlanTable> plans;
    ReloadableTable<BossTacticTable> tactics;
    Server()
    {
        plans.Install(OldSet());
        tactics.Install(Tactics(2));
        plans.Prune([](const DungeonPlanTable&) { return false; });
        tactics.Prune([](const BossTacticTable&) { return false; });
    }
};

static void TestGoodReloadSwaps()
{
    Server server;
    const DungeonPlanTable* before = &server.plans.Current();
    CHECK(server.plans.RetiredCount() == 0);

    auto next = std::make_shared<DungeonPlanTable>();
    PlanLoadCounts counts;
    CHECK(next->AddPlan("bfd", 48));
    CHECK(next->AddPlan("rfk", 47));
    CHECK(next->AddPlan("dm", 36));
    StepRow(*next, counts, "bfd", Move(1, 1, 10));
    StepRow(*next, counts, "bfd", Move(1, 2, 20));
    StepRow(*next, counts, "bfd", Move(1, 3, 25));   // a step added
    StepRow(*next, counts, "bfd", Boss(1, 4, 4887));
    StepRow(*next, counts, "rfk", Move(1, 1, 5));
    StepRow(*next, counts, "rfk", Move(1, 2, 7));     // same count, a moved point
    StepRow(*next, counts, "dm", Move(1, 1, 1));
    CHECK(next->AddBoss(48, 4829));
    CHECK(next->AddBoss(36, 639));
    next->Seal();

    PlanReloadOutcome const out = CommitPlanReload(server.plans, next, counts, server.tactics, Tactics(3), 0, {}, {});
    CHECK(out.swapped);
    CHECK(out.reason.empty());
    CHECK(&server.plans.Current() == next.get());
    CHECK(&server.plans.Current() != before);
    CHECK(server.plans.Current().Find("bfd")->steps.size() == 4);
    CHECK(server.plans.Current().IsBossEntry(36, 639));
    CHECK(server.tactics.Current().Size() == 3);
    CHECK(out.bossesBefore == 2 && out.tacticsBefore == 2);
    // No run held a plan: the old table is freed at once.
    CHECK(out.keptOld == 0 && server.plans.RetiredCount() == 0);
    CHECK(server.tactics.RetiredCount() == 0);
    // The per-plan changes, by name (All() is sorted): bfd count, dm new, rfk rows.
    CHECK(out.changes.size() == 3);
    CHECK(out.changes[0] == "bfd 3 -> 4 steps");
    CHECK(out.changes[1] == "dm new, 1 steps");
    CHECK(out.changes[2] == "rfk 2 steps, rows changed");
    CHECK(PlanLoadSummary(server.plans.Current(), counts) ==
        "Loaded 3 dungeon plans, 7 steps, 3 dungeon bosses (0 plans, 0 steps and 0 boss rows skipped)");
}

static void TestUnchangedReloadListsNothing()
{
    Server server;
    PlanReloadOutcome const out = CommitPlanReload(server.plans, OldSet(), PlanLoadCounts(), server.tactics, Tactics(2), 0, {}, {});
    CHECK(out.swapped);
    CHECK(out.changes.empty());
}

static void TestBadRowKeepsTheOldSet()
{
    Server server;
    const DungeonPlanTable* before = &server.plans.Current();
    const BossTacticTable* tacticsBefore = &server.tactics.Current();

    auto next = std::make_shared<DungeonPlanTable>();
    PlanLoadCounts counts;
    CHECK(next->AddPlan("bfd", 48));
    CHECK(next->AddPlan("rfk", 47));
    StepRow(*next, counts, "bfd", Move(1, 1, 10));
    PlanStep bad = Boss(1, 2, 0);                     // a boss row without its entry: MissingEntry
    StepRow(*next, counts, "bfd", bad);
    StepRow(*next, counts, "rfk", Move(1, 1, 5));
    next->Seal();
    CHECK(counts.skippedSteps == 1);

    PlanReloadOutcome const out = CommitPlanReload(server.plans, next, counts, server.tactics, Tactics(5), 0, {}, {});
    CHECK(!out.swapped);
    CHECK(out.reason.find("1 step") != std::string::npos);
    CHECK(out.reason.find("bfd leg 1 seq 2 (MissingEntry)") != std::string::npos);
    // Nothing changed: the same tables, nothing retired.
    CHECK(&server.plans.Current() == before);
    CHECK(&server.tactics.Current() == tacticsBefore);
    CHECK(server.plans.Current().Find("bfd")->steps.size() == 3);
    CHECK(server.tactics.Current().Size() == 2);
    CHECK(server.plans.RetiredCount() == 0 && server.tactics.RetiredCount() == 0);
}

static void TestOtherRefusalsKeepTheOldSet()
{
    // A skipped boss row, bad unit lists, a refused tactic row, a refused need row: each keeps the old set.
    for (int which = 0; which < 4; ++which)
    {
        Server server;
        const DungeonPlanTable* before = &server.plans.Current();
        PlanLoadCounts counts;
        std::uint32_t skippedTactics = 0;
        if (which == 0)
            counts.skippedBosses = 1;
        else if (which == 1)
            counts.badUnits = 1;
        else if (which == 2)
            counts.skippedNeeds = 1;
        else
            skippedTactics = 1;
        PlanReloadOutcome const out = CommitPlanReload(server.plans, OldSet(), counts, server.tactics, Tactics(2),
            skippedTactics, {}, {});
        CHECK(!out.swapped);
        CHECK(!out.reason.empty());
        CHECK(&server.plans.Current() == before);
    }
}

static void TestMissingPlanKeepsTheOldSet()
{
    Server server;
    const DungeonPlanTable* before = &server.plans.Current();
    auto next = std::make_shared<DungeonPlanTable>();
    PlanLoadCounts counts;
    CHECK(next->AddPlan("bfd", 48));
    StepRow(*next, counts, "bfd", Move(1, 1, 10));
    next->Seal();
    PlanReloadOutcome out = CommitPlanReload(server.plans, next, counts, server.tactics, Tactics(2), 0, {}, {});
    CHECK(!out.swapped);
    CHECK(out.reason == "plan 'rfk' is gone from ai_dungeon_plan");
    CHECK(&server.plans.Current() == before);

    // A plan that lost every step (its step rows gone) is refused too.
    auto empty = std::make_shared<DungeonPlanTable>();
    CHECK(empty->AddPlan("bfd", 48));
    CHECK(empty->AddPlan("rfk", 47));
    StepRow(*empty, counts, "bfd", Move(1, 1, 10));
    empty->Seal();
    out = CommitPlanReload(server.plans, empty, counts, server.tactics, Tactics(2), 0, {}, {});
    CHECK(!out.swapped);
    CHECK(out.reason == "plan 'rfk' has no steps any more");
    CHECK(&server.plans.Current() == before);

    // An unsealed new table (a loader that never sealed) and plans never loaded at startup.
    auto unsealed = std::make_shared<DungeonPlanTable>();
    CHECK(!CommitPlanReload(server.plans, unsealed, PlanLoadCounts(), server.tactics, Tactics(2), 0, {}, {}).swapped);
    ReloadableTable<DungeonPlanTable> never;
    ReloadableTable<BossTacticTable> neverTactics;
    PlanReloadOutcome const off = CommitPlanReload(never, OldSet(), PlanLoadCounts(), neverTactics, Tactics(2), 0, {}, {});
    CHECK(!off.swapped);
    CHECK(off.reason.find("not loaded at startup") != std::string::npos);
}

static void TestRunningRunKeepsItsPlan()
{
    Server server;
    // A run started from the startup table: it holds a pointer into it, as DungeonRun::Run::plan does.
    const DungeonPlan* running = server.plans.Current().Find("bfd");
    CHECK(running && running->steps.size() == 3);
    const DungeonPlanTable* startTable = &server.plans.Current();

    auto next = std::make_shared<DungeonPlanTable>();
    PlanLoadCounts counts;
    CHECK(next->AddPlan("bfd", 48));
    CHECK(next->AddPlan("rfk", 47));
    StepRow(*next, counts, "bfd", Move(1, 1, 99));
    StepRow(*next, counts, "rfk", Move(1, 1, 5));
    next->Seal();
    PlanReloadOutcome out = CommitPlanReload(server.plans, next, counts, server.tactics, Tactics(2), 0, {}, {running});
    CHECK(out.swapped);
    // The old table is kept: the run's pointer still reads the plan it started with.
    CHECK(out.keptOld == 1 && server.plans.RetiredCount() == 1);
    CHECK(running->steps.size() == 3 && running->steps[0].x == 10.0f && running->name == "bfd");
    CHECK(startTable->Owns(running));
    // A new start finds the new plan.
    CHECK(server.plans.Current().Find("bfd")->steps.size() == 1);
    CHECK(!server.plans.Current().Owns(running));

    // A second reload while the same run still goes keeps it; once no run holds it, the next reload frees it.
    out = CommitPlanReload(server.plans, OldSet(), PlanLoadCounts(), server.tactics, Tactics(2), 0, {}, {running});
    CHECK(out.swapped && out.keptOld == 1);
    out = CommitPlanReload(server.plans, OldSet(), PlanLoadCounts(), server.tactics, Tactics(2), 0, {}, {});
    CHECK(out.swapped && out.keptOld == 0 && server.plans.RetiredCount() == 0);
}

// Loader stand-in: one leg row through AddLeg, counted and named as ReadDungeonPlanRows does.
static void LegRow(DungeonPlanTable& table, PlanLoadCounts& counts, const std::string& plan, std::uint8_t leg)
{
    if (!table.AddLeg(plan, leg, "leg"))
    {
        ++counts.skippedLegs;
        counts.Note("ai_dungeon_leg '" + plan + "' leg " + std::to_string(leg) + ": an unknown plan or a second row of the leg");
    }
}

static void TestBadLegKeepsBothOldTables()
{
    for (int which = 0; which < 2; ++which)
    {
        Server server;
        const DungeonPlanTable* before = &server.plans.Current();
        const BossTacticTable* tacticsBefore = &server.tactics.Current();
        auto next = std::make_shared<DungeonPlanTable>();
        PlanLoadCounts counts;
        CHECK(next->AddPlan("bfd", 48));
        CHECK(next->AddPlan("rfk", 47));
        LegRow(*next, counts, "bfd", 1);
        if (which == 0)
            LegRow(*next, counts, "bfd", 1);      // a second row of leg 1
        else
            LegRow(*next, counts, "nosuch", 1);   // a leg of a plan that has no row
        CHECK(counts.skippedLegs == 1);
        StepRow(*next, counts, "bfd", Move(1, 1, 10));
        StepRow(*next, counts, "rfk", Move(1, 1, 5));
        next->Seal();
        PlanReloadOutcome const out = CommitPlanReload(server.plans, next, counts, server.tactics, Tactics(4), 0, {}, {});
        CHECK(!out.swapped);
        CHECK(out.reason.find("1 leg") != std::string::npos);
        CHECK(out.reason.find(which == 0 ? "'bfd' leg 1" : "'nosuch' leg 1") != std::string::npos);
        CHECK(&server.plans.Current() == before);
        CHECK(&server.tactics.Current() == tacticsBefore);
        CHECK(server.tactics.Current().Size() == 2);
        CHECK(server.plans.RetiredCount() == 0 && server.tactics.RetiredCount() == 0);
    }
}

static TableFingerprint Fp(std::uint64_t rows, std::uint64_t sum)
{
    TableFingerprint fp;
    fp.ok = true;
    fp.rows = rows;
    fp.sum = sum;
    return fp;
}

// The six tables read whole and unchanged.
static std::vector<TableRead> GoodReads()
{
    std::vector<TableRead> reads;
    const char* names[] = {"ai_dungeon_plan", "ai_dungeon_leg", "ai_dungeon_step", "ai_dungeon_leg_need", "ai_dungeon_boss", "ai_boss_tactic"};
    std::uint64_t rows = 2;
    for (const char* name : names)
    {
        TableRead r;
        r.table = name;
        r.before = r.after = Fp(rows, 1000 + rows);
        r.rowsRead = rows;
        reads.push_back(r);
        ++rows;
    }
    return reads;
}

static void TestFailedOrChangingReadsKeepTheOldSet()
{
    {
        Server server;
        CHECK(JudgeTableReads(GoodReads()).empty());
        CHECK(CommitPlanReload(server.plans, OldSet(), PlanLoadCounts(), server.tactics, Tactics(2), 0, GoodReads(), {}).swapped);
    }
    // An empty table read whole is fine: the count query answered 0 and the data query handed nothing.
    {
        std::vector<TableRead> reads = GoodReads();
        reads[3].before = reads[3].after = Fp(0, 0);
        reads[3].rowsRead = 0;
        CHECK(JudgeTableReads(reads).empty());
    }
    for (std::size_t t = 0; t < 5; ++t)
        for (int how = 0; how < 5; ++how)
        {
            std::vector<TableRead> reads = GoodReads();
            TableRead& r = reads[t];
            std::string expect;
            if (how == 0)
            {
                r.before.ok = false;            // the revision query failed before the load
                expect = "the read of " + r.table + " failed (a query error";
            }
            else if (how == 1)
            {
                r.after.ok = false;             // ... or after it
                expect = "the read of " + r.table + " failed (a query error";
            }
            else if (how == 2)
            {
                r.rowsRead = 0;                 // the data query failed (null: read as no rows)
                expect = "the read of " + r.table + " failed: 0 of its";
            }
            else if (how == 3)
            {
                r.after.rows += 1;              // a row added mid-load
                expect = r.table + " changed during the reload";
            }
            else
            {
                r.after.sum += 7;               // a row edited mid-load, same count
                expect = r.table + " changed during the reload";
            }
            Server server;
            const DungeonPlanTable* before = &server.plans.Current();
            const BossTacticTable* tacticsBefore = &server.tactics.Current();
            PlanReloadOutcome const out = CommitPlanReload(server.plans, OldSet(), PlanLoadCounts(), server.tactics, Tactics(3),
                0, reads, {});
            CHECK(!out.swapped);
            if (out.reason.find(expect) == std::string::npos)
            {
                std::cerr << "table " << t << " how " << how << ": " << out.reason << "\n";
                CHECK(false);
            }
            CHECK(&server.plans.Current() == before);
            CHECK(&server.tactics.Current() == tacticsBefore);
            CHECK(server.plans.RetiredCount() == 0 && server.tactics.RetiredCount() == 0);
        }
}

static void TestFingerprintSqlIsNullSafe()
{
    // Every column is wrapped: a value is 'v' + value, a NULL is 'n', so a NULL that moves between columns (which
    // CONCAT_WS alone would skip) changes the checksum.
    CHECK(TableFingerprintSql("ai_dungeon_boss", "`map`, `entry`") ==
        "SELECT COUNT(*), CAST(COALESCE(SUM(CRC32(CONCAT_WS('|', COALESCE(CONCAT('v', `map`), 'n'), "
        "COALESCE(CONCAT('v', `entry`), 'n')))), 0) AS UNSIGNED) FROM `ai_dungeon_boss`");
    std::string const step = TableFingerprintSql("ai_dungeon_step",
        "`plan`, `leg`, `seq`, `kind`, `entry`, `x`, `y`, `z`, `radius`, `item`, `param`, `flags`, `timeout_s`, `entries`");
    std::size_t wrapped = 0;
    for (std::size_t at = step.find("COALESCE(CONCAT('v', "); at != std::string::npos; at = step.find("COALESCE(CONCAT('v', ", at + 1))
        ++wrapped;
    CHECK(wrapped == 14);
    CHECK(step.find("`timeout_s`), 'n')") != std::string::npos);
    CHECK(step.find("SELECT COUNT(*), ") == 0);
}

static void TestOwns()
{
    auto table = OldSet();
    DungeonPlanTable other;
    CHECK(table->Owns(table->Find("bfd")));
    CHECK(!table->Owns(nullptr));
    other.Seal();
    CHECK(!other.Owns(table->Find("bfd")));
}

// The Codex review of 7cfcbff6: a boss step carries the subjects of its ai_boss_tactic rows (JoinBossTactics), so a
// reload in which only a tactic target changed (the same row count, the same step rows) reports that plan changed.
static std::shared_ptr<BossTacticTable> KillFirstOn4887(std::uint32_t target)
{
    auto table = std::make_shared<BossTacticTable>();
    BossTacticRow row;
    row.map = 48;
    row.bossEntry = 4887;
    row.kind = BossTacticKind::KillFirst;
    row.targetEntry = target;
    CHECK(table->Add(row));
    table->Seal();
    return table;
}

static std::shared_ptr<DungeonPlanTable> JoinedSet(const BossTacticTable& tactics)
{
    auto table = std::make_shared<DungeonPlanTable>();
    PlanLoadCounts counts;
    CHECK(table->AddPlan("bfd", 48));
    CHECK(table->AddPlan("rfk", 47));
    StepRow(*table, counts, "bfd", Move(1, 1, 10));
    StepRow(*table, counts, "bfd", Move(1, 2, 20));
    StepRow(*table, counts, "bfd", Boss(1, 3, 4887));
    StepRow(*table, counts, "rfk", Move(1, 1, 5));
    StepRow(*table, counts, "rfk", Move(1, 2, 6));
    CHECK(table->AddBoss(48, 4829));
    table->JoinBossTactics(tactics);
    table->Seal();
    return table;
}

static void TestTacticTargetChangeMarksThePlanChanged()
{
    PlanStep a = Boss(1, 3, 4887);
    PlanStep b = a;
    b.tacticEntries = {222};
    CHECK(!SamePlanStep(a, b));
    a.tacticEntries = {222};
    CHECK(SamePlanStep(a, b));

    auto oldTactics = KillFirstOn4887(111);
    ReloadableTable<DungeonPlanTable> plans;
    ReloadableTable<BossTacticTable> tactics;
    plans.Install(JoinedSet(*oldTactics));
    tactics.Install(oldTactics);
    CHECK(plans.Current().Find("bfd")->steps[2].tacticEntries == std::vector<std::uint32_t>{111});
    // The same target again: nothing changed.
    auto same = KillFirstOn4887(111);
    PlanReloadOutcome const kept = CommitPlanReload(plans, JoinedSet(*same), PlanLoadCounts(), tactics, same, 0, {}, {});
    CHECK(kept.swapped && kept.changes.empty());
    // Only the target changes (one row before, one row after): bfd changed, rfk not.
    auto moved = KillFirstOn4887(222);
    PlanReloadOutcome const out = CommitPlanReload(plans, JoinedSet(*moved), PlanLoadCounts(), tactics, moved, 0, {}, {});
    CHECK(out.swapped && out.tacticsBefore == 1 && tactics.Current().Size() == 1);
    CHECK(out.changes.size() == 1 && out.changes[0] == "bfd 3 steps, rows changed");
    CHECK(plans.Current().Find("bfd")->steps[2].tacticEntries == std::vector<std::uint32_t>{222});
}

// An open_proof row and a plain open are not the same step (DungeonPlanReloadPolicy.h SamePlanStep).
static void TestReloadSeesOpenProof()
{
    PlanStep a = Boss(1, 3, 4887);
    PlanStep b = a;
    a.openProof = true;
    CHECK(!SamePlanStep(a, b));
    b.openProof = true;
    CHECK(SamePlanStep(a, b));
}

// Spec item-leg-skip B3/F14: SamePlan compares needs, and a refused need row refuses the reload.
static void TestReloadSeesNeedPick()
{
    auto a = std::make_shared<DungeonPlanTable>();
    auto b = std::make_shared<DungeonPlanTable>();
    for (auto table : {a, b})
    {
        CHECK(table->AddPlan("bfd", 48));
        PlanStep step = Move(1, 1, 10);
        CHECK(table->AddStep("bfd", step) == PlanStepRefusal::None);
        PlanLegNeed need;
        need.leg = 1;
        need.item = 11000;
        need.mode = LegNeedMode::Door;
        need.pick = 250;
        need.object = 170560;
        need.name = "Shadowforge Key";
        need.how = "Dark Iron Legacy quest (Searing Gorge/BRD)";
        CHECK(table->AddLegNeed("bfd", need) == PlanNeedRefusal::None);
        table->Seal();
    }
    CHECK(SamePlan(*a->Find("bfd"), *b->Find("bfd")));
    auto c = std::make_shared<DungeonPlanTable>();
    CHECK(c->AddPlan("bfd", 48));
    PlanStep step = Move(1, 1, 10);
    CHECK(c->AddStep("bfd", step) == PlanStepRefusal::None);
    PlanLegNeed changed;
    changed.leg = 1;
    changed.item = 11000;
    changed.mode = LegNeedMode::Door;
    changed.pick = 200; // the same row with another pick is a change
    changed.object = 170560;
    changed.name = "Shadowforge Key";
    changed.how = "Dark Iron Legacy quest (Searing Gorge/BRD)";
    CHECK(c->AddLegNeed("bfd", changed) == PlanNeedRefusal::None);
    c->Seal();
    CHECK(!SamePlan(*a->Find("bfd"), *c->Find("bfd")));
}

// The reload's handoff runs before the install and receives the new tactic table: the seek_aura records reconcile
// against the rows that replace the ones they were keyed from.
static void TestBeforeInstallSeesTheNewTactics()
{
    ReloadableTable<DungeonPlanTable> plans;
    ReloadableTable<BossTacticTable> tactics;
    auto oldTactics = KillFirstOn4887(111);
    plans.Install(JoinedSet(*oldTactics));
    tactics.Install(oldTactics);
    auto nextTactics = KillFirstOn4887(111);
    const BossTacticTable* seen = nullptr;
    PlanReloadOutcome const out = CommitPlanReload(plans, JoinedSet(*nextTactics), PlanLoadCounts(), tactics, nextTactics,
        0, {}, {}, [&seen](const BossTacticTable& table) { seen = &table; });
    CHECK(out.swapped);
    CHECK(seen == nextTactics.get());
    CHECK(tactics.Current().Size() == 1);
}

int main()
{
    TestReloadSeesOpenProof();
    TestTacticTargetChangeMarksThePlanChanged();
    TestReloadSeesNeedPick();
    TestBeforeInstallSeesTheNewTactics();
    TestGoodReloadSwaps();
    TestUnchangedReloadListsNothing();
    TestBadRowKeepsTheOldSet();
    TestOtherRefusalsKeepTheOldSet();
    TestMissingPlanKeepsTheOldSet();
    TestRunningRunKeepsItsPlan();
    TestOwns();
    TestFingerprintSqlIsNullSafe();
    TestBadLegKeepsBothOldTables();
    TestFailedOrChangingReadsKeepTheOldSet();
    std::cout << "dungeon plan reload policy: all tests passed\n";
    return 0;
}
