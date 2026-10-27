"""Wiring pins for `.bot run reloadplans` (owner goal 2026-10-22: fix dungeon plan blockers by a hot reload instead of an
image rebuild and a server restart).

The pure rules (a refused row or a plan gone keeps the old set, the per-plan changes, the replaced table kept alive while
a run's plan points into it) live in ai/playerbot/DungeonPlanReloadPolicy.h and are unit-tested in
tools/test_dungeon_plan_reload_policy.cpp. These pins prove the live code uses them: the command is registered under
`.bot run`, GameMaster-gated before anything else is read, reads the rows through the SAME loaders the startup uses,
hands every run's plan pointer to the commit, and rebuilds the boss spawn cache after a swap.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CMD = "\n".join((ROOT / "commands" / n).read_text() for n in ("Commander.cpp", "Action.cpp", "Party.cpp", "BotCommands.cpp"))
MGR = (ROOT / "ai/playerbot/DungeonPlanMgr.cpp").read_text()
TACTICS = (ROOT / "ai/playerbot/BossTacticsMgr.cpp").read_text()
DOC_CMD = (ROOT / "docs/play/commands/commander.md").read_text()
DOC_RUN = (ROOT / "docs/play/dungeon-run-leader.md").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class ReloadPlansWiring(unittest.TestCase):
    def test_registered_under_bot_run_before_the_run_leader_checks(self):
        run = body(CMD, "bool HandleDungeonRun(", "\nvoid UpdateRemoteCommanders(")
        hook = 'if (verb == "reloadplans")\n        return HandleReloadPlans(handler, intent);'
        self.assertIn(hook, run)
        # the dispatch carries the GM side branches and the in-game path; the run-leader gate lives in RunVerbAs
        self.assertIn("return RunVerbAs(handler, Requester(handler), verb, rawArg, intent);", run)
        verb = body(CMD, "static bool RunVerbAs(ChatHandler* handler, Player* commander, std::string const& verb, std::string const& rawArg,\n    std::string const& intent)\n{", "\n}\n")
        self.assertIn("RemoteCommander::RunSurfaceOn(", verb)
        self.assertIn("/ reloadplans (GameMaster)", run)
        # `.bot run` reaches HandleDungeonRun
        self.assertIn('{"run", HandleDungeonRun,', CMD)

    def test_gm_gated_first(self):
        fn = body(CMD, "static bool HandleReloadPlans(")
        gate = "if (requester ? !IsBotAdministrator(requester) : !IsGmCliHandler(handler))"
        self.assertIn(gate, fn)
        self.assertIn('SendActionError(handler, intent, "forbidden", "Only a GameMaster may reload the dungeon plans.");', fn)
        self.assertLess(fn.index(gate), fn.index("ai::ReloadDungeonPlans("))
        self.assertLess(fn.index(gate), fn.index("RebuildBossSpawnCache("))
        cli = body(CMD, "bool IsGmCliHandler(ChatHandler* handler)")
        self.assertIn("if (!handler || handler->GetSession())\n        return false;", cli)
        self.assertIn("cli && cli->GetAccessLevel() >= SEC_GAMEMASTER", cli)
        # the GM rank: account security >= SEC_GAMEMASTER, which this core has only as the shim's alias of
        # SEC_DEVELOPER (3); the docs name the value
        shim = (ROOT / "ai/cmangos-compat-shim.h").read_text()
        self.assertIn("#define SEC_GAMEMASTER SEC_DEVELOPER", shim)
        self.assertIn("`SEC_DEVELOPER` = **3**", DOC_CMD)
        self.assertIn("`SEC_DEVELOPER`, which is **3**", DOC_RUN)
        admin = (ROOT / "commands/BotCommandContext.cpp").read_text()
        self.assertIn("requester->GetSession()->GetSecurity() >= SEC_GAMEMASTER", admin)

    def test_every_runs_plan_is_handed_to_the_reload(self):
        fn = body(CMD, "static bool HandleReloadPlans(")
        self.assertRegex(fn, r"for \(auto const& entry : DungeonRuns\(\)\)\s*if \(entry\.second\.run\.plan\)\s*"
                             r"inUse\.push_back\(entry\.second\.run\.plan\);")
        self.assertIn("ai::ReloadDungeonPlans(inUse)", fn)

    def test_refusal_answers_and_changes_nothing_else(self):
        fn = body(CMD, "static bool HandleReloadPlans(")
        refused = fn[fn.index("if (!report.outcome.swapped)"):]
        refused = refused[:refused.index("return true;")]
        self.assertIn('SendActionError(handler, intent, "refused", why.c_str());', refused)
        self.assertIn("report.outcome.reason", refused)
        # the spawn cache is rebuilt only after a swap
        self.assertLess(fn.index("if (!report.outcome.swapped)"), fn.index("RebuildBossSpawnCache(ms, maps)"))

    def test_answer_prints_the_boot_line_and_the_changes(self):
        fn = body(CMD, "static bool HandleReloadPlans(")
        self.assertIn('"%s, reloaded (dungeon bosses %u -> %u, boss tactic rows %u -> %u)", report.summary.c_str()', fn)
        self.assertIn("for (std::string const& change : report.outcome.changes)", fn)
        self.assertIn('"Boss spawn cache rebuilt in %u ms for %u maps"', fn)
        self.assertIn('"Boss spawn cache NOT rebuilt: ', fn)
        self.assertIn('SendActionAck(handler, intent, "plans", uint32(plans.PlanCount()));', fn)

    def test_reload_reads_through_the_startup_loaders(self):
        load = body(MGR, "void ai::LoadDungeonPlans()")
        reload = body(MGR, "PlanReloadReport ai::ReloadDungeonPlans(")
        # one row reader for both
        self.assertIn("ReadDungeonPlanRows(table, counts, GetBossTactics());", load)
        self.assertIn("ReadDungeonPlanRows(*next, counts, *nextTactics);", reload) # the boss steps join the new tactics
        # the six data queries and the revision query
        self.assertEqual(MGR.count("WorldDatabase.Query("), 6)
        for select in ("FROM `ai_dungeon_plan`", "FROM `ai_dungeon_leg`", "FROM `ai_dungeon_step`",
                       "FROM `ai_dungeon_leg_need`", "FROM `ai_dungeon_boss`"):
            self.assertIn(select, body(MGR, "static void ReadDungeonPlanRows("))
        self.assertIn('sLog.outString(">> %s", PlanLoadSummary(table, counts).c_str());', load)
        self.assertIn("report.summary = PlanLoadSummary(*next, counts);", reload)
        # boss tactics through the startup reader too
        self.assertIn("uint32 const skippedTactics = ReadBossTacticRows(*nextTactics, &tacticRowsRead);", reload)
        self.assertIn("ReadBossTacticRows(table);", body(TACTICS, "void ai::LoadBossTactics()"))
        self.assertEqual(TACTICS.count("WorldDatabase.Query("), 1)
        # the decision is the tested commit, into the holders the getters read
        self.assertIn("CommitPlanReload(DungeonPlanHolder(), std::move(next), counts, BossTacticsHolder(), "
                      "std::move(nextTactics),\n        skippedTactics, reads, inUse);", reload)
        self.assertIn("return DungeonPlanHolder().Current();", body(MGR, "DungeonPlanTable& ai::GetDungeonPlans()"))
        self.assertIn("return BossTacticsHolder().Current();", body(TACTICS, "BossTacticTable& ai::GetBossTactics()"))
        # never reloads before the startup load sealed the set
        self.assertRegex(reload, r"if \(!GetDungeonPlans\(\)\.Sealed\(\)\)")

    def test_revisions_are_read_around_the_whole_load(self):
        reload = body(MGR, "PlanReloadReport ai::ReloadDungeonPlans(")
        before = reload.index("r.before = ReadTableFingerprint(t.table, t.columns);")
        after = reload.index("reads[i].after = ReadTableFingerprint(kReloadTables[i].table, kReloadTables[i].columns);")
        # The tactics are read first (the plans' boss steps join them: JoinBossTactics), the plans last.
        self.assertLess(before, reload.index("ReadBossTacticRows(*nextTactics, &tacticRowsRead);"))
        self.assertLess(reload.index("ReadDungeonPlanRows(*next, counts, *nextTactics);"), after)
        self.assertLess(after, reload.index("CommitPlanReload("))
        self.assertIn("std::uint64_t const rowsRead[] = {counts.planRows, counts.legRows, counts.stepRows, counts.needRows, "
                      "counts.bossRows, tacticRowsRead};", reload)
        self.assertIn("reads[i].rowsRead = rowsRead[i];", reload)
        fp = body(MGR, "static TableFingerprint ReadTableFingerprint(")
        # COUNT(*) always answers one row: a null answer is a failed query, never an empty table
        self.assertIn("std::string const sql = TableFingerprintSql(table, columns);", fp)
        self.assertRegex(fp, r"if \(!result\)\s*return fp;")
        self.assertIn("fp.ok = true;", fp)
        # the revision covers the columns each loader reads, for all six tables
        tables = MGR[MGR.index("constexpr ReloadTable kReloadTables[]"):]
        tables = tables[:tables.index("};")]
        for name in ("ai_dungeon_plan", "ai_dungeon_leg", "ai_dungeon_step", "ai_dungeon_leg_need", "ai_dungeon_boss",
                     "ai_boss_tactic"):
            self.assertIn('{"%s", ' % name, tables)
        self.assertIn("`timeout_s`, `entries`", tables)
        self.assertIn("`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`", tables)
        self.assertIn("++*rowsRead;", body(TACTICS, "std::uint32_t ai::ReadBossTacticRows("))

        # The revision of each table covers exactly the columns its loader SELECTs, in order: they cannot drift.
        def joined(text):
            return re.sub(r'"\s*\n\s*"', "", text)
        loaders = {}
        for source in (MGR, TACTICS):
            for columns, table in re.findall(r'"SELECT ((?:`\w+`, )*`\w+`) FROM `(\w+)`"', joined(source)):
                loaders[table] = [c.strip() for c in columns.split(",")]
        tables = joined(MGR[MGR.index("constexpr ReloadTable kReloadTables[]"):])
        tables = tables[:tables.index("};")]
        fingerprints = {t: [c.strip() for c in cols.split(",")] for t, cols in re.findall(r'\{"(\w+)", "([^"]+)"\}', tables)}
        self.assertEqual(sorted(fingerprints), sorted(["ai_dungeon_plan", "ai_dungeon_leg", "ai_dungeon_step",
                                                       "ai_dungeon_leg_need", "ai_dungeon_boss", "ai_boss_tactic"]))

    def test_every_refused_row_is_counted_for_the_reload(self):
        rows = body(MGR, "static void ReadDungeonPlanRows(")
        for counter in ("++counts.planRows;", "++counts.legRows;", "++counts.stepRows;", "++counts.needRows;",
                        "++counts.bossRows;"):
            self.assertEqual(rows.count(counter), 1, counter)
        self.assertIn("if (!table.AddLeg(plan, fields[1].GetUInt8(), fields[2].GetCppString()))", rows)
        self.assertIn("++counts.skippedLegs;", rows)
        self.assertIn("++skippedPlans;", rows)
        self.assertIn("++counts.badUnits;", rows)
        self.assertIn("++skippedSteps;", rows)
        self.assertIn("++skippedNeeds;", rows)
        self.assertIn("++skippedBosses;", rows)
        self.assertEqual(rows.count("counts.Note("), 6)
        self.assertIn("std::uint32_t& skippedSteps = counts.skippedSteps;", rows)

    def test_cache_rebuild_clears_first(self):
        fn = body(CMD, "static bool RebuildBossSpawnCache(uint32& ms, uint32& maps)\n{")
        self.assertLess(fn.index("cache.built = false;"), fn.index("BuildBossSpawnCache();"))
        self.assertLess(fn.index("cache.byMap.clear();"), fn.index("BuildBossSpawnCache();"))
        self.assertIn("cache.buildMs = uint32(ms);", body(CMD, "void BuildBossSpawnCache()"))

    def test_documented(self):
        self.assertIn("`.bot run reloadplans`", DOC_CMD)
        self.assertIn("`.bot run reloadplans`", DOC_RUN)


if __name__ == "__main__":
    unittest.main()
