"""Grey-quest accept gate for alt-bots playing on their own.

Alts carry random=false, so a gate keyed only on IsRandomBot never reaches
them: a level 12 alt in Stormwind took a level 2 city errand and stayed for
its hand-in. The grey skip in WouldAcceptQuest must also key on IsAutonomous.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACCEPT = (ROOT / "ai" / "playerbot" / "strategy" / "actions" / "AcceptQuestAction.cpp").read_text()


class AltGreyQuestWiringTest(unittest.TestCase):
    def test_grey_gate_covers_autonomous_alts(self):
        start = ACCEPT.index("bool AcceptAllQuestsAction::WouldAcceptQuest(")
        gate_end = ACCEPT.index('AI_VALUE2(bool, "need quest reward"', start)
        gate = ACCEPT[ACCEPT.rindex("if (sPlayerbotAIConfig.botQuestLogUpkeep", start, gate_end):gate_end]
        self.assertIn("sRandomBotFacade.IsRandomBot(bot)", gate)
        self.assertIn("AltBotService::Instance().IsAutonomous(bot->GetObjectGuid())", gate)
        self.assertIn("CONFIG_INT32_QUEST_LOW_LEVEL_HIDE_DIFF", gate)


if __name__ == "__main__":
    unittest.main()
