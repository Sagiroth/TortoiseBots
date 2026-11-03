#!/usr/bin/env python3
"""Structural checks for the run trace's glue (PR #927 review findings).

The record layer, the run lifecycle and the permission rules are tested as
behaviour in tools/test_run_trace_records.cpp, and the writer (end marker,
stalled disk, shutdown deadline) in tools/test_run_trace_writer.cpp. The glue
in runtime/RunTrace.cpp, commands/Commander.cpp and the outcome sites in
Engine::DoNextAction need the game headers (Player, ObjectAccessor, Group),
and there is no seam to drive RunTraceService without the core, so these
checks pin how the glue calls the tested parts:

- Sample() replaces the hook scope before it looks at an empty frame, so a
  requester who logs out stops the action records at the next frame, not at
  the scope_lost timeout.
- ResolveBots() picks its source with PickScopeSource(): a party run whose
  group is gone has no bots; it does not shrink to the anchor bot.
- Ending a run never waits for the disk: Finish() only queues the end marker;
  Update() polls the writer and detaches the listeners; Shutdown() lets a busy
  writer go, and Initialize() (server start) marks the summaries of runs that
  were never finalised "incomplete". A failed end is logged and shown by
  Status().
- The command glue asks MayRecordBot (with the commander binding),
  MayStartTrace (GmOnly) and MaySeeRunDetail.
- Every NoteOutcome site in the engine walk reports to the listeners on the
  very next statement, and every early exit reports or re-queues within its
  own block. The "vetoed earlier this tick" branch only zeroes the relevance
  and falls through to the ordinary result sites (Vetoed at the IMPOSSIBLE
  site), so it reports nothing itself; a report there would count it twice.

Run: python3 tools/test_run_trace_glue.py
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RUN_TRACE = ROOT / "runtime/RunTrace.cpp"
ENGINE = ROOT / "ai/playerbot/strategy/Engine.cpp"
COMMANDS = ROOT / "commands/Commander.cpp"
VETO_EARLIER = "if (std::find(vetoedActions.begin(), vetoedActions.end(), action) != vetoedActions.end())"


def function_body(src, signature):
    start = src.index(signature)
    brace = src.index("{", start)
    depth = 0
    for i in range(brace, len(src)):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[brace:i + 1]
    raise AssertionError("unbalanced braces in " + signature)


def code_lines(text):
    """Non-empty lines that are not comments or preprocessor lines."""
    out = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("//") or stripped.startswith("#"):
            continue
        out.append(stripped)
    return out


def enclosing_block(lines, index):
    """The lines of the innermost brace block that contains lines[index]."""
    depth = 0
    for i in range(index - 1, -1, -1):
        depth += lines[i].count("}") - lines[i].count("{")
        if depth < 0:
            return lines[i + 1:index]
    return lines[:index]


class SampleScopeTest(unittest.TestCase):
    def setUp(self):
        self.body = function_body(RUN_TRACE.read_text(), "void RunTraceService::Sample()")

    def test_scope_replaced_before_empty_check(self):
        replace = self.body.find("scopeGuids_.Replace(")
        empty = self.body.find("if (bots.empty())")
        self.assertNotEqual(replace, -1, "Sample() must replace the hook scope every frame")
        self.assertNotEqual(empty, -1)
        self.assertLess(replace, empty, "an empty frame must empty the hook scope before returning")

    def test_scope_not_filled_only_on_the_populated_path(self):
        self.assertNotIn("scopeGuids_.insert(", self.body)
        self.assertEqual(self.body.count("scopeGuids_."), 1)

    def test_attached_bots_are_remembered_for_detach(self):
        attach = self.body.find("AttachListeners(ai);")
        self.assertNotEqual(attach, -1)
        self.assertIn("attachedGuids_.insert(guid);", self.body[attach:attach + 120])


class ResolveBotsTest(unittest.TestCase):
    def setUp(self):
        self.body = function_body(RUN_TRACE.read_text(), "void RunTraceService::ResolveBots(")

    def test_source_comes_from_the_pure_policy(self):
        self.assertIn("RunTrace::PickScopeSource(wholeParty_,", self.body)
        self.assertIn("source == RunTrace::ScopeSource::Group", self.body)
        self.assertIn("source == RunTrace::ScopeSource::Anchor", self.body)

    def test_no_anchor_fallback_outside_the_policy(self):
        self.assertIsNone(re.search(r"else\s+if\s*\(\s*BotManager::Instance\(\)\.IsControllableBot\(anchor\)", self.body))


class EndingNeverWaitsTest(unittest.TestCase):
    WAITS = ("WaitFinished(", "writer_.Close(", "WriteFileAtomic(", "Drain(", ".join(", "sleep_for(")

    def setUp(self):
        self.src = RUN_TRACE.read_text()

    def test_finish_only_queues_the_end_marker(self):
        body = function_body(self.src, "void RunTraceService::Finish(")
        self.assertIn("writer_.PushEnd(", body)
        self.assertIn("lifecycle_.BeginFinish()", body)
        self.assertIn("detachPending_ = true;", body)
        for wait in self.WAITS:
            self.assertNotIn(wait, body, wait)
        # The job owns copies: no `this` capture reaches the writer thread.
        self.assertNotIn("[this", body)
        self.assertNotIn("[&", body)

    def test_stop_and_update_do_not_wait(self):
        for signature in ("bool RunTraceService::Stop(", "void RunTraceService::Update("):
            body = function_body(self.src, signature)
            for wait in self.WAITS:
                self.assertNotIn(wait, body, signature + " " + wait)

    def test_update_polls_the_writer_and_detaches(self):
        body = function_body(self.src, "void RunTraceService::Update(")
        self.assertIn("writer_.Finished()", body)
        self.assertIn("CompleteFinish();", body)
        self.assertIn("DetachAllListeners()", body)
        self.assertLess(body.index("DetachAllListeners()"), body.index("if (!Active())"),
                        "the listeners are detached on the tick after a run ends, active or not")

    def test_detach_only_from_update(self):
        calls = [m.start() for m in re.finditer(r"\bDetachAllListeners\(\);", self.src)]
        update = function_body(self.src, "void RunTraceService::Update(")
        start = self.src.index(update)
        self.assertEqual(len(calls), 1)
        self.assertTrue(start <= calls[0] < start + len(update))

    def test_shutdown_does_not_wait(self):
        body = function_body(self.src, "void RunTraceService::Shutdown(")
        for wait in self.WAITS:
            self.assertNotIn(wait, body, wait)
        self.assertIn("writer_.Detach();", body)
        self.assertNotIn("WaitFinished(", self.src, "the server never waits for the writer")
        self.assertNotIn("kShutdownFlushMs", self.src)

    def test_start_of_the_server_reconciles_interrupted_runs(self):
        body = function_body(self.src, "void RunTraceService::Initialize(")
        self.assertIn("RunTrace::ReconcileStaleSummaries(DirPath(),", body)
        host = (ROOT / "host/BotHostAdapter.cpp").read_text()
        startup = function_body(host, "void BotHostAdapter::OnStartup(")
        self.assertIn("RunTraceService::Instance().Initialize();", startup)
        update = function_body(host, "void BotHostAdapter::OnUpdate(")
        self.assertNotIn("RunTraceService::Instance().Initialize();", update)

    def test_a_failed_end_is_logged_and_shown(self):
        complete = function_body(self.src, "void RunTraceService::CompleteFinish(")
        self.assertIn("lastFailure_ = writer_.Failure();", complete)
        self.assertIn("failed: %s", complete)
        status = function_body(self.src, "std::string RunTraceService::Status(")
        self.assertIn("lastFailure_", status)
        finish = function_body(self.src, "void RunTraceService::Finish(")
        self.assertIn('failure.empty() ? endStatus.c_str() : "failed"', finish)

    def test_start_refuses_while_finishing(self):
        body = function_body(self.src, "bool RunTraceService::Start(")
        self.assertIn("lifecycle_.StartRefusal()", body)
        self.assertIn("lifecycle_.Start();", body)
        self.assertNotIn("WriteFileAtomic(", body, "the running summary goes through the writer")
        self.assertIn("writer_.PushFile(summaryPath_", body)


class AiOffTest(unittest.TestCase):
    def test_host_ticks_the_trace_with_the_ai_off(self):
        host = (ROOT / "host/BotHostAdapter.cpp").read_text()
        update = function_body(host, "void BotHostAdapter::OnUpdate(")
        at = update.index("if (!sPlayerbotAIConfig.enabled)\n    {")
        branch = function_body(update[at:], "if (!sPlayerbotAIConfig.enabled)")
        self.assertIn("RunTraceService::Instance().Update(diff);", branch)
        self.assertLess(branch.index("RunTraceService::Instance().Update(diff);"), branch.index("return;"))

    def test_update_ends_a_run_when_either_switch_is_off(self):
        body = function_body(RUN_TRACE.read_text(), "void RunTraceService::Update(")
        check = body.index("RunTrace::TraceOffReason(sPlayerbotAIConfig.enabled, sPlayerbotAIConfig.runTrace)")
        self.assertLess(body.index("if (!Active())"), check, "the off cost stays one atomic load")
        self.assertIn('Finish("disabled", "complete");', body[check:check + 200])

    def test_start_is_refused_with_the_ai_off(self):
        start = function_body(RUN_TRACE.read_text(), "bool RunTraceService::Start(")
        self.assertIn("RunTrace::TraceOffReason(sPlayerbotAIConfig.enabled, sPlayerbotAIConfig.runTrace)", start)
        handler = function_body(COMMANDS.read_text(), "bool HandleTrace(")
        self.assertIn("RunTrace::TraceOffReason(sPlayerbotAIConfig.enabled, sPlayerbotAIConfig.runTrace)", handler)
        self.assertNotIn("if (!sPlayerbotAIConfig.runTrace)", handler)


class CommandGlueTest(unittest.TestCase):
    def setUp(self):
        self.src = COMMANDS.read_text()

    def test_may_trace_bot_honours_the_commander_binding(self):
        body = function_body(self.src, "static bool MayTraceBot(")
        self.assertIn("RunTrace::MayRecordBot(", body)
        self.assertIn("CommandsBotsGroup(requester, bot)", body)
        binding = function_body(self.src, "static bool CommandsBotsGroup(")
        self.assertIn("CommanderBindings().FindByCommander(", binding)
        self.assertIn("binding->leadGuid", binding)

    def test_party_start_uses_the_frame_rule(self):
        body = function_body(self.src, "bool HandleTrace(")
        party = body[body.index('else if (scope == "party")'):]
        self.assertIn("RunTrace::MayRecordBot(admin, CanControlBot(requester, record), commanded)", party)
        self.assertNotIn("!commanded && !CanControlBot(", body, "a GameMaster may trace any party")

    def test_start_and_status_use_the_policies(self):
        body = function_body(self.src, "bool HandleTrace(")
        self.assertIn("RunTrace::MayStartTrace(sPlayerbotAIConfig.runTraceGmOnly, admin)", body)
        self.assertIn("RunTrace::MaySeeRunDetail(admin,", body)
        self.assertIn("service.Status(detail)", body)
        self.assertLess(body.index("RunTrace::MayStartTrace("), body.index("service.Start(params, message)"))

    def test_stop_checks_the_run_id_before_stopping(self):
        body = function_body(self.src, "bool HandleTrace(")
        stop = body[body.index('if (verb == "stop")'):]
        self.assertIn("RunTrace::StopRunIdRefusal(", stop)
        self.assertLess(stop.index("RunTrace::StopRunIdRefusal("), stop.index("service.Stop("))


class EngineOutcomeSitesTest(unittest.TestCase):
    def setUp(self):
        self.walk = function_body(ENGINE.read_text(), "bool Engine::DoNextAction(")
        self.lines = code_lines(self.walk)

    def test_vetoed_earlier_branch_falls_through(self):
        at = self.walk.index(VETO_EARLIER)
        branch = function_body(self.walk[at:], VETO_EARLIER)
        self.assertIn("relevance = 0.0f;", branch)
        for word in ("continue;", "break;", "delete actionNode", "return"):
            self.assertNotIn(word, branch, "a vetoed-earlier instance must reach a result site")
        self.assertNotIn("actionExecutionListeners.Outcome(", branch,
                         "the result site reports it; a report here would count it twice")

    def test_impossible_site_reports_vetoed_for_a_zeroed_possible_action(self):
        self.assertIn("isPossible ? ActionOutcome::Vetoed : ActionOutcome::Impossible", self.walk)

    def test_outcome_sits_right_at_each_note_outcome(self):
        sites = [i for i, line in enumerate(self.lines) if line.startswith("NoteOutcome(action,")]
        self.assertEqual(len(sites), 7)  # vetoed-earlier, backoff, prereq, ok, failed, impossible, useless
        veto = self.lines.index(VETO_EARLIER)
        reported = 0
        for i in sites:
            nxt = self.lines[i + 1]
            if veto < i and "}" not in self.lines[veto + 2:i]:
                self.assertFalse(nxt.startswith("actionExecutionListeners.Outcome("))
                continue
            self.assertTrue(nxt.startswith("actionExecutionListeners.Outcome(action, "),
                            "no listener report right after: " + self.lines[i])
            reported += 1
        self.assertEqual(reported, 6)

    def test_every_early_exit_reports_or_requeues_in_its_block(self):
        exits = [i for i, line in enumerate(self.lines) if line == "continue;"]
        self.assertGreaterEqual(len(exits), 3)
        for i in exits:
            block = "\n".join(enclosing_block(self.lines, i))
            self.assertTrue("actionExecutionListeners.Outcome(" in block or "PushAgain(" in block,
                            "continue without an outcome or a re-queue in its block: " + block[:200])

    def test_each_result_site_reports_once(self):
        for outcome in ("Backoff", "Prereq", "Ok", "Failed", "Useless"):
            self.assertEqual(self.walk.count("ActionOutcome::%s," % outcome), 1, outcome)
        self.assertEqual(self.walk.count("ActionOutcome::Vetoed"), 1)


class SummaryFieldsGlueTest(unittest.TestCase):
    """Issue #986: the digest of the summary fields is fed by events the recorder already sees."""

    def setUp(self):
        self.src = RUN_TRACE.read_text()
        self.cmd = COMMANDS.read_text()
        self.sample = function_body(self.src, "void RunTraceService::Sample()")

    def test_sample_feeds_deaths_and_wipes_from_the_death_edge(self):
        self.assertIn("digest_.NoteDeath(track.lastDeath);", self.sample)
        self.assertIn("digest_.NoteFrame(elapsedMs_, members);", self.sample)
        # the death is taken at the edge where the existing death event is written
        edge = self.sample.index("track.alive && !alive")
        self.assertLess(edge, self.sample.index("digest_.NoteDeath("))
        self.assertLess(self.sample.index("digest_.NoteDeath("), self.sample.index('e.ev = "death";'))

    def test_boss_kills_are_found_by_guid_not_by_a_scan(self):
        body = function_body(self.src, "void RunTraceService::CheckBossKills(")
        self.assertIn("map->GetUnit(ObjectGuid(it->first))", body)
        self.assertNotIn("bots.front()->GetMap()", body)
        self.assertIn("digest_.NoteBossKill(", body)
        for scan in ("VisitAll", "CellImpl", "Searcher", "GetCreatureListWithEntryInGrid"):
            self.assertNotIn(scan, body, scan)
        self.assertIn("CheckBossKills(bots);", self.sample)

    def test_nothing_in_the_recorder_touches_the_database(self):
        for needle in ("CharacterDatabase", "WorldDatabase", "LoginDatabase", "PExecute", "PQuery"):
            self.assertNotIn(needle, self.src, needle)

    def test_context_is_read_at_start_once_a_second_and_at_the_end(self):
        start = function_body(self.src, "bool RunTraceService::Start(")
        self.assertIn("digest_.SetDriver(params.driver);", start)
        self.assertIn("context_ = params.context;", start)
        self.assertIn("RefreshContext();", start)
        update = function_body(self.src, "void RunTraceService::Update(")
        self.assertLess(update.index("checkAccumMs_ >= 1000"), update.index("RefreshContext();"))
        self.assertIn("RefreshContext();", function_body(self.src, "void RunTraceService::Finish("))

    def test_summary_is_rendered_once_and_handed_to_the_writer_as_text(self):
        body = function_body(self.src, "void RunTraceService::Finish(")
        self.assertEqual(body.count("digest_.Render("), 1)
        self.assertIn("RenderArgs{ true,", body)
        self.assertIn("traceFile, digest]", body)
        self.assertIn("failure, digest)", body)
        # the running summary written at start carries the fields too
        start = function_body(self.src, "bool RunTraceService::Start(")
        # ... but as a live run: no outcome, bosses or clear_s until it ends
        self.assertIn("RenderArgs{ false,", start)

    def test_command_takes_driver_and_passes_the_context(self):
        body = function_body(self.cmd, "bool HandleTrace(")
        self.assertIn("RunTrace::IsDriverToken(parts[i])", body)
        self.assertIn("RunTrace::DriverRefusal(value)", body)
        self.assertIn("params.driver = driver;", body)
        self.assertIn("params.context = &TraceContext;", body)
        ctx = function_body(self.cmd, "static bool TraceContext(")
        self.assertIn("RunTrace::RunMode::RunLeader", ctx)
        self.assertIn("RunTrace::PlanBossEntries(", ctx)
        self.assertIn("PlansOnMap(", ctx)
        self.assertIn("DungeonRun::Current(", ctx)
        self.assertIn("out.knownBosses.push_back(entry)", ctx)


if __name__ == "__main__":
    unittest.main()
