"""Guild order row carries the chosen farm time, keep flag, parent and supply status.

runtime/GuildOrderStore.cpp appends <hours>, <keep>, <parent>, gold cap, step and
need to TBM:ORDER while keeping every older field in place; the addon docs
document the same shape. A give with no hours token stores no time limit.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STORE = (ROOT / "runtime/GuildOrderStore.cpp").read_text()
ADDON = (ROOT / "docs/play/commands/addon.md").read_text()


class GuildOrderHoursWiringTest(unittest.TestCase):
    def test_order_line_ends_with_hours(self):
        self.assertIn('"|" + HoursToken(order.hours) + "|" + KeepToken(order.keep) + "|"', STORE)

    def test_order_line_appends_parent(self):
        self.assertIn('BotCommands::ProtocolSafe(order.parentBotGuidLow ? BotName(order.parentBotGuidLow) : "-")', STORE)

    def test_order_line_keeps_older_fields(self):
        self.assertIn('ReasonToken(order.reason) + "|" + BotCommands::ProtocolSafe(zone) + "|" + difficulty', STORE)

    def test_addon_documents_hours_last(self):
        self.assertIn("`TBM:ORDER\\|<bot>\\|<type>\\|<itemId>\\|<item>\\|<have>\\|<want>\\|<state>\\|<reason>\\|<zone>\\|<difficulty>\\|<hours>\\|<keep>\\|<parent>\\|<goldCap>\\|<step>\\|<need>`", ADDON)

    def test_addon_documents_inf_hours(self):
        self.assertIn("`inf`", ADDON)

    def test_no_hours_give_defaults_to_unlimited(self):
        give = (ROOT / "commands/Order.cpp").read_text()
        self.assertIn("uint32_t hours = GuildOrders::kDefaultOrderHours;", give)
        policy = (ROOT / "runtime/GuildOrderGiveUpPolicy.h").read_text()
        self.assertIn("kDefaultOrderHours = kUnlimitedOrderHours", policy)

    def test_command_doc_documents_no_limit_default(self):
        commands = (ROOT / "docs/play/commands/roster-and-hiring.md").read_text()
        self.assertIn("no hours given (or `inf`) means no time limit", commands)


if __name__ == "__main__":
    unittest.main()
