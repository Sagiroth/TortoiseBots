"""Every pick path that bypasses the attackers list asks the keep-out set-aside book.

AttackersValue::IgnoreTarget filters the attackers list, but the group tank's
victim and the raid marks are picked without it. Before review round 2 of the
creature aura keep-out, the tank's victim handed a set-aside mob straight back
(hold 3 s, set aside, re-pick, all fight). The loop itself is simulated in
tools/test_creature_aura_keepout_policy.cpp; this test pins the wiring.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
VALUES = ROOT / "ai/playerbot/strategy/values"


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class KeepoutSetAsideWiringTest(unittest.TestCase):
    def test_group_tank_target_refuses_set_aside(self):
        src = (VALUES / "DpsTargetValue.cpp").read_text()
        fn = body(src, "Unit* GetGroupTankTarget(", "\n    }\n}")
        self.assertIn("AttackersValue::IsKeepoutSetAside(target, bot)", fn)

    def test_rti_branches_refuse_set_aside(self):
        src = (VALUES / "DpsTargetValue.cpp").read_text()
        for start in ("Unit* DpsTargetValue::Calculate()", "Unit* DpsAoeTargetValue::Calculate()"):
            fn = body(src, start, "\n}\n")
            self.assertRegex(fn, r"if \(rti && !AttackersValue::IsKeepoutSetAside\(rti, bot\)\) return rti;", start)
        tank = body((VALUES / "TankTargetValue.cpp").read_text(), "Unit* TankTargetValue::Calculate()", "\n}\n")
        self.assertIn("AttackersValue::IsKeepoutSetAside(rti, bot)", tank)

    def test_focus_rti_and_ignore_target_ask_the_book(self):
        src = (VALUES / "AttackersValue.cpp").read_text()
        focus = body(src, '"focus rti targets"', "return result;")
        self.assertIn("!IsKeepoutSetAside(rtiTarget, bot)", focus)
        ignore = body(src, "bool AttackersValue::IgnoreTarget(", "\n}\n")
        self.assertIn("IsKeepoutSetAside(target, playerToCheckAgainst)", ignore)
        book = body(src, "bool AttackersValue::IsKeepoutSetAside(", "\n}\n")
        self.assertIn("CreatureAuraSetAsideHolds(", book)

    def test_lift_does_not_depend_on_the_bot_distance(self):
        # Owner case: the tank drags the mob out on the far side, or it runs
        # to a healer 20-30 yd back. A mob with no listed creature within the
        # ring is unringed at any distance, so the book must judge it by the
        # creature-to-mob distance only (no bot-distance guard).
        src = (VALUES / "AttackersValue.cpp").read_text()
        book = body(src, "bool AttackersValue::IsKeepoutSetAside(", "\n}\n")
        self.assertIn('"possible triggers"', book)
        self.assertIn("CreatureAuraSetAsideRinged(*row,", book)
        self.assertNotIn("kMaxAoeAvoidRadiusYd", book)
        self.assertNotIn("bot->GetPositionX()", book)
        self.assertNotIn("GetDistance", book)

    def test_set_aside_writes_the_book_and_an_order_erases_it(self):
        move = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
        hold = body(move, "bool MovementAction::HoldChaseOutsideKeepout(", "\n}\n")
        self.assertIn('"keepout set aside"', hold)
        self.assertNotIn('"unreachable targets"', hold)
        order = body((ROOT / "ai/playerbot/strategy/actions/AttackAction.cpp").read_text(),
                     "void CommitExplicitAttackTarget(", "\n}\n")
        self.assertRegex(order, r'"keepout set aside"\)->Get\(\)\.erase\(guid\)')

    def test_set_aside_leaves_a_trace(self):
        # Live run 2 (2026-10-13) could not prove the set-aside fired: its
        # only message was a debug-move whisper. One action-log row and one
        # detail log line per set-aside, never per tick.
        move = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
        hold = body(move, "bool MovementAction::HoldChaseOutsideKeepout(", "\n}\n")
        self.assertIn('"KEEPOUT_SET_ASIDE"', hold)
        self.assertIn('[BOT CHASE] %s: set aside', hold)

    def test_gap_closers_and_end_points_ask_the_ring(self):
        # Charge / Intercept / Feral Charge are CastReachTargetSpellAction.
        reach = (ROOT / "ai/playerbot/strategy/actions/ReachTargetActions.h").read_text()
        cls = body(reach, "class CastReachTargetSpellAction", "\n    };\n")
        self.assertIn("GapCloserIntoKeepout()", cls)
        self.assertIn("CreatureAuraGapCloserVetoed(", cls)
        self.assertIn('"explicit attack target"', cls)
        for spell in ("CastChargeAction", "CastInterceptAction"):
            self.assertRegex((ROOT / "ai/playerbot/strategy/warrior/WarriorActions.h").read_text(),
                             r"REACH_ACTION\(" + spell + r",")
        self.assertIn("class CastFeralChargeBearAction : public CastReachTargetSpellAction",
                      (ROOT / "ai/playerbot/strategy/druid/DruidActions.h").read_text())
        move = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
        for start in ("bool SetBehindTargetAction::Execute(", "bool RearFlankAction::Execute(",
                      "bool MoveOutOfCollisionAction::Execute("):
            self.assertIn("EndPointInCreatureAuraKeepout(", body(move, start, "\n}\n"), start)


if __name__ == "__main__":
    unittest.main()
