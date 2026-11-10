"""The hover-aware ranged reach glue (AiPlayerbot.RangedReachHoverTarget).

Onyxia in P2 hovers 19-23 yd above her floor point with a 22 yd combat reach: the
"enemy out of spell range" trigger measures edge to edge, the path walk measures
centre to centre, so no floor point is in range under her and the reach stood
still. The policy is tested in tools/test_ranged_approach_policy.cpp; this pins
the glue in ai/playerbot/strategy/actions/ReachTargetActions.h:
- the base MoveIntoRange asks the hover walk first, only with the key on, before
  the plain path walk;
- the hover walk paths to the floor under the target, never to the target;
- the walk runs only for a hostile target (the trigger's own IsHostileTo), and for
  an attack reach only one the core lets the bot attack;
- the BossReachHover row is written only inside the key-on branch;
and the key's read and shipped default.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/ReachTargetActions.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONF = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class HoverReachWiringTest(unittest.TestCase):
    def test_base_move_asks_the_hover_walk_first(self):
        move = body(ACTIONS, "virtual bool MoveIntoRange(Unit* target, float distance)", "\n        }")
        self.assertIn("if (sPlayerbotAIConfig.rangedReachHoverTarget && MoveIntoRangeUnderHover(target, distance))", move)
        self.assertLess(move.index("MoveIntoRangeUnderHover"), move.index("MoveIntoRangeAlongPath"))
        self.assertLess(move.index("MoveIntoRangeAlongPath"), move.index("ChaseTo"))

    def test_hover_walk_paths_to_the_ground_under_the_target(self):
        fn = body(ACTIONS, "bool MoveIntoRangeUnderHover(Unit* target, float distance)", "\n        }")
        self.assertIn("float const groundZ = map->GetHeight(targetX, targetY, targetZ, true);", fn)
        self.assertIn("bool const groundValid = groundZ > INVALID_HEIGHT + 1.0f;", fn)
        self.assertIn("if (!HoverReachApplies(true, map->IsDungeon()", fn)
        self.assertIn("WorldPosition const ground(bot->GetMapId(), targetX, targetY, groundZ);", fn)
        self.assertIn("ground.getPathStepFrom(WorldPosition(bot), bot)", fn)
        self.assertNotIn("target.getPathStepFrom", fn)
        self.assertNotIn("WorldPosition const target(", fn)
        self.assertIn("if (IsHazardNearPosition(destination))", fn)
        self.assertIn("HoverReachResult::NoGround", fn)
        self.assertIn("HoverReachResult::NoPath", fn)
        self.assertIn("HoverReachResult::NoPoint", fn)

    def test_hover_row_is_key_gated(self):
        fn = body(ACTIONS, "void HoverReachRow(HoverReachResult result", "\n        }")
        guard = fn.index('if (!sPlayerbotAIConfig.rangedReachHoverTarget || !sPlayerbotAIConfig.hasLog("bot_events.csv"))')
        self.assertLess(guard, fn.index('sPlayerbotAIConfig.logEvent(ai, "BossReachHover"'))
        self.assertEqual(ACTIONS.count('logEvent(ai, "BossReachHover"'), 1)
        self.assertIn("lastHoverLogMs != 0 && nowMs - lastHoverLogMs < kHoverLogMs", fn)

    def test_neutral_or_unattackable_target_cannot_select_a_hover_destination(self):
        fn = body(ACTIONS, "bool MoveIntoRangeUnderHover(Unit* target, float distance)", "\n        }")
        # EnemyOutOfSpellRangeTrigger judges with IsHostileTo; !IsFriendlyTo also
        # takes neutral units, which would get a hover destination.
        self.assertIn("bool const hostile = sServerFacade.IsHostileTo(bot, target);", fn)
        self.assertNotIn("IsFriendlyTo(bot, target)", fn)
        self.assertIn("bool const attackReach = RequiresAttackableTarget();", fn)
        self.assertIn("bool const attackable = !attackReach || bot->IsValidAttackTarget(target);", fn)
        self.assertIn("if (!HoverReachTargetEligible(hostile, attackReach, attackable))", fn)
        self.assertLess(fn.index("if (!HoverReachTargetEligible(hostile, attackReach, attackable))"),
                        fn.index("if (!HoverReachApplies(true, map->IsDungeon()"))

    def test_key_is_read_and_ships_zero(self):
        self.assertIn('rangedReachHoverTarget = config.GetBoolDefault("AiPlayerbot.RangedReachHoverTarget", false);',
                      CONFIG_CPP)
        self.assertIn("bool rangedReachHoverTarget = false;", CONFIG_H)
        self.assertIn("AiPlayerbot.RangedReachHoverTarget = 0", CONF)


if __name__ == "__main__":
    unittest.main()
