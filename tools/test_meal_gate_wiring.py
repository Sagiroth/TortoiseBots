"""Spec 04: meals wait while the run fights, wherever the bot stands.

The run leader stamps eating and drinking bots once a second; the gate, the
wake check and the `.bot action rest` refusal all read the same mark.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ITEMS = (ROOT / "ai/playerbot/strategy/actions/UseItemAction.h").read_text()
PLAYERBOT_AI = (ROOT / "ai/playerbot/PlayerbotAI.cpp").read_text()
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()
ACTION = (ROOT / "commands/Action.cpp").read_text()

MARK = '"run fight until ms"'


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def span_after(text, marker, start=0):
    """From the marker through the closing brace of its block."""
    i = text.index(marker, start)
    brace = text.index("{", i)
    depth = 0
    for j in range(brace, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i:j]
    return text[i:]


def body_after(text, marker, start=0):
    i = text.index(marker, start)
    i = text.index("{", i)
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i:j]
    return text[i:]


SRC_ITEMS = strip_comments(ITEMS)
SRC_AI = strip_comments(PLAYERBOT_AI)
SRC_COMMANDER = strip_comments(COMMANDER)
SRC_ACTION = strip_comments(ACTION)


class MealGateWiringTest(unittest.TestCase):
    def test_group_is_fighting_reads_the_run_mark(self):
        body = body_after(SRC_ITEMS, "inline bool GroupIsFighting")
        self.assertIn("MealBlocked(", body)
        self.assertIn(MARK, body)

    def test_policy_header_included_by_every_reader(self):
        for name, text in (
            ("UseItemAction.h", SRC_ITEMS),
            ("PlayerbotAI.cpp", SRC_AI),
            ("Commander.cpp", SRC_COMMANDER),
        ):
            self.assertIn("playerbot/policy/combat/MealGatePolicy.h", text, name)

    def test_wake_check_stands_a_seated_bot_for_the_run(self):
        body = span_after(SRC_AI, "if (aiInternalUpdateDelay > 0 && !isWaiting")
        flat = " ".join(body.split())
        self.assertIn(MARK, flat)
        # The member test and the run's mark are alternatives: `&&` would
        # never wake a bot the member test cannot see.
        self.assertRegex(flat, r"IsGroupMemberNearInCombat\(\) \|\| RunFightHolds\(")

    def test_run_tick_marks_while_the_run_fights_and_clears_when_it_ends(self):
        after_monitor = SRC_COMMANDER.index("RefreshCombatMonitor(commanderRaw")
        step_before = SRC_COMMANDER.index("std::size_t const stepBeforeTick", after_monitor)
        region = SRC_COMMANDER[after_monitor:step_before]
        flat = " ".join(region.split())
        self.assertEqual(flat.count("if (facts.anyInCombat)"), 1,
            "the mark must be gated on the run fighting")
        # The stamp is the hold ahead of now, marked with `| 1` so a computed
        # 0 never reads as "never set".
        self.assertIn(
            "if (facts.anyInCombat) { uint32 const until = WorldTimer::getMSTime() + ai::RUN_FIGHT_HOLD_MS;",
            flat)
        self.assertIn('->GetValue<int32>("manual int", "run fight until ms")->Set(int32(until | 1));', flat)
        # The tick that sees no fight clears the same bots at once: the run's
        # own rest order goes out on the next tick and the live hold would
        # still refuse it.
        else_flat = flat[flat.index("} else"):]
        self.assertIn("} else for (Player* bot : bots)", else_flat)
        self.assertIn('->GetValue<int32>("manual int", "run fight until ms")->Set(0);', else_flat)

    def test_rest_refusal_names_the_same_gate(self):
        self.assertIn("in.groupFighting = GroupIsFighting(ai);", SRC_ACTION)
        self.assertNotIn("IsGroupMemberNearInCombat", SRC_ACTION)


if __name__ == "__main__":
    unittest.main()
