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
                      "        ? PickWalkoutPrePlace(ai, map, boss, fightSons, row.targetEntry, tank, scan.x, scan.y, prePlaceX,"
                      " prePlaceY,\n                              prePlaceFloorRefused)\n        : nullptr;", start)
        # A point off the platform's level: no pre-place, one `reason=floor` row.
        self.assertIn("else if (prePlaceFloorRefused)", start)
        self.assertIn('prePlaceFloorRefused->GetName(), "floor");', start)
        self.assertIn('"son=%u,event=preplace_skip,receiver=%s,reason=%s"', start)
        self.assertNotIn("event=preplace_end", start)
        # Recorded under the lock, right after the task starts, for that task.
        self.assertLess(start.index("WalkoutStart(record.state, self, sonGuid, nowMs, cycleNo, dueInMs);"),
                        start.index("WalkoutPrePlaceSet(record.state, prePlaceTank ?"))
        self.assertIn('"son=%u,event=preplace,receiver=%s,receiver_yd=%.1f,point_yd=%.1f"', start)
        helper = body(ACTIONS, "Player* PickWalkoutPrePlace(PlayerbotAI* ai, Map* map, Unit* boss,")
        # The point's floor is read as the anchor clamp reads it.
        self.assertIn("float const floor = map ? map->GetHeight(x, y, tank.z + 5.0f) : INVALID_HEIGHT;", helper)
        self.assertIn("if (!AnchorFloorOk(tank, floor > INVALID_HEIGHT, floor))", helper)
        self.assertLess(helper.index("WalkoutMeetPrePlacePoint("), helper.index("AnchorFloorOk("))
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
        # The stale-reader guard: a compare-and-clear on the tank, puller and task start the reader saw.
        self.assertIn("WalkoutPrePlaceClearIf(it->second.state, state.prePlaceReceiver, state.prePlacePuller,\n"
                      "                                   state.prePlaceStartMs, state.prePlaceGen);", lost)
        self.assertNotIn("WalkoutPrePlaceClear(it->second.state);", lost)
        self.assertIn('"son=%u,event=preplace_end,receiver=%s,reason=%s"', lost)

    def test_the_receivers_pre_shot_task_is_walk_to_spot_to_the_point(self):
        helper = body(ACTIONS, "bool SeekMeetPrePlaceTask(Player* bot, const BossTacticRow* row, SeekTankTask& task)")
        self.assertIn("if (!sPlayerbotAIConfig.dungeonRunSeekMeetPrePlace || !row ||", helper)
        self.assertIn("SeekMeetPrePlaceFor(bot->GetInstanceId(), bot->GetObjectGuid().GetRawValue(), x, y)", helper)
        self.assertIn("task.valid = true;\n    task.x = x;\n    task.y = y;", helper)
        self.assertIn("const bool walk = MeetPrePlaceStepFor(bot->GetDistance2d(x, y), false, false) =="
                      " MeetPrePlaceStep::Walk;", helper)
        self.assertIn("task.walkToSpot = walk;\n    task.prePlaceHold = !walk;", helper)
        # At the point the task stays valid (no `task.valid = false` in the helper).
        self.assertNotIn("task.valid = false;", helper)
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

    def test_the_late_walk_reads_the_key(self):
        # Walk 57 F3: DungeonRunSeekMeetPrePlaceLate holds the walk until the puller nears its stand or is in position.
        self.assertIn('GetBoolDefault("AiPlayerbot.DungeonRunSeekMeetPrePlaceLate", false)', CONFIG)
        self.assertIn("bool dungeonRunSeekMeetPrePlaceLate = false;", CONFIG_H)
        self.assertIn("AiPlayerbot.DungeonRunSeekMeetPrePlaceLate = 0", TEMPLATE)
        self.assertIn("### `AiPlayerbot.DungeonRunSeekMeetPrePlaceLate`", SETTINGS)
        helper = body(ACTIONS, "bool SeekMeetPrePlaceTask(Player* bot, const BossTacticRow* row, SeekTankTask& task)")
        gate = helper.index("if (!MeetPrePlaceWalkDue(sPlayerbotAIConfig.dungeonRunSeekMeetPrePlaceLate, pullerToStandYd,")
        self.assertIn("progress.pullerInPos))\n            return false;", helper)
        self.assertIn("? puller->GetDistance2d(progress.standX, progress.standY) : std::numeric_limits<float>::max();",
                      helper)
        # Only before the walk began; the first due read latches it; all of it before the task is filled.
        self.assertLess(helper.index("if (!progress.walking)"), gate)
        self.assertLess(gate, helper.index("SeekMeetPrePlaceNoteWalk(instanceId, self, progress.gen, nowMs);"))
        self.assertLess(helper.index("SeekMeetPrePlaceNoteWalk("), helper.index("task.row = row;"))
        read = body(ACTIONS, "bool SeekMeetPrePlaceProgressRead(")
        self.assertIn("out.pullerInPos = WalkoutPullerInPos(s);", read)
        self.assertIn("!WalkoutPrePlaceLive(it->second.state)", read)
        self.assertIn("out.standKnown = WalkoutActive(s) && s.puller == s.prePlacePuller;", read)

    def test_the_hold_stops_a_walk_and_frees_the_rotation(self):
        fn = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        hold = fn[fn.index("if (task.prePlaceHold)"):fn.index("if (task.take)")]
        # The own walk to the point (the move this action sent there) is told apart from any other move.
        self.assertIn("const bool ownWalk = moving && hasLast && std::fabs(lastX - task.x) < 1.0f &&"
                      " std::fabs(lastY - task.y) < 1.0f;", hold)
        self.assertIn("const MeetPrePlaceStep step = MeetPrePlaceStepFor(bot->GetDistance2d(task.x, task.y), moving,"
                      " ownWalk);", hold)
        self.assertLess(hold.index("if (step == MeetPrePlaceStep::Walk)\n            return true;"),
                        hold.index("hasLast = false;"))
        self.assertIn("if (step == MeetPrePlaceStep::HoldStop)\n            ai->StopMoving();", hold)
        self.assertIn("return MeetPrePlaceStepConsumesTick(step);", hold)
        # No cast stop and no attack stop: the rotation and the threat go on.
        self.assertNotIn("CastStop", hold)
        self.assertNotIn("AttackStop", hold)
        self.assertNotIn("InterruptNonMeleeSpells", hold)

    def test_the_rows_are_documented(self):
        self.assertIn("event=preplace,receiver=<name>,receiver_yd=%.1f,point_yd=%.1f", ROWS)
        self.assertIn("event=preplace_end", ROWS)
        self.assertIn("event=preplace_skip,receiver=<name>,reason=floor", ROWS)

    def test_precedence_and_limits_are_documented(self):
        boss = (ROOT / "docs/play/boss-tactics.md").read_text()
        for doc in (SETTINGS, boss):
            self.assertIn("comes first", doc)
            self.assertIn("2 yd inside the tanks' circle edge", doc)
        self.assertIn("does not follow the stand's re-plan", SETTINGS)
        self.assertIn("own pull of a Son that wanders into the tanks' reach waits", SETTINGS)
        self.assertIn("up to 40 yd from the gather spot", SETTINGS)
        self.assertIn("replaces the pre-place", SETTINGS)
        # Round 2: no threat claim, the melee-reach note, and the two new limits.
        for doc in (SETTINGS, boss):
            self.assertNotIn("keeps its threat", doc)
            self.assertIn("walk 57 watches tanks[2]'s threat", doc)
        self.assertIn("its rotation keeps the tick", SETTINGS)
        self.assertIn("may stop the dodge's walk at the effect's edge", SETTINGS)
        self.assertIn("height is re-derived from the ground", SETTINGS)
        self.assertIn("the tank dies, is charmed or becomes Hakkar's\n    // victim", CONFIG_H)
        self.assertIn("receiver_yd=%.1f", ROWS)


if __name__ == "__main__":
    unittest.main()
