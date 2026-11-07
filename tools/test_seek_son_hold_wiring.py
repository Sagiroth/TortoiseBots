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
SEEK_POLICY = (ROOT / "ai/playerbot/SeekAuraPolicy.h").read_text()
STATE_POLICY = (ROOT / "ai/playerbot/SeekFightStatePolicy.h").read_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


def guard_body(text, signature):
    # The function's definition (the forward declaration comes first in the file).
    start = text.index(signature, text.index(signature) + len(signature))
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


class SeekSonHoldReview(unittest.TestCase):
    def test_the_boss_victim_takes_no_son(self):
        tank = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        gate = tank.index("SeekTankTakeAllowed(state.boss && state.boss->GetVictim() == bot)")
        self.assertLess(tank.index("ReadSeekFightCached(ai, bot, state)"), gate)
        self.assertLess(gate, tank.index("if (onTank)"))

    def test_no_comment_keeps_the_three_tank_gate(self):
        for stale in ("once the third off-tank cannot take it", "while the third off-tank cannot",
                      "Nothing\n        // here when the third off-tank can still take it"):
            self.assertNotIn(stale, ACTIONS)


class SeekSonHoldLastRound(unittest.TestCase):
    def test_the_fallback_pulls_while_the_third_tank_is_the_victim(self):
        read = body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("SeekPullActorsFor(roles.tanks.size(), thirdTankIsBossVictim, anchorShot)", read)
        self.assertIn("thirdTankIsBossVictim = roles.tanks.size() >= 3 && boss->GetVictim() == roles.tanks[2]", read)
        self.assertIn("FindSeekFallbackPuller(ai, bot, roles, boss, *row, actors.fallback)", read)
        self.assertIn("out.fallbackOpen = actors.fallback;", read)
        tank = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        self.assertIn("FindSeekFallbackPuller(ai, bot, roles, state.boss, *state.row, state.fallbackOpen)", tank)
        self.assertIn("(roles.tanks.size() >= 3 && !state.fallbackOpen)", tank)


class SeekHoldClockEngagement(unittest.TestCase):
    def test_the_hold_clock_counts_engaged_time_only(self):
        # Every Son in the fight list used to feed the clock, and the hold rule holds any non-selected Son wherever it
        # stands: after 20 s every idle spare around the spot was released for good and the area veto stopped
        # protecting the reservoir. The clock now takes a Son only while it fights a victim, and the cap reaches the
        # selected source and a Son on a tank inside the window.
        tick = guard_body(ACTIONS, "void TickSeekHoldClock(")
        self.assertIn("SeekHoldClockTicks(son == out.source, engaged, victimTank, out.windowOpen)", tick)
        # The record/release composition is the policy's (SeekSonHeld), not a hand-rolled expression in the action.
        self.assertIn("SeekSonHeld(IsSeekSource(out, son), ticks,", tick)
        self.assertIn("SeekHeldMs(out.holdSince, guid, nowMs)", tick)
        self.assertIn("SeekHoldsUnit(out, son))", tick)
        self.assertNotIn("capped ||", tick)
        self.assertIn("sServerFacade.IsAlive(son)", tick)
        self.assertIn("son->IsInCombat()", tick)
        self.assertIn("son->GetVictim()", tick)
        self.assertLess(tick.index("SeekHoldClockTicks("), tick.index("SeekHoldsUnit(out, son)"))
        self.assertIn("inline bool SeekHoldClockTicks(bool selected, bool engaged, bool victimTank, bool windowOpen)",
                      SEEK_POLICY)
        self.assertIn("return engaged && (selected || (victimTank && windowOpen));", SEEK_POLICY)

    def test_the_hold_clock_drops_what_nobody_sees(self):
        # A read only reaches the Sons around the bot, so an unseen Son keeps its entry; the sweep drops what no read
        # refreshed for the grace, so the map cannot grow and a respawn under the same guid starts a clean hold.
        self.assertIn("constexpr std::uint32_t kSeekHoldStaleMs = 20000;", STATE_POLICY)
        clock = body(STATE_POLICY, "inline void SeekHoldClockTick(")
        self.assertIn("nowMs - it->second.seenMs > kSeekHoldStaleMs ? held.erase(it) : std::next(it)", clock)


if __name__ == "__main__":
    unittest.main()
