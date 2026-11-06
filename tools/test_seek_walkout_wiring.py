"""Wiring pins for the seek walk-out pull (Hakkar's Sons stand ~78 yd out, past the anchored shot).

The rules are pure and tested in tools/test_seek_walkout_policy.cpp (the puller's role and exclusions, the task's
start limits, its ends and the exemption window, the shot and back points). These pins only prove the live code calls
them: one ranged puller leaves the anchor while its task runs, shoots the Son once and walks back.
Run: python3 -m unittest tools/test_seek_walkout_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
ACTIONS_H = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.h").read_text()
MOVEMENT = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


class SeekWalkoutWiring(unittest.TestCase):
    def test_the_puller_alone_is_exempt_from_the_anchor(self):
        # The exemption lives in the one live-anchor read, so every clamp and the ChaseTo refusal drop for the puller
        # while its task runs, and come back the moment it ends.
        live = body(ACTIONS, "const BossTacticRow* ai::LiveBossAnchorRow(PlayerbotAI* ai, Player* bot)")
        self.assertIn("if (SeekWalkoutExemptsBot(bot))\n        return nullptr;", live)
        self.assertLess(live.index("SeekWalkoutExemptsBot(bot)"), live.index("ScanBossTactics(ai, bot)"))
        exempt = body(ACTIONS, "bool SeekWalkoutExemptsBot(Player* bot)")
        self.assertIn("WalkoutExempts(", exempt)
        self.assertIn("LiveBossAnchor(ai, bot, anchor)", body(MOVEMENT, "bool MovementAction::ChaseTo(WorldObject* obj, float distance, float angle)"))

    def test_the_puller_is_picked_by_the_policy(self):
        pick = body(ACTIONS, "Player* FindSeekWalkoutPuller(PlayerbotAI* ai, Player* bot, Unit* boss)")
        self.assertIn("PickWalkoutPuller(", pick)
        self.assertIn("WalkoutRoleOf(", pick)
        for guard in ("PlayerbotAI::IsTank(member)", "PlayerbotAI::IsHeal(member)", "boss->GetVictim() == member",
                      "getThreatList()", "GetCharmerGuid()", "kHakkarCauseInsanitySpell"):
            self.assertIn(guard, pick)

    def test_the_seek_read_runs_the_task(self):
        read = body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("UpdateSeekWalkout(ai, bot, *row, boss, cycleNo, nowMs, walkoutWanted, out)", read)
        update = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        for call in ("WalkoutCheck(", "WalkoutMayStart(", "WalkoutStart(", "WalkoutNoteShot(", "WalkoutFinish(",
                     "FindSeekWalkoutPuller(ai, bot, boss) != bot"):
            self.assertIn(call, update)
        # Its one shot is the pull: exempt from the seek hold like the other pullers' until the shot lands, then held.
        self.assertIn("out.puller = true;", update)
        self.assertIn("out.pullTarget = nearest;", update)
        self.assertIn("out.puller = !state.shot;", update)
        self.assertIn("out.pullTarget = !state.shot && sonOk ? son : nullptr;", update)
        self.assertIn('"seek pull: ranged walk-out %s -> %u"', ACTIONS)
        self.assertIn('"seek pull: walk-out %s"', ACTIONS)
        # A re-pull or an evade ends any walk-out with the seek record.
        self.assertGreaterEqual(read.count("ForgetSeekWalkout(ai, bot->GetInstanceId())"), 2)

    def test_the_task_shoots_once_and_walks_back(self):
        self.assertIn("bool walkout = false;", ACTIONS_H)
        tank = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        self.assertIn("state.walkout", tank)
        fn = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        walk = fn[fn.index("if (task.walkout)"):fn.index("if (task.take)")]
        self.assertIn("WalkoutShotPoint(", walk)
        self.assertIn("WalkoutShotRangeYd(bot->GetClass() == CLASS_HUNTER)", walk)
        self.assertIn('ai->DoSpecificAction("shoot", event, true)', walk)
        self.assertIn("WalkoutBackPoint(", walk)
        self.assertIn("IsWithinLOSInMap(task.source)", walk)
        # The clamp is asked even here: a no-op while the task exempts the bot, the anchor again once it lapses.
        self.assertLess(walk.index("ClampMoveToBossAnchor(ai, bot, x, y, z)"), walk.index("MoveTo(bot->GetMapId(), x, y, z"))


if __name__ == "__main__":
    unittest.main()
