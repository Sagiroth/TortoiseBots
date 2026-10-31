import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "ai/playerbot/strategy/actions/ChooseTravelTargetAction.cpp"


class ChooseTravelAppliesTargetWiring(unittest.TestCase):
    def test_found_pick_is_applied_to_the_bot(self):
        # A found pick lives in a local TravelTarget; without this call the bot
        # never gets it, re-requests every tick and never travels.
        text = SRC.read_text()
        body = re.search(r"bool ChooseTravelTargetAction::Execute\(Event& event\)\n\{(.*?)\n\}\n", text, re.S).group(1)
        tail = body[body.rindex("TravelPickOutcome::NotFound"):]
        self.assertIn("setNewTarget(requester, &newTarget, travelTarget);", tail)


if __name__ == "__main__":
    unittest.main()
