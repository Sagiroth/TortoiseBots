"""Wiring pins for the victim relief (Hakkar's Sons on the boss's victim tank, walk 66).

The rules are pure and tested in tools/test_seek_aura_policy.cpp (the dwell, the
retake latch, the reliever, the claim, the keep, the wrap). These pins only prove
the live code calls them: FindBossSeekTank answers a relief for any raid tank
while two or more are alive, after the boss-victim return and the own-Son block,
and the relief take logs its own row. Flag off keeps today's takes exactly.
Run: python3 -m unittest tools/test_seek_victim_relief_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
POLICY = (ROOT / "ai/playerbot/SeekAuraPolicy.h").read_text()

def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


class SeekVictimReliefWiring(unittest.TestCase):
    def test_the_call_stands_after_the_boss_victim_return_and_the_own_son(self):
        fn = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        self.assertIn("Unit* FindSeekVictimRelief(PlayerbotAI* ai, Player* bot,", ACTIONS)
        boss_victim = fn.index("if ((thirdTank || pickupTank || healerTaker) &&")
        relief_call = fn.index("if (roles.botIndex >= 0 && SeekReliefTaskFor(ai, bot, roles, state, task))")
        third_pickup = fn.index("if (thirdTank || pickupTank)")
        on_son = fn.index("if (onTank)")
        self.assertLess(boss_victim, relief_call)
        self.assertIn("SeekTankTakeAllowed",
                      fn[boss_victim:fn.rindex("return task;", boss_victim, relief_call)])
        self.assertLess(on_son, relief_call)
        self.assertLess(fn.index("return task;", on_son), relief_call)
        self.assertLess(relief_call, third_pickup)

    def test_the_call_is_for_every_raid_tank(self):
        fn = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        self.assertIn("if (roles.botIndex >= 0 && SeekReliefTaskFor(ai, bot, roles, state, task))", fn)
        self.assertNotIn("if (thirdTank || healerTaker)", fn)
        # One fill for both call sites: the fields and the source word are written once, in the helper.
        helper = body(ACTIONS, "bool SeekReliefTaskFor(PlayerbotAI* ai, Player* bot,")
        self.assertIn("task.takeSrc = \"victim_relief\";", helper)
        self.assertIn("FindSeekVictimRelief(ai, bot, roles, state)", helper)
        # The row and the pick's clock come from the same state at both sites, so both log the same `beyond=` and the
        # take's confirm measures the claim on the clock its pick used.
        self.assertIn("task.row = state.row;", helper)
        self.assertIn("task.pickMs = state.nowMs;", helper)
        self.assertEqual(1, ACTIONS.count("task.takeSrc = \"victim_relief\";"))

    def test_VictimRelief_MainAndFourthTankReachRelief(self):
        fn = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        gate_start = fn.index("if (!thirdTank && !pickupTank && !healerTaker)\n    {")
        gate = fn[gate_start:fn.index("SeekFight state;", gate_start)]
        # The bots the gate drops (no pull job, and no hunter/ranged fallback: the main tank, a fourth tank, a
        # two-tank raid's main tank) still ask for the victim relief when they are raid tanks. No tank position is
        # a filter: roles.tanks[0] and roles.tanks[3] are raid tanks as much as tanks[1] and tanks[2].
        class_test = gate.index("if (bot->GetClass() != CLASS_HUNTER")
        self.assertIn("if (roles.botIndex >= 0 && sPlayerbotAIConfig.dungeonRunSeekVictimRelief)",
                      gate[class_test:])
        self.assertIn("SeekReliefTaskFor(ai, bot, roles, reliefState, task)", gate[class_test:])
        self.assertLess(gate.index("SeekReliefTaskFor(", class_test), gate.index("return task;", class_test))
        # And the relief is all they get: no pull, walk-out, drag, pre-place, swing hold or anchor branch starts
        # for a tank the gate drops (the pull paths keep their own eligibility exactly).
        early = gate[gate.index("if (roles.botIndex >= 0", class_test):]
        for field in ("task.pull", "task.rangedPull", "task.fromAnchor", "task.walkout", "task.walkToSpot",
                      "task.holdSwing", "task.prePlaceHold", "task.row"):
            self.assertNotIn(field, early)

    def test_VictimRelief_KeyOffTheEarlyBlockReadsNothing(self):
        fn = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        gate_start = fn.index("if (!thirdTank && !pickupTank && !healerTaker)\n    {")
        gate = fn[gate_start:fn.index("SeekFight state;", gate_start)]
        flag = gate.index("if (roles.botIndex >= 0 && sPlayerbotAIConfig.dungeonRunSeekVictimRelief)")
        read = gate.index("ReadSeekFightCached(ai, bot, reliefState)")
        fill = gate.index("SeekReliefTaskFor(ai, bot, roles, reliefState, task)")
        # The flagged branch owns the whole relief read: key 0 reads no fight state and fills no task here.
        self.assertLess(flag, read)
        self.assertLess(read, fill)
        self.assertNotIn("dungeonRunSeekVictimRelief", gate[:flag])
        self.assertNotIn("ReadSeekFightCached", gate[:flag])

    def test_the_function_reads_the_flag_and_names_the_take(self):
        fn = body(ACTIONS, "Unit* FindSeekVictimRelief(PlayerbotAI* ai, Player* bot,")
        self.assertIn("sPlayerbotAIConfig.dungeonRunSeekVictimRelief", fn)
        self.assertIn("SeekTankTauntReady(", fn)
        self.assertIn("PlayerHoldsFightSon(", fn)
        self.assertIn("SeekVictimReliefClaimFor(", fn)
        self.assertIn("SeekVictimReliefKeepTick(", fn)
        self.assertNotIn("SeekVictimReliefDue(", fn)
        self.assertNotIn("SeekVictimReliefStart(", fn)
        self.assertNotIn('"seek relief son=', fn)

    def test_VictimRelief_TwoTanksAliveRelieves(self):
        fn = body(ACTIONS, "Unit* FindSeekVictimRelief(PlayerbotAI* ai, Player* bot,")
        # Two living tanks are enough, and every raid tank may relieve (roles.tanks holds live members only).
        self.assertIn("SeekVictimReliefAllowed(roles.tanks.size())", fn)
        self.assertNotIn("roles.tanks.size() < 3", fn)
        self.assertIn("const bool tankRole = roles.botIndex >= 0;", fn)
        self.assertIn("SeekVictimRelieverOk(tankRole,", fn)
        # The claim carries the pick's rank, so a nearer reliever can win it.
        self.assertIn("SeekVictimReliefClaimFor(true, latch, self, nowMs, roles.botIndex, sonYd)", fn)
        self.assertIn("const float sonYd = bot->GetDistance(son);", fn)
        self.assertIn("inline bool SeekVictimReliefAllowed(std::size_t livingTanks)", POLICY)
        self.assertIn("return livingTanks >= 2;", POLICY)
        self.assertIn("inline bool SeekReliefClaimBeats(float selfYd, int selfIdx, float claimYd, int claimIdx)",
                      POLICY)
        claim = POLICY[POLICY.index("inline bool SeekVictimReliefClaimFor(bool flagOn, SeekReliefLatch& latch,"):
                        POLICY.index("inline bool SeekVictimReliefKept(")]
        self.assertIn("if (latch.claimTaker == self)", claim)
        self.assertIn("SeekReliefClaimBeats(selfYd, selfIdx, latch.claimYd, latch.claimIdx)", claim)
        # The main tank (tanks[0]) is the raid's Hakkar tank: the ranking puts any off-tank ahead of it, and an
        # off-tank may take a claim over from it, never the reverse.
        beats = POLICY[POLICY.index("inline bool SeekReliefClaimBeats("):
                       POLICY.index("inline bool SeekVictimReliefClaimFor(")]
        self.assertIn("if ((selfIdx == 0) != (claimIdx == 0))", beats)
        self.assertIn("return claimIdx == 0;", beats)

    def test_VictimRelief_LatchKeyedByInstanceAndSon(self):
        self.assertIn(
            "inline std::pair<std::uint32_t, std::uint64_t> SeekReliefKey(std::uint32_t instanceId, "
            "std::uint64_t sonGuid)", POLICY)
        self.assertIn("SeekReliefLatches g_seekReliefLatches;", ACTIONS)
        fn = body(ACTIONS, "Unit* FindSeekVictimRelief(PlayerbotAI* ai, Player* bot,")
        self.assertEqual(2, fn.count("SeekReliefKey(bot->GetInstanceId(),"))
        note = body(ACTIONS, "void SeekReliefTakeNoteExec(PlayerbotAI* ai, Player* bot,")
        self.assertIn("SeekReliefKey(bot->GetInstanceId(),", note)
        # The sweep is the production helper, its clock is read under the latch mutex, and it names its own instance.
        lock = fn.index("std::lock_guard<std::mutex> lock(g_seekReliefMutex);")
        clock = fn.index("WorldTimer::getMSTime()", lock)
        sweep = fn.index("SweepSeekReliefLatches(g_seekReliefLatches, bot->GetInstanceId(), sweepNowMs);")
        self.assertLess(lock, clock)
        self.assertLess(clock, sweep)

    def test_VictimRelief_OnlyItsOwnInstanceIsSwept(self):
        sweep = body(POLICY, "inline void SweepSeekReliefLatches(SeekReliefLatches& latches,")
        self.assertIn("if (it->first.first != instanceId)", sweep)
        self.assertIn("kSeekVictimReliefStaleMs", sweep)

    def test_VictimRelief_SelectionClaimsOnlyWhenEligible(self):
        fn = body(ACTIONS, "Unit* FindSeekVictimRelief(PlayerbotAI* ai, Player* bot,")
        self.assertLess(fn.index("SeekVictimRelieverOk("), fn.index("SeekVictimReliefClaimFor("))
        gate = body(ACTIONS, "SeekReliefGate SeekReliefTakeGate(")
        self.assertIn("SeekReliefKey(bot->GetInstanceId(),", gate)
        self.assertIn("SeekVictimReliefPreExec(found->second,", gate)

    def test_VictimRelief_TauntPickIgnoresNearestTake(self):
        take = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        take = take[take.index("if (task.take)"):take.index("if (task.pull)")]
        self.assertIn("SeekTakeUsesKnownTaunt(sPlayerbotAIConfig.dungeonRunSeekNearestTake, task.takeSrc)", take)
        self.assertIn("taunt = KnownTaunt(ai);", take)
        self.assertIn("SeekTakeUsesKnownTaunt(bool nearestTakeOn, const char* takeSrc)", POLICY)
        self.assertIn('std::strcmp(takeSrc, "victim_relief") == 0', POLICY)

    def test_VictimRelief_GateStandsBeforeAnySideEffect(self):
        take = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        take = take[take.index("if (task.take)"):take.index("if (task.pull)")]
        gate = take.index("SeekReliefTakeGate(bot, task)")
        self.assertLess(gate, take.index("LogSeekTake("))
        self.assertLess(gate, take.index("if (reliefGate == SeekReliefGate::Refuse)"))
        self.assertLess(gate, take.index("Attack(bot, task.source)"))
        self.assertLess(gate, take.index("ai->CastSpell(taunt, task.source)"))

    def test_VictimRelief_ConfirmOnThePickClock(self):
        gate = body(ACTIONS, "SeekReliefGate SeekReliefTakeGate(")
        rollback = body(ACTIONS, "void SeekReliefTakeRollback(")
        row = body(ACTIONS, "void SeekReliefTakeNoteExec(")
        for src in (gate, rollback, row):
            self.assertIn("pickMs", src)
            self.assertNotIn("WorldTimer", src)
        self.assertIn("SeekVictimReliefPreExec(found->second, bot->GetObjectGuid().GetRawValue(), task.pickMs)",
                      gate)
        pre = body(POLICY, "inline SeekReliefGate SeekVictimReliefPreExec(")
        pre = pre[:pre.index("inline void SeekVictimReliefRollback(")]
        self.assertIn("SeekVictimReliefClaimLive(latch, pickMs)", pre)
        self.assertIn("SeekVictimReliefStart(latch, self, pickMs)", pre)
        rb = body(POLICY, "inline void SeekVictimReliefRollback(")
        self.assertIn("latch.activeSinceMs != pickMs", rb)
        self.assertIn("latch.lastStartMs = latch.rollbackStartMs;", rb)

    def test_VictimRelief_CountsOnlyStartedTakes(self):
        take = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        take = take[take.index("if (task.take)"):take.index("if (task.pull)")]
        # Three exits, three rows: the attack-only return, the refusal, the cast. Only a confirmed take writes a row;
        # each exit reads the pick's clock, and a confirmed take that issues nothing rolls back.
        self.assertEqual(3, take.count("SeekReliefTakeNoteExec(ai, bot,"))
        self.assertEqual(2, take.count("SeekReliefTakeRollback(bot, task.source, task.pickMs);"))
        self.assertEqual(5, take.count("task.pickMs"))
        # `from=` is the Son's victim read before the cast, not after the taunt may have handed it over.
        self.assertLess(take.index("Unit* const reliefFrom = task.source->GetVictim();"),
                        take.index("ai->CastSpell(taunt, task.source)"))
        note = body(ACTIONS, "void SeekReliefTakeNoteExec(PlayerbotAI* ai, Player* bot,")
        self.assertIn("latch.lastStartMs != pickMs", note)
        self.assertIn('"seek relief son=', note)
        self.assertIn("dwellMs = pickMs - latch.sinceMs;", note)
        # The row is built and written after the lock: its cap read and logEvent never run under it.
        self.assertEqual(1, note.count("g_seekReliefMutex"))
        self.assertLess(note.index("snprintf(info1"), note.index("SeekTankLeaveCollapsesCap("))

if __name__ == "__main__":
    unittest.main()
