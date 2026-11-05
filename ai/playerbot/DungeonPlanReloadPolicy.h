#pragma once

// `.bot run reloadplans` (owner goal 2026-10-22: fix dungeon plan blockers by a hot reload instead of an image rebuild and
// a server restart): the pure rules of the reload of `ai_dungeon_plan`, `ai_dungeon_leg`, `ai_dungeon_step`,
// `ai_dungeon_leg_need`, `ai_dungeon_boss` and `ai_boss_tactic`.
//
// The loader (DungeonPlanMgr.cpp ReadDungeonPlanRows, BossTacticsMgr.cpp ReadBossTacticRows) reads the rows into NEW
// sealed tables, the same code and the same row checks as at startup. CommitPlanReload then decides:
//   - a failed query or a table that changed while it was read (JudgeTableReads: the core's Query answers null for an
//     error and for no rows alike, so each table's row count and checksum are read before and after the load);
//   - any refused row (a plan, leg, step, need or boss row skipped, a plan's unit lists that do not parse, a tactic row
//     skipped), a plan of the old set that is gone or has no steps any more: the old tables stay, nothing changes, the
//     reason is returned (at startup such rows are skipped with an error line; a reload is stricter, since the old set
//     works);
//   - else both new tables replace the old ones at once.
// A run in progress holds a raw `const DungeonPlan*` into the table it started from (DungeonRun::Run::plan). The replaced
// plan table is therefore RETIRED, not freed: ReloadableTable keeps it alive as long as a run's plan points into it
// (checked on each reload against every run's pointer), so such a run goes on with the copy it started with, and the
// next `.bot run start` takes the new plan. No boss tactic row is held past the update that read it (ScanBossTactics
// caches one scan per bot and AI tick, and the cache is keyed by the table's identity, so a replaced table's rows are
// never read again; the seek_aura fight records copy every column they read and key on the row's primary key, never
// its address, and the reload reconciles them against the new row set before the old table is retired), so the replaced
// tactic table is freed at once.
//
// World thread only: chat commands run there (a player's `.bot` is queued and drained by the world tick, a console or
// SOAP command runs in the world's CLI pass), and every reader of the tables (run ticks, bot AI, the dashboard snapshot)
// runs there or on a map thread while the world thread waits for it. No lock is taken.
//
// No core includes: tested in tools/test_dungeon_plan_reload_policy.cpp.

#include "BossTacticsPolicy.h"
#include "DungeonPlanPolicy.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ai
{
    // The current table and the replaced ones a reader may still point into.
    template <class Table>
    class ReloadableTable
    {
    public:
        ReloadableTable() : current_(std::make_shared<Table>()) {}

        Table& Current() { return *current_; }
        const Table& Current() const { return *current_; }

        // `next` becomes the current table; the old one is retired until Prune lets it go.
        void Install(std::shared_ptr<Table> next)
        {
            retired_.push_back(std::move(current_));
            current_ = std::move(next);
        }

        // Frees every retired table `inUse` does not keep. Returns how many stay.
        template <class InUse>
        std::size_t Prune(InUse inUse)
        {
            std::vector<std::shared_ptr<Table>> keep;
            for (std::shared_ptr<Table>& table : retired_)
                if (inUse(static_cast<const Table&>(*table)))
                    keep.push_back(std::move(table));
            retired_.swap(keep);
            return retired_.size();
        }

        std::size_t RetiredCount() const { return retired_.size(); }

    private:
        std::shared_ptr<Table> current_;
        std::vector<std::shared_ptr<Table>> retired_;
    };

    // What one pass of the plan loader refused (the counts of the boot line, and for a reload the first few rows named).
    struct PlanLoadCounts
    {
        static constexpr std::size_t kNamedRefusals = 3;

        std::uint32_t skippedPlans = 0;
        std::uint32_t skippedLegs = 0;       // unknown plan, or a second row of a leg number
        std::uint32_t skippedSteps = 0;
        std::uint32_t skippedBosses = 0;
        std::uint32_t skippedNeeds = 0;      // refused ai_dungeon_leg_need rows
        std::uint32_t badUnits = 0;          // plans whose cc_priority / cc_always / kill_first / cc_never did not parse
        std::vector<std::string> refusals;   // the first kNamedRefusals refused rows, in words
        // Rows each query handed the loader (accepted or not): a reload compares them with the table's row count
        // (TableRead), since the core's Query answers null both for an error and for no rows.
        std::uint64_t planRows = 0, legRows = 0, stepRows = 0, bossRows = 0, needRows = 0;

        void Note(const std::string& what)
        {
            if (refusals.size() < kNamedRefusals)
                refusals.push_back(what);
        }

        std::uint32_t Refused() const { return skippedPlans + skippedLegs + skippedSteps + skippedBosses + skippedNeeds + badUnits; }
    };

    // A cheap revision of one table: its row count and a sum of a checksum of every row's loaded columns, read by a query
    // that always answers one row (COUNT(*)), so a null answer is a failed query, never an empty table.
    struct TableFingerprint
    {
        bool ok = false;           // the query answered
        std::uint64_t rows = 0;
        std::uint64_t sum = 0;

        bool operator==(const TableFingerprint& o) const { return ok == o.ok && rows == o.rows && sum == o.sum; }
        bool operator!=(const TableFingerprint& o) const { return !(*this == o); }
    };

    // The revision query of one table: its row count and the sum of a CRC32 of every row's `columns` (the loader's
    // SELECT list, "`a`, `b`"). Each column is NULL-safe and unambiguous: a value is 'v' + the value, a NULL is 'n'
    // (CONCAT_WS alone skips a NULL, so a NULL moving to the next column would keep the checksum). COUNT(*) always
    // answers one row, so a null result is a failed query.
    inline std::string TableFingerprintSql(const std::string& table, const std::string& columns)
    {
        std::string parts;
        std::size_t pos = 0;
        while (pos < columns.size())
        {
            std::size_t comma = columns.find(',', pos);
            std::string column = columns.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            std::size_t const first = column.find_first_not_of(' ');
            std::size_t const last = column.find_last_not_of(' ');
            column = first == std::string::npos ? std::string() : column.substr(first, last - first + 1);
            if (!column.empty())
                parts += (parts.empty() ? "" : ", ") + std::string("COALESCE(CONCAT('v', ") + column + "), 'n')";
            if (comma == std::string::npos)
                break;
            pos = comma + 1;
        }
        return "SELECT COUNT(*), CAST(COALESCE(SUM(CRC32(CONCAT_WS('|', " + parts + "))), 0) AS UNSIGNED) FROM `" +
            table + "`";
    }

    // One table of a reload: its revision before the data query and after the last one, and the rows the data query
    // handed the loader.
    struct TableRead
    {
        std::string table;
        TableFingerprint before;
        TableFingerprint after;
        std::uint64_t rowsRead = 0;
    };

    // Why the reads cannot be trusted, or "" when every table was read whole and none changed meanwhile. The core's
    // world database has a pool of connections and no read transaction, so the six queries cannot share one snapshot:
    // instead each table's revision is read before and after the whole load, and any difference refuses the reload.
    inline std::string JudgeTableReads(const std::vector<TableRead>& reads)
    {
        for (const TableRead& r : reads)
        {
            if (!r.before.ok || !r.after.ok)
                return "the read of " + r.table + " failed (a query error: see the server log)";
            if (r.before != r.after)
                return r.table + " changed during the reload (" + std::to_string(r.before.rows) + " -> " +
                    std::to_string(r.after.rows) + " rows): send the reload again once the migration is in";
            if (r.rowsRead != r.before.rows)
                return "the read of " + r.table + " failed: " + std::to_string(r.rowsRead) + " of its " +
                    std::to_string(r.before.rows) + " rows came back";
        }
        return std::string();
    }

    // The boot log's line, without its ">> " prefix:
    // "Loaded 11 dungeon plans, 3340 steps, 70 dungeon bosses (0 plans, 0 steps and 0 boss rows skipped)".
    inline std::string PlanLoadSummary(const DungeonPlanTable& table, const PlanLoadCounts& counts)
    {
        char line[200];
        std::snprintf(line, sizeof(line),
            "Loaded %u dungeon plans, %u steps, %u dungeon bosses (%u plans, %u steps and %u boss rows skipped)",
            static_cast<unsigned>(table.PlanCount()), static_cast<unsigned>(table.StepCount()),
            static_cast<unsigned>(table.BossCount()), counts.skippedPlans, counts.skippedSteps, counts.skippedBosses);
        return line;
    }

    inline bool SamePlanStep(const PlanStep& a, const PlanStep& b)
    {
        return a.leg == b.leg && a.seq == b.seq && a.kind == b.kind && a.entry == b.entry && a.x == b.x && a.y == b.y &&
            a.z == b.z && a.radius == b.radius && a.item == b.item && a.param == b.param && a.flags == b.flags &&
            a.timeoutS == b.timeoutS && a.entries == b.entries && a.exactLaunch == b.exactLaunch && a.openProof == b.openProof &&
            a.tacticEntries == b.tacticEntries; // a boss step's joined ai_boss_tactic subjects (JoinBossTactics)
    }

    inline bool SamePlanNeed(const PlanLegNeed& a, const PlanLegNeed& b)
    {
        return a.leg == b.leg && a.item == b.item && a.mode == b.mode && a.pick == b.pick && a.object == b.object &&
            a.name == b.name && a.how == b.how;
    }

    inline bool SamePlan(const DungeonPlan& a, const DungeonPlan& b)
    {
        if (a.map != b.map || a.legs.size() != b.legs.size() || a.steps.size() != b.steps.size() ||
            a.needs.size() != b.needs.size() ||
            a.ccPriority != b.ccPriority || a.ccAlways != b.ccAlways || a.killFirst != b.killFirst ||
            a.ccNever != b.ccNever)
            return false;
        for (std::size_t i = 0; i < a.legs.size(); ++i)
            if (a.legs[i].leg != b.legs[i].leg || a.legs[i].name != b.legs[i].name)
                return false;
        for (std::size_t i = 0; i < a.steps.size(); ++i)
            if (!SamePlanStep(a.steps[i], b.steps[i]))
                return false;
        for (std::size_t i = 0; i < a.needs.size(); ++i)
            if (!SamePlanNeed(a.needs[i], b.needs[i]))
                return false;
        return true;
    }

    // Why the new set may not replace the old one, or "" when it may. Both tables are sealed.
    inline std::string JudgePlanReload(const DungeonPlanTable& old, const DungeonPlanTable& next, const PlanLoadCounts& counts,
        std::uint32_t skippedTactics)
    {
        if (!old.Sealed())
            return "the dungeon plans were not loaded at startup (the AI is off): nothing to reload";
        if (!next.Sealed())
            return "the new plan set was not sealed";
        if (counts.Refused() || skippedTactics)
        {
            std::string why = std::to_string(counts.skippedPlans) + " plan, " + std::to_string(counts.skippedLegs) +
                " leg, " + std::to_string(counts.skippedSteps) +
                " step, " + std::to_string(counts.skippedBosses) + " boss and " + std::to_string(skippedTactics) +
                " boss tactic rows refused, " + std::to_string(counts.badUnits) + " plans with bad unit lists";
            for (const std::string& r : counts.refusals)
                why += "; " + r;
            return why + " (the server log names every row)";
        }
        for (const DungeonPlan* plan : old.All())
        {
            const DungeonPlan* now = next.Find(plan->name);
            if (!now)
                return "plan '" + plan->name + "' is gone from ai_dungeon_plan";
            if (!plan->steps.empty() && now->steps.empty())
                return "plan '" + plan->name + "' has no steps any more";
        }
        return std::string();
    }

    // One line per plan that is new or changed: "bfd 120 -> 131 steps", "bfd 120 steps, rows changed", "rfk new, 80 steps".
    inline std::vector<std::string> PlanReloadChanges(const DungeonPlanTable& old, const DungeonPlanTable& next)
    {
        std::vector<std::string> out;
        for (const DungeonPlan* plan : next.All())
        {
            const DungeonPlan* was = old.Find(plan->name);
            if (!was)
                out.push_back(plan->name + " new, " + std::to_string(plan->steps.size()) + " steps");
            else if (was->steps.size() != plan->steps.size())
                out.push_back(plan->name + " " + std::to_string(was->steps.size()) + " -> " +
                    std::to_string(plan->steps.size()) + " steps");
            else if (!SamePlan(*was, *plan))
                out.push_back(plan->name + " " + std::to_string(plan->steps.size()) + " steps, rows changed");
        }
        return out;
    }

    struct PlanReloadOutcome
    {
        bool swapped = false;
        std::string reason;                 // why not, when not swapped
        std::vector<std::string> changes;   // PlanReloadChanges, when swapped
        std::size_t bossesBefore = 0;
        std::size_t tacticsBefore = 0;
        std::size_t keptOld = 0;            // replaced plan tables kept alive for runs in progress
    };

    // Replaces both tables when JudgeTableReads and JudgePlanReload allow it, else changes nothing. `reads`: the six
    // tables' revisions and rows read. `inUse`: every run's plan pointer. `beforeInstall`: called with the new tactic
    // table just before the old one is retired, so the seek_aura fight records can be reconciled against the rows that
    // replace the ones they were keyed from (and the old table may then be freed).
    inline PlanReloadOutcome CommitPlanReload(ReloadableTable<DungeonPlanTable>& plans, std::shared_ptr<DungeonPlanTable> next,
        const PlanLoadCounts& counts, ReloadableTable<BossTacticTable>& tactics, std::shared_ptr<BossTacticTable> nextTactics,
        std::uint32_t skippedTactics, const std::vector<TableRead>& reads, const std::vector<const DungeonPlan*>& inUse,
        const std::function<void(const BossTacticTable&)>& beforeInstall = std::function<void(const BossTacticTable&)>())
    {
        PlanReloadOutcome out;
        if (!next || !nextTactics)
        {
            out.reason = "no new table";
            return out;
        }
        if (!plans.Current().Sealed())
        {
            out.reason = "the dungeon plans were not loaded at startup (the AI is off): nothing to reload";
            return out;
        }
        out.reason = JudgeTableReads(reads);
        if (out.reason.empty())
            out.reason = JudgePlanReload(plans.Current(), *next, counts, skippedTactics);
        if (out.reason.empty() && !nextTactics->Sealed())
            out.reason = "the new boss tactic table was not sealed";
        if (!out.reason.empty())
            return out;
        out.changes = PlanReloadChanges(plans.Current(), *next);
        out.bossesBefore = plans.Current().BossCount();
        out.tacticsBefore = tactics.Current().Size();
        if (beforeInstall)
            beforeInstall(*nextTactics);
        plans.Install(std::move(next));
        tactics.Install(std::move(nextTactics));
        out.keptOld = plans.Prune([&inUse](const DungeonPlanTable& table)
        {
            for (const DungeonPlan* plan : inUse)
                if (table.Owns(plan))
                    return true;
            return false;
        });
        tactics.Prune([](const BossTacticTable&) { return false; });
        out.swapped = true;
        return out;
    }
}
