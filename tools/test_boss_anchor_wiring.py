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
POSITION = (ROOT / "ai/playerbot/strategy/actions/PositionAction.cpp").read_text()
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/GenericTriggers.cpp").read_text()

CLAMP = "ClampMoveToBossAnchor(ai, bot, "
# The pre-anchor hazard dodge of MovementAction::ChaseTo, kept verbatim (one indent deeper) for the anchor-less path.
ORIGINAL_DODGE = """            // Generate point translated to the left
            Vector3 possibleEndPoint = CalculatePerpendicularPoint(endPoint, hazardPoint, hazardRangeOffset, true).GetVector3();

            // Check if point is valid
            WorldPosition possibleEndPosition(bot->GetMapId(), possibleEndPoint.x, possibleEndPoint.y, possibleEndPoint.z);
            if (IsValidPosition(possibleEndPosition, botPosition))
            {
                endPosition.x = possibleEndPoint.x;
                endPosition.y = possibleEndPoint.y;
                endPosition.z = possibleEndPoint.z;
            }
            else
            {
                // Generate point translated to the right
                possibleEndPoint = CalculatePerpendicularPoint(endPoint, hazardPoint, hazardRangeOffset, false).GetVector3();

                endPosition.x = possibleEndPoint.x;
                endPosition.y = possibleEndPoint.y;
                endPosition.z = possibleEndPoint.z;
            }"""


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
        self.assertIn("LiveBossAnchorRow(ai, bot)",
                      body(ACTIONS, "bool ai::LiveBossAnchor(PlayerbotAI* ai, Player* bot, BossAnchorCircle& out)"))
        live = body(ACTIONS, "const BossTacticRow* ai::LiveBossAnchorRow(PlayerbotAI* ai, Player* bot)")
        # Data-gated: a map without an anchor row costs one lookup and clamps nothing.
        self.assertIn("HasAnchorMap(bot->GetMapId())", live)
        self.assertIn("BossTacticKind::Anchor", live)
        self.assertIn("ScanBossTactics(ai, bot)", live)
        self.assertIn("IsInEvadeMode()", live)
        self.assertIn("return row;", live)

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
        self.assertIn("LiveBossAnchorRow(ai, bot)", fn)
        # The latch keeps only a Son inside the tanks' reach; the new pick judges each Son by AnchorSeekPull.
        self.assertIn("SeekPullSonInAnchor(tankAnchor, latchedPull->", fn)
        self.assertIn("thirdTank ? kind == AnchorPull::TankWalk", fn)
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


class BossAnchorFixRound1(unittest.TestCase):
    def test_return_to_stay_walk_goes_to_the_clamped_point(self):
        # The main tank's stay (the lead's authored approach, or a landed pullback's anchor) is walked to through the
        # clamp; the trigger, the action's isUseful and its dispatch all judge the same point, so the back-off
        # (StayWalkDispatched) handles a point the clamp refuses.
        point = body(POSITION, "bool ai::StayWalkPoint(PlayerbotAI* ai, Player* bot, PositionEntry& stay)")
        self.assertIn(CLAMP + "x, y, z)", point)
        due = body(POSITION, "bool ai::StayWalkDueNow(PlayerbotAI* ai, Player* bot, PositionEntry const& stayPosition)")
        self.assertIn("StayWalkPoint(ai, bot, point)", due)
        fn = body(POSITION, "bool ReturnToStayPositionAction::Execute(Event& event)")
        self.assertIn("StayWalkPoint(ai, bot, point)", fn)
        self.assertNotIn("MoveToPositionAction::Execute(event)", fn)
        self.assertLess(fn.index("StayWalkPoint(ai, bot, point)"), fn.index("MoveTo(bot->GetMapId(), point.x, point.y, point.z"))
        self.assertIn("StayWalkDispatched(backoff, moved", fn)
        self.assertIn("StayWalkDueNow(ai, bot, stayPosition)", TRIGGERS)

    def test_ranged_fallback_shoots_from_the_anchor(self):
        read = body(ACTIONS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("AnchorSeekPull(", read)
        self.assertIn("AnchorRangedShotStand(", read)
        self.assertIn("AnchorShotStandFor(bot, shotAnchor, candidate, shotRange, sx, sy,", read)
        stand = body(ACTIONS, "bool AnchorShotStandFor(Player* bot, const BossAnchorCircle& c, Unit* son, float rangeYd,")
        self.assertIn("isInLineOfSight(", stand)
        self.assertIn("AnchorFloorOk(", stand)
        self.assertIn("FindSeekFallbackPuller(ai, bot, roles, boss, *row, anchorShot)", read)
        tank = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        self.assertIn("state.anchorShot", tank)
        self.assertIn("LogSeekPullRangedFromAnchor(ai, bot)", ACTIONS)
        self.assertIn('"seek pull: ranged from anchor"', ACTIONS)
        fn = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        self.assertIn("task.fromAnchor", fn)

    def test_cloud_outside_the_anchor_is_ignored(self):
        fn = body(ACTIONS, "SeekTask ai::FindBossSeek(PlayerbotAI* ai, Player* bot)")
        self.assertIn("AnchorReachesCloud(cloudAnchor, dyn->GetPositionX(), dyn->GetPositionY(), dyn->GetRadius())", fn)
        self.assertLess(fn.index("AnchorReachesCloud("), fn.index("cloud = dyn;"))

    def test_spread_rests_when_the_anchor_rejects_every_heading(self):
        fn = body(DUNGEON, "bool RaidSpreadAction::Execute(Event& event)")
        self.assertIn("anchorRejected = true;", fn)
        tail = fn[fn.rindex("anchorRejected"):]
        self.assertIn("lastMove.lastSpreadStepMs = WorldTimer::getMSTime();", fn[fn.index("if (anchorRejected)"):])

    def test_take_of_a_son_outside_drops_the_chase(self):
        fn = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        take = fn[fn.index("chaseRefused"):fn.index("if (task.pull)")]
        self.assertIn('SET_AI_VALUE(Unit*, "current target", nullptr);', take)
        self.assertIn("ai->StopMoving();", take)


class BossAnchorChase(unittest.TestCase):
    def test_chase_to_enforces_a_live_anchor(self):
        fn = body(MOVEMENT, "bool MovementAction::ChaseTo(WorldObject* obj, float distance, float angle)")
        self.assertIn("const bool anchored = LiveBossAnchor(ai, bot, anchor);", fn)
        # Refused before any chase branch: a hostile beyond the radius plus the melee reach.
        refuse = fn.index("AnchorAllowsChase(anchor, obj->GetPositionX(), obj->GetPositionY(), kBossAnchorChaseReachYd)")
        self.assertLess(refuse, fn.index("if (distanceToTarget > sPlayerbotAIConfig.sightDistance"))
        # The far walk's destination and the chase end point are clamped, and MoveChase never starts toward a target
        # outside the circle.
        self.assertIn("if (anchored && !ClampMoveToBossAnchor(ai, bot, farX, farY, farZ))", fn)
        self.assertIn("if (anchored && !ClampMoveToBossAnchor(ai, bot, endX, endY, endZ))", fn)
        self.assertLess(fn.index("ClampMoveToBossAnchor(ai, bot, endX, endY, endZ)"), fn.index("IsHazardNearPosition(endPosition"))
        self.assertIn("if (anchored && !InsideAnchor(anchor, obj->GetPositionX(), obj->GetPositionY()))", fn)
        self.assertLess(fn.index("!InsideAnchor(anchor, obj->GetPositionX(), obj->GetPositionY())"),
                        fn.index("mm.MoveChase((Unit*)obj, distance, angle);"))
        # The hazard dodge that replaces the end point is clamped and re-checked, and the hazard-avoiding path's points
        # must all stay inside; otherwise the move is refused this tick.
        hazard = fn[fn.index("if (IsHazardNearPosition(endPosition, &hazardPosition))"):fn.index("MotionMaster& mm")]
        # Under the anchor both dodges, left then right, run the full pipeline (clamp, floor, hazard, navigation) and the
        # first that passes is taken; refused only when both fail.
        anchored_block = hazard[hazard.index("if (anchored)\n"):hazard.index("        else\n        {\n            // The original dodge")]
        self.assertIn("AnchorPickDodge(anchor, hazardPoint.x, hazardPoint.y, hazardPosition.second, dodgeXs, dodgeYs,", anchored_block)
        self.assertIn("CalculatePerpendicularPoint(endPoint, hazardPoint, hazardRangeOffset, true)", anchored_block)
        self.assertIn("CalculatePerpendicularPoint(endPoint, hazardPoint, hazardRangeOffset, false)", anchored_block)
        for check in ("AnchorFloorOk(anchor,", "IsHazardNearPosition(candidate)", "!IsValidPosition(candidate, botPosition)"):
            self.assertIn(check, anchored_block)
        self.assertIn("< 0)\n                return false;", anchored_block)
        # Without an anchor the dodge is exactly the old one.
        self.assertIn(ORIGINAL_DODGE, hazard)
        spline = fn[fn.index("bool const hazardPath = GeneratePathAvoidingHazards(path);"):fn.index("init.Launch();")]
        self.assertIn("if (anchored && hazardPath && !AnchorPathInside(anchor, pathXY))", spline)
        self.assertLess(spline.index("AnchorPathInside("), spline.index("if (hazardPath)\n"))
        self.assertLess(spline.index("AnchorPathInside("), spline.index("mm.Clear(false, true);"))
        # Gated: every anchor branch asks `anchored` first, so without a live anchor row nothing changes.
        self.assertIn("if (anchored && chased && bot->IsHostileTo(chased) &&\n        !AnchorAllowsChase(", fn)
        for line in fn.splitlines():
            if "ClampMoveToBossAnchor(" in line or "InsideAnchor(" in line:
                self.assertIn("anchored", line, line)


if __name__ == "__main__":
    unittest.main()
