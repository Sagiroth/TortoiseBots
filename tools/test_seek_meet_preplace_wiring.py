"""Wiring pins for the meet's pre-place (AiPlayerbot.DungeonRunSeekMeetPrePlace, Hakkar walk 56 F2).

The rules are pure and tested in tools/test_seek_walkout_policy.cpp (WalkoutMeetPrePlacePoint, PickMeetPrePlaceReceiver,
MeetPrePlaceKeeps and the pre-place's lifecycle). These pins prove the live code calls them: the walk-out start picks
the third tank and records its point under the flag, the shot hands the pre-place to the meet, the live check ends it,
and the receiver's pre-shot task is walkToSpot to that point.
Run: python3 -m unittest tools/test_seek_meet_preplace_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
ROWS = (ROOT / "docs/play/refusal-rows.md").read_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


def update():
    return body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")


def tank_pick():
    return body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")


class SeekMeetPrePlaceWiring(unittest.TestCase):
    def test_the_flag_defaults_off(self):
        self.assertIn('GetBoolDefault("AiPlayerbot.DungeonRunSeekMeetPrePlace", false)', CONFIG)
        self.assertIn("bool dungeonRunSeekMeetPrePlace = false;", CONFIG_H)
        self.assertIn("AiPlayerbot.DungeonRunSeekMeetPrePlace = 0", TEMPLATE)
        self.assertIn("### `AiPlayerbot.DungeonRunSeekMeetPrePlace`", SETTINGS)

    def test_the_start_picks_and_records_the_pre_place_under_the_flag(self):
        fn = update()
        start = fn[fn.index("const std::uint64_t sonGuid = scan.son->GetObjectGuid().GetRawValue();"):]
        self.assertIn("Player* const prePlaceTank = meetOn && sPlayerbotAIConfig.dungeonRunSeekMeetPrePlace\n"
                      "        ? PickWalkoutPrePlace(ai, boss, fightSons, row.targetEntry, tank, scan.x, scan.y, prePlaceX,"
                      " prePlaceY)\n        : nullptr;", start)
        # Recorded under the lock, right after the task starts, for that task.
        self.assertLess(start.index("WalkoutStart(record.state, self, sonGuid, nowMs, cycleNo, dueInMs);"),
                        start.index("WalkoutPrePlaceSet(record.state, prePlaceTank ?"))
        self.assertIn('"son=%u,event=preplace,receiver=%s,receiver_yd=%.1f,point_yd=%.1f"', start)
        helper = body(ACTIONS, "Player* PickWalkoutPrePlace(PlayerbotAI* ai, Unit* boss,")
        self.assertIn("int const pick = PickMeetPrePlaceReceiver(tanks);", helper)
        self.assertIn("WalkoutMeetPrePlacePoint(tank, standX, standY, x, y)", helper)
        # The victim, charm and Son checks feed the pick.
        self.assertIn("candidate.bossVictim = member && boss->GetVictim() == member;", helper)
        self.assertIn("member->HasAura(kHakkarCauseInsanitySpell)", helper)
        self.assertIn("candidate.holdsSon = PlayerHoldsFightSon(fightSons, sonEntry, member, 0);", helper)

    def test_the_shot_hands_over_and_the_live_check_ends_it(self):
        fn = update()
        edge = fn[fn.index("bool const firstShot = !it->second.state.shot;"):]
        self.assertLess(edge.index("meetRow = info1;"), edge.index("WalkoutPrePlaceNoteMeet(it->second.state);"))
        # Today's pick at the shot is unchanged.
        self.assertEqual(1, ACTIONS.count("PickMeetReceiver("))
        self.assertIn("if (meetOn && sPlayerbotAIConfig.dungeonRunSeekMeetPrePlace && WalkoutPrePlaceLive(state))\n"
                      "        EndLostWalkoutPrePlace(ai, map, boss, instanceId, state);", fn)
        self.assertLess(fn.index("EndLostWalkoutPrePlace(ai, map, boss, instanceId, state);"),
                        fn.index("if (WalkoutActive(state))\n    {"))
        lost = body(ACTIONS, "void EndLostWalkoutPrePlace(PlayerbotAI* ai, Map* map, Unit* boss,")
        self.assertIn("if (MeetPrePlaceKeeps(alive, charmed, victim))\n        return;", lost)
        self.assertIn("bool const victim = alive && boss->GetVictim() == receiver;", lost)
        self.assertIn("WalkoutPrePlaceClear(it->second.state);", lost)
        self.assertIn('"son=%u,event=preplace_end,receiver=%s,reason=%s"', lost)

    def test_the_receivers_pre_shot_task_is_walk_to_spot_to_the_point(self):
        helper = body(ACTIONS, "bool SeekMeetPrePlaceTask(Player* bot, const BossTacticRow* row, SeekTankTask& task)")
        self.assertIn("if (!sPlayerbotAIConfig.dungeonRunSeekMeetPrePlace || !row ||", helper)
        self.assertIn("SeekMeetPrePlaceFor(bot->GetInstanceId(), bot->GetObjectGuid().GetRawValue(), x, y)", helper)
        self.assertIn("task.walkToSpot = true;\n    task.x = x;\n    task.y = y;", helper)
        self.assertIn("if (bot->GetDistance2d(x, y) <= kSeekTankArriveYd)", helper)
        accessor = body(ACTIONS, "bool SeekMeetPrePlaceFor(std::uint32_t instanceId, std::uint64_t bot, float& x, float& y)")
        self.assertIn("!WalkoutPrePlaceLive(it->second.state)", accessor)
        fn = tank_pick()
        # The third tank's branch: after its own Son and a fighting Son, before its pull.
        branch = fn[fn.index("    if (thirdTank || pickupTank)\n    {"):]
        pre = branch.index("if (thirdTank && SeekMeetPrePlaceTask(bot, state.row, task))")
        self.assertLess(branch.index("task.source = fighting;"), pre)
        self.assertLess(pre, branch.index("if (thirdTank && state.pullTarget)"))
        # The meet's wait keeps the pre-placed receiver at its point.
        wait = fn[fn.index("LogSeekMeetWait(ai, bot, son, sonVictim, meetMs);"):]
        self.assertLess(wait.index("SeekMeetPrePlaceTask(bot, state.row, task);"), wait.index("return task;"))
        # Hakkar's victim stands its task down before either call.
        self.assertLess(fn.index("!SeekTankTakeAllowed(state.boss && state.boss->GetVictim() == bot))\n        return task;"),
                        fn.index("if (thirdTank && SeekMeetPrePlaceTask("))

    def test_the_rows_are_documented(self):
        self.assertIn("event=preplace,receiver=<name>,receiver_yd=%.1f,point_yd=%.1f", ROWS)
        self.assertIn("event=preplace_end", ROWS)
        self.assertIn("receiver_yd=%.1f", ROWS)


if __name__ == "__main__":
    unittest.main()
