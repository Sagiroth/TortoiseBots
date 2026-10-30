"""A6: COMMANDERS carries the run, its last 30 status lines, and the raid
bots' subgroup and away flags. The glue: SendRunLine records every line
change in the slot's notes ring; CollectCommanderTelemetry fills ref.run
from StatusParts and the away set from the bots' distance to the lead.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = (ROOT / "commands" / "Commander.cpp").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class RunTelemetryWiringTest(unittest.TestCase):
    def test_send_run_line_records_the_change(self):
        send = strip_comments(body(COMMANDS, "void SendRunLine(", "\n}\n"))
        self.assertIn("slot.changedAt = ", send)
        self.assertIn("slot.notes.push_back(", send)
        self.assertLess(send.index("slot.notes.push_back("), send.index("IsSystemBinding("))

    def test_collect_telemetry_fills_the_run(self):
        collect = strip_comments(body(COMMANDS, "void CollectCommanderTelemetry(", "\n}\n"))
        self.assertIn("StatusParts(", collect)
        self.assertIn("ref.run.present = true", collect)
        self.assertIn("ref.run.notes", collect)


if __name__ == "__main__":
    unittest.main()
