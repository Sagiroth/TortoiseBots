"""The gate split: the run leader needs only DungeonSelfClear (default on);
RemoteCommander gates only the outside commander protocol.

The policy (runtime/RemoteCommanderPolicy.h Feature gates) decides; these
checks pin that the glue, config, template and docs hand it the gates.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = "\n".join((ROOT / "commands" / n).read_text() for n in ("Commander.cpp", "BotCommands.cpp", "Info.cpp"))
COMMANDER = (ROOT / "commands" / "Commander.cpp").read_text()
BOTCOMMANDS = (ROOT / "commands" / "BotCommands.cpp").read_text()
INFO = (ROOT / "commands" / "Info.cpp").read_text()
SHARED = (ROOT / "commands" / "CommandShared.cpp").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class GateSplitWiringTest(unittest.TestCase):
    def test_command_surfaces(self):
        self.assertIn(
            "static bool CommanderVisible() { return RemoteCommander::CommanderSurfaceOn(CurrentCommanderGates()); }",
            BOTCOMMANDS)
        self.assertIn(
            "static bool RunVisible() { return RemoteCommander::RunSurfaceOn(CurrentCommanderGates()); }",
            BOTCOMMANDS)
        self.assertIn(
            "bool const dungeonRun = RemoteCommander::RunSurfaceOn(CurrentCommanderGates());",
            INFO)

    def test_single_gate_reader(self):
        for name, text in (("Commander.cpp", COMMANDER), ("BotCommands.cpp", BOTCOMMANDS), ("Info.cpp", INFO)):
            bare = [line for line in strip_comments(text).splitlines()
                    if "sPlayerbotAIConfig.remoteCommander" in line and "remoteCommanderViewMs" not in line]
            self.assertEqual(bare, [], name)
        self.assertEqual(strip_comments(SHARED).count("sPlayerbotAIConfig.remoteCommander"), 1)

    def test_binding_state_uses_kind_gate(self):
        state = body(strip_comments(COMMANDER), "RemoteCommander::BindingState ReadBindingState(", "\n}\n")
        self.assertIn(
            "state.enabled = RemoteCommander::BindingKindEnabled(RemoteCommander::KindOf(binding), CurrentCommanderGates());",
            state)

    def test_bind_gate(self):
        bind = body(strip_comments(COMMANDER), "static bool HandleCommanderBind(", "\n}\n")
        self.assertIn(
            "facts.inPartyAllowed = RemoteCommander::BindingKindEnabled(RemoteCommander::BindingKind::InParty, gates);",
            bind)
        self.assertIn(
            "facts.commanderIsBot = BotManager::Instance().IsBot(commander->GetObjectGuid());",
            bind)

    def test_commander_gate(self):
        cmd = body(strip_comments(COMMANDER), "bool HandleCommander(", "\n}\n")
        self.assertIn("RemoteCommander::CommanderSurfaceOn(", cmd)
        self.assertIn("RemoteCommander::ViewAllowed(", cmd)

    def test_order_gate_after_standing_binding(self):
        order = body(strip_comments(COMMANDER), "bool HandleCommanderOrder(", "\n}\n")
        gate = "RemoteCommander::OrderAllowed(RemoteCommander::KindOf(*binding), gates, s_runIssuing, verb)"
        self.assertGreater(
            order.index(gate),
            order.index("StandingBinding(order.commander->GetObjectGuid().GetRawValue())"))
        self.assertLess(order.index(gate), order.index("RemoteCommander::SplitOrderArgs(rest)"))

    def test_switched_off_sweep_per_run(self):
        update = body(strip_comments(COMMANDER), "static void UpdateDungeonRuns(", "\n}\n")
        self.assertIn("if (RemoteCommander::RunAllowed(kind, gates))", update)
        self.assertIn("RemoteCommander::RunSwitchedOffNote(kind, gates)", update)

    def test_player_led_bind_gate(self):
        bind = body(strip_comments(COMMANDER), "static bool BindPlayerLed(", "\n}\n")
        self.assertIn("RemoteCommander::BindingKindEnabled(RemoteCommander::BindingKind::PlayerLed,", bind)

    def test_run_gate(self):
        run = body(strip_comments(COMMANDER), "static bool RunVerbAs(ChatHandler* handler, Player* commander, std::string const& verb, std::string const& rawArg,\n    std::string const& intent)\n{", "\n}\n")
        self.assertIn("RemoteCommander::RunSurfaceOn(", run)

    def test_config_keys(self):
        keys = body(CONFIG, "void PlayerbotAIConfig::LoadDungeonRunKeys()", "\n}\n")
        self.assertIn('dungeonSelfClear = config.GetBoolDefault("AiPlayerbot.DungeonSelfClear", true);', keys)
        self.assertIn('botLfgGroups = config.GetBoolDefault("AiPlayerbot.BotLfgGroups", false);', keys)
        self.assertIn(
            "AiPlayerbot.BotLfgGroups = 1 needs AiPlayerbot.DungeonSelfClear = 1: bot LFG groups stay dormant.",
            keys)

    def test_template(self):
        self.assertIn("AiPlayerbot.DungeonSelfClear = 1", TEMPLATE)
        self.assertIn("AiPlayerbot.BotLfgGroups = 0", TEMPLATE)
        self.assertIn("#AiPlayerbot.RemoteCommander = 0", TEMPLATE)
        self.assertNotIn("Needs AiPlayerbot.RemoteCommander = 1 and a binding", TEMPLATE)


if __name__ == "__main__":
    unittest.main()
