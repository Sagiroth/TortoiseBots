#include "playerbot/playerbot.h"
#include "DungeonPlanMgr.h"
#include "BossTacticsMgr.h"

#include <memory>

using namespace ai;

// The current table, and the replaced ones a run in progress still points into (DungeonPlanReloadPolicy.h).
static ReloadableTable<DungeonPlanTable>& DungeonPlanHolder()
{
    static ReloadableTable<DungeonPlanTable> holder;
    return holder;
}

DungeonPlanTable& ai::GetDungeonPlans()
{
    return DungeonPlanHolder().Current();
}

namespace
{
    // A move this much longer than the one before it is logged (the exporter
    // should cut the leg into via points).
    constexpr float kLongHopYd = 60.0f;
}

// One pass over each table into `table` (empty, not sealed), then the seal: the startup load and `.bot run reloadplans`
// both read through here, so a reload checks every row as the startup did.
// The core's Query hands over a raw QueryResult* the caller frees (src/shared/Database/Database.h): each is held by a
// unique_ptr, as in the rest of the module.
// `tactics` (sealed, read first): the boss steps take the subjects of their `ai_boss_tactic` rows before the seal
// (DungeonPlanTable::JoinBossTactics), so the run leader's deferral gate sees them (PullMayBeDeferred).
static void ReadDungeonPlanRows(DungeonPlanTable& table, PlanLoadCounts& counts, const BossTacticTable& tactics)
{
    std::uint32_t& skippedPlans = counts.skippedPlans;
    if (std::unique_ptr<QueryResult> result{WorldDatabase.Query(
        "SELECT `plan`, `map`, `cc_priority`, `cc_always`, `kill_first` FROM `ai_dungeon_plan`")})
    {
        do
        {
            Field* fields = result->Fetch();
            ++counts.planRows;
            std::string const plan = fields[0].GetCppString();
            if (!table.AddPlan(plan, fields[1].GetUInt32()))
            {
                ++skippedPlans;
                counts.Note("ai_dungeon_plan '" + plan + "' (map " + std::to_string(fields[1].GetUInt32()) +
                    "): an empty name, map 0 or a second row of the name");
            }
            else if (!table.SetUnits(plan, fields[2].GetCppString(), fields[3].GetCppString(), fields[4].GetCppString()))
            {
                ++counts.badUnits;
                counts.Note("ai_dungeon_plan '" + plan + "': cc_priority, cc_always or kill_first is not a list of creature entries");
                sLog.outError("ai_dungeon_plan: plan '%s': cc_priority, cc_always or kill_first is not a list of "
                    "creature entries; the plan runs without crowd control and kill-first targets", plan.c_str());
            }
        } while (result->NextRow());
    }

    if (std::unique_ptr<QueryResult> result{WorldDatabase.Query("SELECT `plan`, `leg`, `name` FROM `ai_dungeon_leg`")})
    {
        do
        {
            Field* fields = result->Fetch();
            ++counts.legRows;
            std::string const plan = fields[0].GetCppString();
            if (!table.AddLeg(plan, fields[1].GetUInt8(), fields[2].GetCppString()))
            {
                // Skipped at startup as before (the plan runs without that leg's name); a reload refuses on it.
                ++counts.skippedLegs;
                counts.Note("ai_dungeon_leg '" + plan + "' leg " + std::to_string(uint32(fields[1].GetUInt8())) +
                    ": an unknown plan or a second row of the leg");
                sLog.outError("ai_dungeon_leg: plan '%s' leg %u skipped (an unknown plan or a second row of the leg)",
                    plan.c_str(), uint32(fields[1].GetUInt8()));
            }
        } while (result->NextRow());
    }

    std::uint32_t& skippedSteps = counts.skippedSteps;
    if (std::unique_ptr<QueryResult> result{WorldDatabase.Query(
        "SELECT `plan`, `leg`, `seq`, `kind`, `entry`, `x`, `y`, `z`, `radius`, `item`, `param`, `flags`, "
        "`timeout_s`, `entries`, `comment` FROM `ai_dungeon_step`")})
    {
        do
        {
            Field* fields = result->Fetch();
            ++counts.stepRows;
            std::string const plan = fields[0].GetCppString();
            PlanStep step;
            step.leg = fields[1].GetUInt8();
            step.seq = fields[2].GetUInt16();
            ParsePlanStepRowKind(fields[3].GetCppString(), step.kind, step.exactLaunch, step.openProof);
            step.entry = fields[4].GetUInt32();
            step.x = fields[5].GetFloat();
            step.y = fields[6].GetFloat();
            step.z = fields[7].GetFloat();
            step.radius = fields[8].GetFloat();
            step.item = fields[9].GetUInt32();
            step.param = fields[10].GetUInt32();
            step.flags = fields[11].GetUInt32();
            step.timeoutS = fields[12].GetUInt16();
            step.label = fields[14].GetCppString();
            PlanStepRefusal refusal = ParsePlanEntries(fields[13].GetCppString(), step.entries)
                ? table.AddStep(plan, step) : PlanStepRefusal::BadEntries;
            if (refusal != PlanStepRefusal::None)
            {
                ++skippedSteps;
                counts.Note("ai_dungeon_step '" + plan + "' leg " + std::to_string(uint32(step.leg)) + " seq " +
                    std::to_string(uint32(step.seq)) + " (" + fields[3].GetCppString() + "): " + PlanStepRefusalName(refusal));
                sLog.outError("ai_dungeon_step: plan '%s' leg %u seq %u (%s) skipped (%s)", plan.c_str(),
                    uint32(step.leg), uint32(step.seq), fields[3].GetCppString().c_str(), PlanStepRefusalName(refusal));
            }
        } while (result->NextRow());
    }

    // Leg needs after the steps (AddLegNeed checks the leg against the step rows). A null result
    // (an old database without the table) is no needs and no error: the startup must not lose every plan.
    std::uint32_t& skippedNeeds = counts.skippedNeeds;
    if (std::unique_ptr<QueryResult> result{WorldDatabase.Query(
        "SELECT `plan`, `leg`, `item`, `mode`, `pick`, `object`, `name`, `how` FROM `ai_dungeon_leg_need`")})
    {
        do
        {
            Field* fields = result->Fetch();
            ++counts.needRows;
            std::string const plan = fields[0].GetCppString();
            PlanLegNeed need;
            need.leg = fields[1].GetUInt8();
            need.item = fields[2].GetUInt32();
            need.mode = static_cast<LegNeedMode>(fields[3].GetUInt8());
            need.pick = fields[4].GetUInt16();
            need.object = fields[5].GetUInt32();
            need.name = fields[6].GetCppString();
            need.how = fields[7].GetCppString();
            PlanNeedRefusal refusal = table.AddLegNeed(plan, need);
            if (refusal != PlanNeedRefusal::None)
            {
                ++skippedNeeds;
                counts.Note("ai_dungeon_leg_need '" + plan + "' leg " + std::to_string(uint32(need.leg)) + " item " +
                    std::to_string(need.item) + ": " + PlanNeedRefusalName(refusal));
                sLog.outError("ai_dungeon_leg_need: plan '%s' leg %u item %u skipped (%s)", plan.c_str(),
                    uint32(need.leg), need.item, PlanNeedRefusalName(refusal));
            }
        } while (result->NextRow());
    }

    // Bosses no boss step names (the telemetry's boss rank, DungeonPlanTable::IsBossEntry).
    std::uint32_t& skippedBosses = counts.skippedBosses;
    if (std::unique_ptr<QueryResult> result{WorldDatabase.Query("SELECT `map`, `entry` FROM `ai_dungeon_boss`")})
    {
        do
        {
            Field* fields = result->Fetch();
            ++counts.bossRows;
            if (!table.AddBoss(fields[0].GetUInt32(), fields[1].GetUInt32()))
            {
                ++skippedBosses;
                counts.Note("ai_dungeon_boss map " + std::to_string(fields[0].GetUInt32()) + " entry " +
                    std::to_string(fields[1].GetUInt32()) + ": a zero map or entry, or a second row");
            }
        } while (result->NextRow());
    }

    table.JoinBossTactics(tactics);
    // Sealed even when the tables are empty or missing: `.bot run` then finds no plan.
    table.Seal();
    for (const DungeonPlan* plan : table.All())
    {
        std::size_t const longHops = CountLongHops(*plan, kLongHopYd);
        if (longHops)
            sLog.outError("ai_dungeon_step: plan '%s' has %u moves more than %.0f yd from the move before them",
                plan->name.c_str(), uint32(longHops), kLongHopYd);
    }
}

void ai::LoadDungeonPlans()
{
    DungeonPlanTable& table = GetDungeonPlans();
    if (table.Sealed())
        return;
    PlanLoadCounts counts;
    LoadBossTactics(); // the tactics first (nothing when the startup already read them): the boss steps join them
    ReadDungeonPlanRows(table, counts, GetBossTactics());
    sLog.outString(">> %s", PlanLoadSummary(table, counts).c_str());
    sLog.outString();
}

// The revision of one table (DungeonPlanReloadPolicy.h TableFingerprint, TableFingerprintSql): its row count and the sum
// of a NULL-safe CRC32 of the columns the loader reads, by one query that always answers one row, so a null answer is a
// failed query.
static TableFingerprint ReadTableFingerprint(char const* table, char const* columns)
{
    TableFingerprint fp;
    std::string const sql = TableFingerprintSql(table, columns);
    std::unique_ptr<QueryResult> result(WorldDatabase.Query(sql.c_str()));
    if (!result)
        return fp;
    Field* fields = result->Fetch();
    fp.ok = true;
    fp.rows = fields[0].GetUInt64();
    fp.sum = fields[1].GetUInt64();
    return fp;
}

namespace
{
    // The six tables of a reload and the columns their loaders read.
    struct ReloadTable { char const* table; char const* columns; };
    constexpr ReloadTable kReloadTables[] = {
        {"ai_dungeon_plan", "`plan`, `map`, `cc_priority`, `cc_always`, `kill_first`"},
        {"ai_dungeon_leg", "`plan`, `leg`, `name`"},
        {"ai_dungeon_step", "`plan`, `leg`, `seq`, `kind`, `entry`, `x`, `y`, `z`, `radius`, `item`, `param`, `flags`, "
            "`timeout_s`, `entries`"},
        {"ai_dungeon_leg_need", "`plan`, `leg`, `item`, `mode`, `pick`, `object`, `name`, `how`"},
        {"ai_dungeon_boss", "`map`, `entry`"},
        {"ai_boss_tactic", "`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`"},
    };
}

// `.bot run reloadplans`, world thread: both tables are read into new ones through the startup loaders, and replace the
// old ones only when CommitPlanReload accepts them (a failed query, a table changed mid-load, any refused row, a plan
// gone: the old set stays, nothing changes). The core's world database hands each Query to a connection of its pool and
// has no read transaction, so the six reads cannot share one snapshot: each table's revision is read before the first
// data query and after the last one, and any difference refuses the reload (JudgeTableReads).
PlanReloadReport ai::ReloadDungeonPlans(std::vector<DungeonPlan const*> const& inUse)
{
    PlanReloadReport report;
    if (!GetDungeonPlans().Sealed())
    {
        report.outcome.reason = "the dungeon plans were not loaded at startup (the AI is off): nothing to reload";
        return report;
    }
    std::vector<TableRead> reads;
    for (ReloadTable const& t : kReloadTables)
    {
        TableRead r;
        r.table = t.table;
        r.before = ReadTableFingerprint(t.table, t.columns);
        reads.push_back(r);
    }
    // The tactics first: the new plans' boss steps join the new tactic rows (JoinBossTactics).
    auto nextTactics = std::make_shared<BossTacticTable>();
    std::uint64_t tacticRowsRead = 0;
    uint32 const skippedTactics = ReadBossTacticRows(*nextTactics, &tacticRowsRead);
    auto next = std::make_shared<DungeonPlanTable>();
    PlanLoadCounts counts;
    ReadDungeonPlanRows(*next, counts, *nextTactics);
    std::uint64_t const rowsRead[] = {counts.planRows, counts.legRows, counts.stepRows, counts.needRows, counts.bossRows, tacticRowsRead};
    for (std::size_t i = 0; i < reads.size(); ++i)
    {
        reads[i].after = ReadTableFingerprint(kReloadTables[i].table, kReloadTables[i].columns);
        reads[i].rowsRead = rowsRead[i];
    }
    report.summary = PlanLoadSummary(*next, counts);
    report.tacticRows = uint32(nextTactics->Size());
    report.outcome = CommitPlanReload(DungeonPlanHolder(), std::move(next), counts, BossTacticsHolder(), std::move(nextTactics),
        skippedTactics, reads, inUse);
    if (report.outcome.swapped)
        sLog.outString(">> %s, %u boss tactic rows (reloaded by .bot run reloadplans)", report.summary.c_str(), report.tacticRows);
    else
        sLog.outError(".bot run reloadplans refused, the old dungeon plans stay: %s", report.outcome.reason.c_str());
    return report;
}
