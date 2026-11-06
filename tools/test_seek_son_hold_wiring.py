"""Wiring pins for the Son hold on a non-tank (Hakkar, walk 45: a Son brought onto the platform sat on non-tanks and
the seek hold kept it alive; 87 of 99 deaths from Sons were non-tanks).

The rules are pure and tested in tools/test_seek_aura_policy.cpp (SeekHoldDecision's non-tank time,
SeekTakeFightingSonOnHealer), tools/test_seek_fight_state_policy.cpp (SeekNonTankHeldMs, SeekHealerTakeGuid) and
tools/test_seek_walkout_policy.cpp (the siphon lead of the walk-out start). These pins prove the live code feeds them.
Run: python3 -m unittest tools/test_seek_son_hold_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


class SeekSonHoldWiring(unittest.TestCase):
    def test_the_hold_reads_the_sons_non_tank_time(self):
        hold = body(ACTIONS, "bool SeekHoldsUnit(const SeekFight& state, Unit* unit)")
        self.assertIn("SeekNonTankHeldMs(state.nonTankSince, unit->GetObjectGuid().GetRawValue(), state.nowMs)", hold)
        read = body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("out.nonTankSince = record.healerHeldSince;", read)
        self.assertLess(read.index("SeekHealerClockTick("), read.index("out.nonTankSince = record.healerHeldSince;"))

    def test_the_first_off_tank_takes_after_8_s_whatever_the_third_does(self):
        read = body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("SeekHealerTakeGuid(record.healerHeldSince, nowMs)", read)
        self.assertNotIn("thirdTankTakes", read)

    def test_the_take_reaches_the_raids_circle_and_logs(self):
        fn = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        take = fn[fn.index("if (task.take)"):fn.index("if (task.pull)")]
        # A Son on a non-tank stands where the raid stands: inside the everyone circle (30 yd), past the tanks' (25).
        self.assertIn("BossAnchorRefusesChase(ai, bot, task.source, true)", take)
        self.assertIn("LogSeekTake(ai, bot, task.source)", take)
        log = body(ACTIONS, "void LogSeekTake(PlayerbotAI* ai, Player* bot, Unit* son)")
        self.assertIn('"seek take: %s -> %u victim=%s"', log)
        refuse = body(ACTIONS, "bool ai::BossAnchorRefusesChase(PlayerbotAI* ai, Player* bot, Unit* unit, bool raidCircle)")
        self.assertIn("AnchorCircleOf(*row, false)", refuse)

    def test_the_walk_out_waits_for_the_siphon(self):
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        self.assertIn("WalkoutMayStartFor(record.state, cycleNo, sonGuid, self, nowMs, dueInMs)", update)
        read = body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("UpdateSeekWalkout(ai, bot, *row, boss, cycleNo, nowMs, out.dueInMs, walkoutWanted, out)", read)


if __name__ == "__main__":
    unittest.main()
