"""A8b: combat monitor hooks run on the core's map threads into a mutex-guarded
service feeding the 1 Hz commander tick; the emitter ships one COMBAT_FIGHT
per finished fight and the bindings carry a live combat window."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ADAPTER = (ROOT / "host" / "CombatMonitorAdapter.cpp").read_text()
ADAPTER_H = (ROOT / "host" / "CombatMonitorAdapter.h").read_text()
SERVICE = (ROOT / "runtime" / "CombatMonitorService.cpp").read_text()
SERVICE_H = (ROOT / "runtime" / "CombatMonitorService.h").read_text()
MODULE = (ROOT / "host" / "Module.cpp").read_text()
CMAKE = (ROOT / "TortoiseBots.cmake").read_text()
COMMANDER = (ROOT / "commands" / "Commander.cpp").read_text()
TELEMETRY = (ROOT / "runtime" / "CommanderTelemetry.h").read_text()
EMITTER = (ROOT / "runtime" / "ObservabilityEmitter.cpp").read_text()
EMITTER_H = (ROOT / "runtime" / "ObservabilityEmitter.h").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


def definition(text, head):
    # The body of the first `head` that opens a brace before any ';' (skips forward declarations).
    i = text.index(head)
    while True:
        rest = text[i:]
        if rest.index("{") < rest.index(";"):
            return rest[:rest.index("\n}\n")]
        i = text.index(head, i + len(head))


def anon_namespace_lines(text):
    # 1-based line numbers inside an anonymous `namespace {` ... `} // namespace` block.
    inside, depth = set(), 0
    for no, line in enumerate(text.splitlines(), 1):
        if line == "namespace {":
            depth += 1
        elif line == "} // namespace":
            depth -= 1
        elif depth:
            inside.add(no)
    return inside


class CombatMonitorWiringTest(unittest.TestCase):
    def test_adapter_registers_all_six_hooks(self):
        for hook in ("UNITHOOK_ON_DAMAGE", "UNITHOOK_ON_HEAL", "UNITHOOK_MODIFY_HEAL_RECEIVED",
                     "UNITHOOK_ON_UNIT_ENTER_COMBAT", "UNITHOOK_ON_UNIT_DEATH",
                     "PLAYERHOOK_ON_SPELL_CAST"):
            self.assertIn(hook, ADAPTER, hook)

    def test_every_hook_body_starts_with_the_enabled_check(self):
        code = strip_comments(ADAPTER)
        for name, cls in (("OnDamage", "CombatMonitorUnitAdapter"), ("OnHeal", "CombatMonitorUnitAdapter"),
                          ("ModifyHealReceived", "CombatMonitorUnitAdapter"),
                          ("OnSpellCast", "CombatMonitorCastAdapter"),
                          ("OnUnitEnterCombat", "CombatMonitorUnitAdapter"),
                          ("OnUnitDeath", "CombatMonitorUnitAdapter")):
            fn = body(code, "%s::%s(" % (cls, name), "\n}\n")
            open_brace = fn.index("{")
            first = fn[open_brace + 1:]
            self.assertIn("Enabled()", first.split(";", 1)[0], name)

    def test_hook_bodies_read_the_core_fields(self):
        code = strip_comments(ADAPTER)
        cast = body(code, "CombatMonitorCastAdapter::OnSpellCast(", "\n}\n")
        self.assertIn("IsTriggered()", cast)
        self.assertLess(cast.index("IsTriggered()"), cast.index("OnCast("))
        heal = body(code, "CombatMonitorUnitAdapter::OnHeal(", "\n}\n")
        self.assertIn("GetMaxHealth()", heal)
        self.assertIn("GetHealth()", heal)
        damage = body(code, "CombatMonitorUnitAdapter::OnDamage(", "\n}\n")
        self.assertIn("OwnerLow(", damage)
        self.assertIn("GetCharmerOrOwnerPlayerOrPlayerItself()", code)

    def test_service_guards_member_hooks_with_the_snapshot_and_the_mutex(self):
        for name in ("OnCast", "OnDamage", "OnHeal", "OnEnterCombat", "OnDeath"):
            fn = body(SERVICE, "::%s(" % name, "\n}\n")
            self.assertIn("m_memberCount", fn, name)
            self.assertIn("atomic_load", fn, name)
            self.assertIn("MaybeMember(", fn, name)
            self.assertLess(fn.index("MaybeMember("), fn.index("atomic_load"), name)
            self.assertTrue("std::lock_guard<std::mutex>" in fn or "std::scoped_lock" in fn, name)
        for name in ("Refresh", "Forget", "SyncConfig"):
            fn = body(SERVICE, "::%s(" % name, "\n}\n")
            self.assertIn("PublishMembers", fn, name)
        for member in re.findall(r"(?:Player|Unit)\s*\*\s*m_\w+|(?:Player|Unit)\s*\*\s*\w+;", SERVICE_H):
            self.fail("service must not keep a game pointer: %s" % member)

    def test_module_lists_both_adapters(self):
        self.assertIn("new CombatMonitorUnitAdapter()", MODULE)
        self.assertIn("new CombatMonitorCastAdapter()", MODULE)
        self.assertIn("host/CombatMonitorAdapter.cpp", CMAKE)
        self.assertIn("runtime/CombatMonitorService.cpp", CMAKE)

    def test_sync_config_is_ungated(self):
        code = strip_comments(SERVICE)
        sync = body(code, "CombatMonitorService::SyncConfig(", "\n}\n")
        self.assertNotIn("Enabled()", sync.split("PublishMembers")[0] if "PublishMembers" in sync else sync)
        self.assertIn("m_enabled.exchange", sync)
        collect = strip_comments(COMMANDER)
        self.assertIn("SyncConfig(sPlayerbotAIConfig.combatMonitor", collect)
        startup = (ROOT / "host" / "BotHostAdapter.cpp").read_text()
        self.assertIn("SyncConfig(sPlayerbotAIConfig.combatMonitor", strip_comments(startup))

    def test_commander_tick_feeds_and_forgets_the_books(self):
        collect = strip_comments(COMMANDER)
        self.assertIn("CombatMonitorService::Instance().Refresh(", collect)
        self.assertIn("CombatMonitorService::Instance().Forget(", collect)
        self.assertIn("ref.combatJson = ", collect)

    def test_commander_syncs_the_config_before_the_enabled_check(self):
        code = strip_comments(COMMANDER)
        for name in ("FillCommanderCombat", "RefreshCombatMonitor"):
            fn = definition(code, "static void %s(" % name)
            self.assertIn("SyncConfig(", fn, name)
            self.assertIn("Enabled()", fn, name)
            self.assertLess(fn.index("SyncConfig("), fn.index("Enabled()"), name)

    def test_combat_glue_declarations_sit_at_bot_commands_scope(self):
        # Declared inside `namespace {` but defined outside it, the functions would never link.
        inside = anon_namespace_lines(COMMANDER)
        lines = COMMANDER.splitlines()
        for name in ("FillCommanderCombat", "RefreshCombatMonitor", "ForgetReleasedCombatGroups"):
            found = [no for no, line in enumerate(lines, 1) if line.startswith("static void %s(" % name)]
            self.assertEqual(len(found), 2, name)
            for no in found:
                self.assertNotIn(no, inside, "%s at line %d" % (name, no))

    def test_telemetry_writes_the_window_raw(self):
        self.assertIn("std::string combatJson", TELEMETRY)
        payload = body(TELEMETRY, "inline std::string BuildPayloadOf(", "\n}\n")
        self.assertIn('combat', payload)
        self.assertIn('<< b.combatJson', payload)
        self.assertIn("combatJson", payload)

    def test_emitter_calls_the_combat_provider_after_commanders(self):
        self.assertIn("SetCombatProvider", EMITTER_H)
        cycle = strip_comments(EMITTER)
        self.assertIn("EmitCommanders()", cycle)
        after = cycle[cycle.index("EmitCommanders()"):]
        self.assertIn("m_combatProvider", after)
        self.assertIn("SendDatagram", after.split("EmitCommanders()", 1)[-1] if "EmitCommanders()" in after else after)

    # Live fight events, and a fight that outlives its run.

    def test_bound_sampler_feeds_fights_without_a_run(self):
        # Every standing binding keeps its combat book: the run tick feeds the
        # ones with a run, and the sampler feeds the rest at 1 Hz.
        code = strip_comments(COMMANDER)
        fill = definition(code, "static void FillCommanderCombat(")
        self.assertNotIn("Forget(", fill, "a standing binding is never forgotten")
        self.assertNotIn("DungeonRuns()", fill, "the sampler, not this snapshot, owns the no-run case")
        update = definition(code, "void UpdateRemoteCommanders(")
        self.assertIn("SampleBoundFights(diff)", update)
        self.assertLess(update.index("UpdateDungeonRuns(diff)"), update.index("SampleBoundFights(diff)"),
                        "the sampler runs after the run ticks that stamp their feed")
        sample = definition(code, "static void SampleBoundFights(")
        self.assertIn("SamplerFeeds(", sample)
        self.assertIn("FeedCombatMonitor(", sample)
        self.assertIn("NoteBoss(", sample)
        # A lead in another group (it left and rejoined elsewhere) is away for
        # this binding: it never feeds the fight, and the grace closes it.
        self.assertIn("LeadUsable(", sample)
        self.assertIn("group ? group->GetId() : 0, binding->groupId", sample)
        self.assertLess(sample.index("LeadUsable("), sample.index(".Overdue("),
                        "the away clock uses the group-matched lead, not any group")
        self.assertLess(sample.index("if (!leadUsable)"), sample.index("FeedCombatMonitor("),
                        "an unusable lead is never fed")
        enabled = sample.index("Enabled()")
        self.assertLess(enabled, sample.index("SamplerFeeds("), "nothing is sampled while the monitor is off")
        self.assertNotIn("DungeonRuns()", sample[:sample.index("SamplerFeeds(")],
                         "the feed decision is the run's stamp, not a run slot")
        for scan in ("GetCreatureListWithEntry", "VisitNearby", "Cell::", "GetPlayers()"):
            self.assertNotIn(scan, sample, scan)
        self.assertIn("ctx, nullptr)", sample, "the sampler hands no engaged facts")
        refresh = definition(code, "static void RefreshCombatMonitor(")
        self.assertIn("s_runFedAt()[commanderRaw]", refresh)
        self.assertLess(refresh.index("s_runFedAt()[commanderRaw]"), refresh.index("FeedCombatMonitor("),
                        "the run's feed is stamped before the sample it feeds")
        self.assertIn("&facts.engagedUnits", refresh, "the run path keeps its own engaged facts")
        sources = sorted((ROOT / "runtime").glob("*.h")) + sorted((ROOT / "runtime").glob("*.cpp"))
        sources.append(ROOT / "commands" / "Commander.cpp")
        for path in sources:
            self.assertNotIn("RefreshNoRun", path.read_text(), path)

    def test_forget_retires_instead_of_dropping(self):
        fn = body(strip_comments(SERVICE), "CombatMonitorService::Forget(", "\n}\n")
        self.assertIn("Close(", fn)
        self.assertIn("m_retired", fn)
        self.assertIn("HasEvents(", fn)

    def test_cast_hook_classifies_by_spell_effects(self):
        cast = body(strip_comments(ADAPTER), "CombatMonitorCastAdapter::OnSpellCast(", "\n}\n")
        for constant in ("SPELL_EFFECT_ATTACK_ME", "SPELL_AURA_MOD_TAUNT", "SPELL_EFFECT_INTERRUPT_CAST"):
            self.assertIn(constant, cast, constant)
        self.assertIn("getUnitTargetGuid()", cast)
        self.assertLess(cast.index("SPELL_EFFECT_ATTACK_ME"), cast.index("OnCast("), "classification first")

    def test_heal_received_credits_only_periodic_ticks(self):
        code = strip_comments(ADAPTER)
        heal = body(code, "CombatMonitorUnitAdapter::ModifyHealReceived(", "\n}\n")
        self.assertIn("SPELL_AURA_PERIODIC_HEAL", heal)
        self.assertIn("SPELL_EFFECT_HEAL", heal)
        self.assertIn("FindCurrentSpellBySpellId", heal)
        self.assertIn("GetCastedTime", heal)
        self.assertIn("OnHeal(", heal)
        self.assertIn("GetMaxHealth()", heal)
        self.assertIn("GetHealth()", heal)
        self.assertLess(heal.index("SPELL_AURA_PERIODIC_HEAL"), heal.index("OnHeal("),
                        "only a periodic-heal spell reaches the books")
        self.assertLess(heal.index("GetCastedTime"), heal.index("OnHeal("),
                        "the direct half of a hybrid cast is skipped before counting")

    def test_event_drain_runs_before_the_snapshot_gate(self):
        code = strip_comments(EMITTER)
        update = definition(code, "void ObservabilityEmitter::Update(")
        self.assertIn("m_combatEventProvider(", update)
        self.assertLess(update.index("m_combatEventProvider("),
                        update.index("if (m_snapshotTimerMs < kSnapshotIntervalMs)"),
                        "the 100 ms drain is not inside the 2 s gate")
        self.assertIn("m_enemyCastPoller", update)

    def test_enemy_cast_poll_has_no_world_scan(self):
        code = strip_comments(COMMANDER)
        fn = definition(code, "void PollEnemyCasts(")
        self.assertIn("AnyInFight()", fn)
        self.assertIn("CurrentSpellId(", fn)
        self.assertIn("NoteEnemyCast(", fn)
        for scan in ("GetCreatureListWithEntry", "VisitNearby", "Cell::"):
            self.assertNotIn(scan, fn, scan)

    def test_conf_dist_carries_the_realtime_keys(self):
        conf = (ROOT / "conf" / "tortoise_bots.conf.dist").read_text()
        self.assertIn("AiPlayerbot.ObservabilityRealtime = 1", conf)
        self.assertIn("AiPlayerbot.ObservabilityRealtime.Events = 1", conf)
        self.assertIn("AiPlayerbot.ObservabilityRealtime.EventGapMs = 250", conf)

    def test_config_reload_sets_the_realtime_flag(self):
        startup = (ROOT / "host" / "BotHostAdapter.cpp").read_text()
        self.assertIn("s_realtimeKeysDue", startup)
        self.assertIn("ReloadRealtimeKeys()", startup)
        self.assertIn("ObservabilityEmitter::Instance().SetCombatEventProvider(", startup)
        self.assertIn("ObservabilityEmitter::Instance().SetEnemyCastPoller(", startup)

    def test_run_slot_erase_leaves_the_open_fight_to_the_sampler(self):
        # A run that ends while its binding stands (`.bot run stop`, the time
        # limit, a switched-off gate) erases its run slot without dropping the
        # group's combat book: the sampler feeds the open fight to its end, so a
        # fight continuing after the stop is still recorded. Only the binding's
        # removal forgets.
        code = strip_comments(COMMANDER)
        erase = body(code, "void EraseRunSlot(", "\n}\n")
        self.assertIn("TakeRunItems(", erase)
        self.assertNotIn("Forget(", erase, "the run slot's erase must not close the open fight")
        release = body(code, "uint32 ReleaseBinding(", "\n}\n")
        self.assertIn("Forget(removed.groupId)", release, "the binding's removal still forgets")


if __name__ == "__main__":
    unittest.main()
