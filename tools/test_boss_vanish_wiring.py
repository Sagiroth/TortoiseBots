"""Wiring pins for the boss-vanish carry-over of the fight scan (High Priestess Arlokk's Vanish).

A boss that vanishes (ScriptedAI::EnterVanish: invisible, not selectable, attack stopped) leaves the bot's attackers list
while it stays alive and in combat. The scan loads a boss entry's rows from the attackers, so Arlokk's Zulian Prowler
kill_first row (boss_entry 14515) ended with her visibility and the prowlers ran free. The rules are pure and tested in
tools/test_boss_tactics_policy.cpp (BossPresenceMemory, VanishedBossStillInFight); this file proves ScanBossTactics calls
them. Run: python3 -m unittest tools/test_boss_vanish_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()


def scan_body():
    start = ACTIONS.index("BossTacticsFight ai::ScanBossTactics(PlayerbotAI* ai, Player* bot)")
    return ACTIONS[start:ACTIONS.index("if (ai && t_fightScan.live)", start)]


class BossVanishWiring(unittest.TestCase):
    def test_scan_notes_seen_bosses_and_reads_absent_ones(self):
        body = scan_body()
        self.assertIn("record->second.memory.Note(", body)
        self.assertIn("memory.Absent(seen, nowMs, !fight.units.empty())", body)

    def test_absent_boss_must_still_be_in_the_fight(self):
        body = scan_body()
        self.assertIn("VanishedBossStillInFight(", body)
        for guard in ("IsAlive()", "IsInEvadeMode()", "IsInCombat()", "GetDistance(creature)"):
            self.assertIn(guard, body)

    def test_absent_boss_rows_go_through_the_same_gates(self):
        body = scan_body()
        self.assertIn("addRows(*rows, absent.hpPct)", body)
        self.assertIn("addRows(*rows, bossHpPct)", body)
        # The key and gate checks live once, in the shared lambda.
        self.assertEqual(body.count("RowGateOpen("), 1)


if __name__ == "__main__":
    unittest.main()
