"""Wiring pins for the victim relief (Hakkar's Sons on the boss's victim tank, walk 66).

The rules are pure and tested in tools/test_seek_aura_policy.cpp (the dwell, the
retake latch, the reliever, the keep, the wrap). These pins only prove the live
code calls them: FindBossSeekTank answers a relief for the other off-tank, after
the boss-victim return and the own-Son block, and the relief take logs its own
row. Flag off keeps today's takes exactly.
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
        relief_call = fn.index("if (thirdTank || healerTaker)")
        relief = fn.index("FindSeekVictimRelief(ai, bot, roles, state)")
        third_pickup = fn.index("if (thirdTank || pickupTank)")
        on_son = fn.index("if (onTank)")
        self.assertLess(boss_victim, relief)
        self.assertIn("SeekTankTakeAllowed", fn[boss_victim:fn.rindex("return task;", boss_victim, relief)])
        self.assertLess(on_son, relief_call)
        self.assertLess(fn.index("return task;", on_son), relief_call)
        third_healer = fn[relief_call:third_pickup]
        self.assertIn("task.takeSrc = \"victim_relief\";", third_healer)
        self.assertEqual(third_healer.count("task.take = true;"),
                         third_healer.count("task.takeSrc = "))

    def test_the_call_is_for_the_two_off_tanks(self):
        fn = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        self.assertIn("if (thirdTank || healerTaker)\n    {\n        if (Unit* const reliefSon =", fn)

    def test_the_function_reads_the_flag_and_names_the_take(self):
        fn = body(ACTIONS, "Unit* FindSeekVictimRelief(PlayerbotAI* ai, Player* bot,")
        self.assertIn("sPlayerbotAIConfig.dungeonRunSeekVictimRelief", fn)
        self.assertIn("SeekTankTauntReady(", fn)
        self.assertIn("PlayerHoldsFightSon(", fn)
        self.assertNotIn("SeekVictimReliefStart(", fn)
        self.assertNotIn('"seek relief son=', fn)

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
        self.assertIn("task.source, true,\n", take)
        self.assertIn("task.source, started,\n", take)
        self.assertIn("task.source, started || cast,\n", take)
        note = body(ACTIONS, "void SeekReliefTakeNoteExec(PlayerbotAI* ai, Player* bot,")
        self.assertIn("SeekVictimReliefNoteExec(latch,", note)
        self.assertIn('"seek relief son=', note)

if __name__ == "__main__":
    unittest.main()
