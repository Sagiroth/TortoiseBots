"""Guild order request rows log one row per supply leg with the parent attached.

RequestNamedTravelTargetAction::Execute keeps its event name and first two
info columns (travel name, step:item) and adds parent name, order item id and
type token. The per-minute throttle carries a leg key so a sub-order's short
legs (bank, bags, deliver) each log a row inside the same minute. The key also
carries the order identity (parent, order item, type, creation time) so an
identical replacement order on the same bot logs its first row at once.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTION = (ROOT / "ai/playerbot/strategy/actions/ChooseTravelTargetAction.cpp").read_text()


def execute_slice():
    start = ACTION.index("bool RequestNamedTravelTargetAction::Execute(")
    end = ACTION.index("\n}\n", start)
    return ACTION[start:end]


class GuildOrderTelemetryWiringTest(unittest.TestCase):
    def test_key_tracked_per_travel_name(self):
        self.assertIn('"order request leg::" + travelName', execute_slice())
        self.assertIn("OrderRequestKey(", execute_slice())
        self.assertIn("logged->createdAt", execute_slice())

    def test_per_leg_due_check(self):
        self.assertIn("OrderRequestLogDue(", execute_slice())

    def test_parent_token_logged(self):
        self.assertIn("OrderParentToken(", execute_slice())

    def test_single_emitter(self):
        self.assertEqual(ACTION.count('"GuildOrderRequest"'), 1)


if __name__ == "__main__":
    unittest.main()
