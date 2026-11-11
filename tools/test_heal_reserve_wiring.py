"""Wiring pins for the healer top-up reserve (DungeonRunHealReserve).

The pure rules live in ai/playerbot/HealReservePolicy.h and are tested in
tools/test_heal_reserve_policy.cpp. These pins hold the call sites: both top-up
gates ask HealTopUpAllowedFor, the trigger's old MediumMana line is gone, and the
reader that reads the key is the gate, never the row read.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY = (ROOT / "ai/playerbot/HealReservePolicy.h").read_text()
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/HealthTriggers.cpp").read_text()
DRUID = (ROOT / "ai/playerbot/strategy/druid/DruidActions.cpp").read_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


class HealReserveWiring(unittest.TestCase):
    def test_the_top_up_trigger_asks_the_rule_and_not_medium_mana(self):
        fn = body(TRIGGERS, "bool PartyMemberAlmostFullHealthTrigger::IsActive()")
        self.assertIn("HealTopUpAllowedFor(ai, bot, target)", fn)
        self.assertNotIn("mediumMana", fn)
        self.assertNotIn('AI_VALUE2(uint8, "mana"', fn)

    def test_the_rejuvenation_spread_asks_the_rule(self):
        pick = body(DRUID, "Unit* ai::FindRejuvenationSpreadTarget(PlayerbotAI* ai, Player* bot)")
        self.assertIn("HealTopUpAllowedFor(ai, bot, member)", pick)
        useful = body(DRUID, "bool CastRejuvenationSpreadAction::isUseful()")
        self.assertIn("HealTopUpAllowedFor(ai, bot, target)", useful)
        self.assertNotIn("mediumMana", useful)

    def test_the_gate_reads_the_key_before_the_row_read(self):
        fn = body(ACTIONS, "bool ai::HealTopUpAllowedFor(PlayerbotAI* ai, Player* bot, Unit* target)")
        self.assertLess(fn.index("dungeonRunHealReserve"), fn.index("BossHealReserve("))
        # Key off: today's rule and nothing else is read.
        self.assertIn("manaPct >= mediumManaPct", fn)
        self.assertIn("HealTopUpAllowed(", fn)

    def test_the_row_read_does_not_read_the_key(self):
        fn = body(ACTIONS, "HealReserveRead ai::BossHealReserve(PlayerbotAI* ai, Player* bot)")
        self.assertNotIn("dungeonRunHealReserve", fn)
        self.assertIn("HasHealReserveMap(", fn)
        self.assertIn("BossTacticKind::HealReserve", fn)
        self.assertIn("HealReservePick(", fn)
        self.assertIn("ReadSeekFightCached(", fn)

    def test_the_policy_holds_the_pure_rules(self):
        for name in ("HealReserveReleased(", "HealReservePick(", "HealTopUpAllowed("):
            self.assertIn(name, POLICY)
        self.assertIn("reservePct", POLICY)
        self.assertIn("released", POLICY)
        self.assertNotIn("14834", POLICY)


if __name__ == "__main__":
    unittest.main()
