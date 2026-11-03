"""S3/R3m: the live position report. The emitter sends COMMANDER_POS on its own
clock before the 2 s snapshot gate, only under the realtime master switch and
with the clamped cadence keys; the commands layer fills it from the binding's
own bots and the mobs its last full report listed, without any world scan."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
EMITTER = (ROOT / "runtime" / "ObservabilityEmitter.cpp").read_text()
EMITTER_H = (ROOT / "runtime" / "ObservabilityEmitter.h").read_text()
CADENCE = (ROOT / "runtime" / "RealtimeCadence.h").read_text()
TELEMETRY = (ROOT / "runtime" / "CommanderTelemetry.h").read_text()
COMMANDER = (ROOT / "commands" / "Commander.cpp").read_text()
COMMANDS_H = (ROOT / "commands" / "BotCommands.h").read_text()
ADAPTER = (ROOT / "host" / "BotHostAdapter.cpp").read_text()
CONF = (ROOT / "conf" / "tortoise_bots.conf.dist").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class RealtimeWiringTest(unittest.TestCase):
    def test_position_block_sits_before_the_snapshot_gate(self):
        code = strip_comments(EMITTER)
        update = body(code, "void ObservabilityEmitter::Update(", "m_snapshotTimerMs < kSnapshotIntervalMs")
        self.assertIn("m_positionProvider", update)
        self.assertIn("BuildPosPayload(", update)

    def test_master_switch_gates_the_provider(self):
        code = strip_comments(EMITTER)
        update = body(code, "void ObservabilityEmitter::Update(", "m_snapshotTimerMs < kSnapshotIntervalMs")
        pos = update.index("m_positionProvider")
        head = update[:pos]
        guard = max(head.rfind("m_realtimeOn"), head.rfind("IsEnabled"))
        self.assertGreaterEqual(guard, 0, "the position provider is called only under a switch")
        self.assertLess(pos - guard, 400, "the switch is the condition of the same block")

    def test_keys_are_clamped_on_startup_and_reload(self):
        code = strip_comments(EMITTER)
        for name in ("void ObservabilityEmitter::Initialize()", "void ObservabilityEmitter::ReloadRealtimeKeys()"):
            fn = body(code, name, "\n}\n")
            self.assertIn("AiPlayerbot.ObservabilityRealtime.CombatMs", fn, name)
            self.assertIn("AiPlayerbot.ObservabilityRealtime.IdleMs", fn, name)
            self.assertIn("RealtimeCadence::Clamp(", fn, name)
        self.assertIn("Clamp(int combat, int idle)", CADENCE)
        self.assertIn("IdleSends(Cadence c)", CADENCE)

    def test_feed_advances_both_periods_each_world_tick(self):
        code = strip_comments(EMITTER)
        update = body(code, "void ObservabilityEmitter::Update(", "m_snapshotTimerMs < kSnapshotIntervalMs")
        self.assertIn("m_posClock.Advance(diff)", update, "both periods advance on every world tick")
        start = update.index("m_posClock.Advance(diff)")
        block = update[start:start + 800]
        self.assertIn("due.Any()", block, "the provider runs when either period is due")
        self.assertIn("m_positionProvider(m_positionRefs, due.combat, due.idle)", block,
                      "each period asks for its own groups on the one call")
        # Neither period may read the other's gate: the idle one then fired only on combat ticks (once per 10 s at
        # idle 1000 / combat 500).
        self.assertNotIn("CombatDue", update)
        self.assertNotIn("IdleDue", update)
        self.assertNotIn("CombatDue", CADENCE)
        self.assertNotIn("IdleDue", CADENCE)

    def test_conf_template_carries_both_keys(self):
        self.assertIn("AiPlayerbot.ObservabilityRealtime.CombatMs = 500", CONF)
        self.assertIn("AiPlayerbot.ObservabilityRealtime.IdleMs = 2000", CONF)
        self.assertIn("250..2000", CONF)

    def test_positions_are_wired_from_the_commands_layer(self):
        self.assertIn("SetPositionProvider(", EMITTER_H)
        self.assertIn("BotCommands::CollectCommanderPositions(", ADAPTER)
        self.assertIn("CollectCommanderPositions(std::vector<CommanderTelemetry::PosRef>& out, bool wantCombat, bool wantIdle)",
                      COMMANDS_H)

    def test_collector_selects_each_periods_groups(self):
        code = strip_comments(COMMANDER)
        fn = body(code, "void CollectCommanderPositions(", "\n}\n")
        self.assertIn("combat ? !wantCombat : !wantIdle", fn,
                      "a fighting group on the combat period, a standing one on the idle period")

    def test_collector_returns_on_no_bindings(self):
        code = strip_comments(COMMANDER)
        fn = body(code, "void CollectCommanderPositions(", "\n}\n")
        self.assertIn("out.clear();", fn)
        self.assertLess(fn.index("out.clear();"), fn.index("CommanderBindings().Empty()"))
        self.assertLess(fn.index("CommanderBindings().Empty()"), fn.index("CommanderBindings().Commanders()"))

    def test_collector_does_no_world_scan(self):
        code = strip_comments(COMMANDER)
        fn = body(code, "void CollectCommanderPositions(", "\n}\n")
        for scan in ("GetCreatureListWithEntry", "VisitNearby", "Cell::", "GetPlayers()"):
            self.assertNotIn(scan, fn, scan)
        self.assertIn("s_mobWatch()", fn, "the mobs are the ones the last full report listed")
        self.assertIn("->GetUnit(", fn, "each one resolved by guid on the lead's map")

    def test_position_mobs_carry_no_facing(self):
        # The daemon's COMMANDER_POS mob row is id/x/y/hp/v; a facing here would be a field its model never reads. A bot's
        # row does carry one (PosBot.o), so only the mob read may be gone.
        code = strip_comments(COMMANDER)
        fn = body(code, "void CollectCommanderPositions(", "\n}\n")
        self.assertIn("pos.o = bot->GetOrientation();", fn, "a bot row keeps its facing")
        self.assertNotIn("mob.o", fn)
        self.assertNotIn("mob.hasO", fn)

        pos = body(strip_comments(TELEMETRY), "inline std::string BuildPosPayload(", "\n}\n")
        self.assertNotIn("hasO", pos)
        self.assertNotIn(',\\"o\\":', pos.split('\\"mobs\\":', 1)[1], "the mob rows carry no facing")

    def test_builder_caps_and_budget_are_named(self):
        self.assertIn("kMaxPosBots = 40", TELEMETRY)
        self.assertIn("kMaxPosMobs = 24", TELEMETRY)
        self.assertIn("kPosByteBudget = 16000", TELEMETRY)
        self.assertIn("kMaxPosBindings = 20", TELEMETRY)
        pos = body(strip_comments(TELEMETRY), "inline std::string BuildPosPayload(", "\n}\n")
        self.assertIn("kMaxPosBots", pos)
        self.assertIn("kMaxPosMobs", pos)
        self.assertIn("kPosByteBudget", pos)


if __name__ == "__main__":
    unittest.main()
