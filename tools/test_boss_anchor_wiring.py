"""Wiring pins for the boss `anchor` kind (Hakkar's platform, map 309 entry 14834).

The clamp itself is pure and tested in tools/test_boss_anchor_policy.cpp (ClampToAnchor, InsideAnchor, AnchorFloorOk,
SeekPullSonInAnchor); the evade outcome in tools/test_combat_monitor_policy.cpp. These pins only prove that every TB
mover of a boss fight runs its destination through the one glue helper before it moves, that the seek pull only picks a
Son inside the anchor, and that the fight samplers feed the boss's state to the classifier.
Run: python3 -m unittest tools/test_boss_anchor_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
ACTIONS_H = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.h").read_text()
DUNGEON = (ROOT / "ai/playerbot/strategy/actions/DungeonActions.cpp").read_text()
MOVEMENT = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()
POLICY = (ROOT / "ai/playerbot/BossTacticsPolicy.h").read_text()
MGR = (ROOT / "ai/playerbot/BossTacticsMgr.cpp").read_text()

CLAMP = "ClampMoveToBossAnchor(ai, bot, "


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


def assert_clamped_before_moves(case, text, signature, min_moves=1):
    fn = body(text, signature)
    moves = [m.start() for m in re.finditer(r"\bMoveTo\(bot->GetMapId\(\)", fn)]
    case.assertGreaterEqual(len(moves), min_moves, signature)
    clamps = [m.start() for m in re.finditer(re.escape(CLAMP), fn)]
    case.assertGreaterEqual(len(clamps), len(moves), "%s: a MoveTo without the anchor clamp" % signature)
    for move in moves:
        case.assertTrue(any(clamp < move for clamp in clamps), "%s: the clamp comes after its MoveTo" % signature)
    return fn


class BossAnchorWiring(unittest.TestCase):
    def test_one_glue_helper_over_the_pure_clamp(self):
        self.assertIn("bool ClampMoveToBossAnchor(PlayerbotAI* ai, Player* bot, float& x, float& y, float& z);",
                      ACTIONS_H)
        glue = body(ACTIONS, "bool ai::ClampMoveToBossAnchor(PlayerbotAI* ai, Player* bot, float& x, float& y, float& z)")
        self.assertIn("LiveBossAnchor(ai, bot, circle)", glue)
        self.assertIn("ClampToAnchor(circle, cx, cy)", glue)
        self.assertIn("AnchorFloorOk(", glue)
        live = body(ACTIONS, "bool ai::LiveBossAnchor(PlayerbotAI* ai, Player* bot, BossAnchorCircle& out)")
        # Data-gated: a map without an anchor row costs one lookup and clamps nothing.
        self.assertIn("HasAnchorMap(bot->GetMapId())", live)
        self.assertIn("BossTacticKind::Anchor", live)
        self.assertIn("ScanBossTactics(ai, bot)", live)
        self.assertIn("IsInEvadeMode()", live)
        self.assertIn("AnchorCircleOf(", live)

    def test_seek_gather_and_hold_is_clamped(self):
        assert_clamped_before_moves(self, ACTIONS, "bool BossTacticsSeekAction::Execute(Event& event)")

    def test_seek_tank_walk_and_drag_are_clamped(self):
        fn = assert_clamped_before_moves(self, ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)", 2)
        # The healer take never chases a Son out of the anchor: it taunts from where it stands.
        self.assertIn("BossAnchorRefusesChase(ai, bot, task.source)", fn)

    def test_boss_stand_seat_is_clamped(self):
        assert_clamped_before_moves(self, ACTIONS, "bool BossTacticsStandAction::Execute(Event& event)")

    def test_spread_step_is_clamped(self):
        assert_clamped_before_moves(self, DUNGEON, "bool RaidSpreadAction::Execute(Event& event)")

    def test_tank_face_away_is_clamped(self):
        assert_clamped_before_moves(self, MOVEMENT, "bool TankFaceAwayAction::Execute(Event& event)")

    def test_approach_stand_ring_skips_points_outside_the_anchor(self):
        fn = body(COMMANDER, "bool ApproachStandSearch(Player* lead, TortoiseBots::DungeonRun::Run const& run,")
        self.assertIn("ai::BossAnchorOfBoss(", fn)
        self.assertIn("ai::InsideAnchor(anchor, px, py)", fn)
        self.assertLess(fn.index("ai::InsideAnchor(anchor, px, py)"), fn.index("candidates.push_back(c)"))

    def test_seek_pull_picks_only_a_son_inside_the_anchor(self):
        fn = body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("LiveBossAnchor(ai, bot, pullAnchor)", fn)
        self.assertGreaterEqual(fn.count("SeekPullSonInAnchor(pullAnchor, "), 2, "both the latch and the new pick")
        self.assertIn("LogSeekPullSkipAnchor(ai, bot)", fn)
        self.assertIn('"seek pull: skip anchor"', ACTIONS)

    def test_anchor_row_loads_through_the_table(self):
        self.assertIn('if (text == "anchor")', POLICY)
        self.assertIn("Anchor = 23", POLICY)
        self.assertIn("anchorMaps_", POLICY)
        self.assertIn("an anchor row it cannot use", MGR)

    def test_samplers_feed_the_boss_state(self):
        fn = body(COMMANDER, "void SampleFightTimeline(uint32 groupId, Player* anchor)")
        self.assertIn("monitor.NoteBossState(groupId, ", fn)
        # Before the timeline gate: the evade outcome needs no AiPlayerbot.CombatTimeline.
        self.assertLess(fn.index("monitor.NoteBossState("), fn.index("monitor.TimelineEnabled()"))
        for guard in ("IsInEvadeMode()", "GetRespawnCoord(", "GetMaxHealth()"):
            self.assertIn(guard, fn)


if __name__ == "__main__":
    unittest.main()
