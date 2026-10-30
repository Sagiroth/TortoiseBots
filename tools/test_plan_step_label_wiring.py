"""A plan step row's `comment` is the dashboard's step label.

`ai_dungeon_step`.`comment` is the row's own short description. The loader must read it (the last SELECT field) into
`PlanStep::label`, which `DungeonPlanPolicy.h` clips to kPlanMaxLabel, and the run's telemetry must carry the current step's
label so the card's run line can name the step in words.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LOADER = (ROOT / "ai/playerbot/DungeonPlanMgr.cpp").read_text()
POLICY = (ROOT / "ai/playerbot/DungeonPlanPolicy.h").read_text()
COMMANDS = (ROOT / "commands/Commander.cpp").read_text()


class PlanStepLabelWiringTest(unittest.TestCase):
    def test_loader_selects_the_comment_and_sets_the_label(self):
        self.assertIn("`timeout_s`, `entries`, `comment` FROM `ai_dungeon_step`", LOADER)
        self.assertIn("step.label = fields[14].GetCppString();", LOADER)

    def test_the_label_is_clipped_on_load(self):
        self.assertIn("step.label = PlanStepLabel(step.label);", POLICY)

    def test_telemetry_carries_the_current_steps_label(self):
        self.assertIn("ref.run.label = step->label;", COMMANDS)


if __name__ == "__main__":
    unittest.main()
