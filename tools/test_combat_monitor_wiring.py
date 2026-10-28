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
    def test_adapter_registers_all_five_hooks(self):
        for hook in ("UNITHOOK_ON_DAMAGE", "UNITHOOK_ON_HEAL",
                     "UNITHOOK_ON_UNIT_ENTER_COMBAT", "UNITHOOK_ON_UNIT_DEATH",
                     "PLAYERHOOK_ON_SPELL_CAST"):
            self.assertIn(hook, ADAPTER, hook)

    def test_every_hook_body_starts_with_the_enabled_check(self):
        code = strip_comments(ADAPTER)
        for name, cls in (("OnDamage", "CombatMonitorUnitAdapter"), ("OnHeal", "CombatMonitorUnitAdapter"),
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
        payload = body(TELEMETRY, "inline std::string BuildPayload(", "\n}\n")
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


if __name__ == "__main__":
    unittest.main()
