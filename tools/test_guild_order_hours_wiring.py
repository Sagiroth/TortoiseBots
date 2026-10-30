"""Guild order row carries the chosen farm time plus the keep flag.

runtime/GuildOrderStore.cpp appends <hours> and <keep> to TBM:ORDER while
keeping every older field in place; docs/play/commands/addon.md documents
the same shape.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STORE = (ROOT / "runtime/GuildOrderStore.cpp").read_text()
ADDON = (ROOT / "docs/play/commands/addon.md").read_text()


class GuildOrderHoursWiringTest(unittest.TestCase):
    def test_order_line_ends_with_hours(self):
        self.assertIn('"|" + std::to_string(order.hours) + "|" + KeepToken(order.keep)', STORE)

    def test_order_line_keeps_older_fields(self):
        self.assertIn('ReasonToken(order.reason) + "|" + BotCommands::ProtocolSafe(zone) + "|" + difficulty', STORE)

    def test_addon_documents_hours_last(self):
        self.assertIn("`TBM:ORDER\\|<bot>\\|<type>\\|<itemId>\\|<item>\\|<have>\\|<want>\\|<state>\\|<reason>\\|<zone>\\|<difficulty>\\|<hours>\\|<keep>`", ADDON)


if __name__ == "__main__":
    unittest.main()
