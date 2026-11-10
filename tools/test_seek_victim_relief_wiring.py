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
        relief_call = fn.index("if (roles.botIndex >= 0)")
        relief = fn.index("FindSeekVictimRelief(ai, bot, roles, state)")
        third_pickup = fn.index("if (thirdTank || pickupTank)")
        on_son = fn.index("if (onTank)")
        self.assertLess(boss_victim, relief)
        self.assertIn("SeekTankTakeAllowed", fn[boss_victim:fn.rindex("return task;", boss_victim, relief)])
        self.assertLess(on_son, relief_call)
        self.assertLess(fn.index("return task;", on_son), relief_call)
        relief_block = fn[relief_call:third_pickup]
        self.assertIn("task.takeSrc = \"victim_relief\";", relief_block)
        self.assertEqual(relief_block.count("task.take = true;"),
                         relief_block.count("task.takeSrc = "))

    def test_the_call_is_for_every_raid_tank(self):
        fn = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        self.assertIn("if (roles.botIndex >= 0)\n    {\n        if (Unit* const reliefSon =", fn)
        self.assertNotIn("if (thirdTank || healerTaker)\n    {\n        if (Unit* const reliefSon =", fn)

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
        note = body(ACTIONS, "void SeekReliefTakeNoteExec(PlayerbotAI* ai, Player* bot,")
        self.assertIn("SeekVictimReliefNoteExec(latch,", note)

    def test_VictimRelief_TauntPickIgnoresNearestTake(self):
        take = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        take = take[take.index("if (task.take)"):take.index("if (task.pull)")]
        self.assertIn("SeekTakeUsesKnownTaunt(sPlayerbotAIConfig.dungeonRunSeekNearestTake, task.takeSrc)", take)
        self.assertIn("taunt = KnownTaunt(ai);", take)
        self.assertIn("SeekTakeUsesKnownTaunt(bool nearestTakeOn, const char* takeSrc)", POLICY)
        self.assertIn('std::strcmp(takeSrc, "victim_relief") == 0', POLICY)

    def test_VictimRelief_CountsOnlyStartedTakes(self):
        take = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        take = take[take.index("if (task.take)"):take.index("if (task.pull)")]
        # Three exits, three notes: the attack-only return stamps true, the
        # refusal stamps this tick's `started`, the cast stamps `started || cast`.
        self.assertEqual(3, take.count("SeekReliefTakeNoteExec(ai, bot,"))
        self.assertIn("task.source->GetVictim(), true, WorldTimer::getMSTime());", take)
        self.assertIn("task.source, task.source->GetVictim(),\n", take)
        self.assertIn("task.source, reliefFrom, started || cast,", take)
        # `from=` is the Son's victim read before the cast, not after the taunt may have handed it over.
        self.assertLess(take.index("Unit* const reliefFrom = task.source->GetVictim();"),
                        take.index("ai->CastSpell(taunt, task.source)"))
        note = body(ACTIONS, "void SeekReliefTakeNoteExec(PlayerbotAI* ai, Player* bot,")
        self.assertIn("SeekVictimReliefNoteExec(latch,", note)
        self.assertIn('"seek relief son=', note)
        self.assertIn("dwellMs = nowMs - latch.sinceMs;", note)
        # The row is built and written after the lock: its cap read and logEvent never run under it.
        self.assertEqual(1, note.count("g_seekReliefMutex"))
        self.assertLess(note.index("snprintf(info1"), note.index("SeekTankLeaveCollapsesCap("))

if __name__ == "__main__":
    unittest.main()
