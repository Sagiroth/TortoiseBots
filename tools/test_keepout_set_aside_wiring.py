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
            # The mark is refused when the keep-out set it aside (this book) and when a hold_kill or one_at_a_time
            # row holds it (HeldDpsTarget): both drop it to the next pick. The dps target's condition spans a second
            # line as well (a mark on a held linked_death member drops too), so the two checks are pinned by name.
            self.assertIn("rti && !AttackersValue::IsKeepoutSetAside(rti, bot) && !HeldDpsTarget(ai, bot, rti)", fn)
            branch = fn[fn.index("Unit* rti = RtiTargetValue::Calculate();"):]
            branch = branch[:branch.index("return rti;")]
            self.assertIn("IsKeepoutSetAside(rti, bot)", branch)
            self.assertIn("HeldDpsTarget(ai, bot, rti)", branch)
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
        self.assertIn("PossibleTriggersValue::GapCloserIntoKeepout(", cls)
        warrior = (ROOT / "ai/playerbot/strategy/warrior/WarriorActions.h").read_text()
        for spell in ("CastChargeAction", "CastInterceptAction"):
            self.assertRegex(warrior, r"REACH_ACTION\(" + spell + r",")
        # Intercept on the enemy healer or the snare target is gated the same way.
        for cls_name in ("CastInterceptOnEnemyHealerAction", "CastInterceptOnSnareTargetAction"):
            self.assertNotRegex(warrior, r"_ACTION\(" + cls_name + r",")
            gated = body(warrior, "class " + cls_name, "\n    };\n")
            self.assertIn("PossibleTriggersValue::GapCloserIntoKeepout(ai, GetSpellName(), GetTarget())", gated)
        self.assertIn("PossibleTriggersValue::GapCloserIntoKeepout(ai, GetSpellName(), ", cls)
        # The shared veto: cached list first, then the bot's hazard snapshot
        # (Charge fires past the 15 yd list).
        values = (VALUES / "PossibleTargetsValue.cpp").read_text()
        veto = body(values, "bool PossibleTriggersValue::GapCloserIntoKeepout(", "\n}\n")
        self.assertIn('"explicit attack target"', veto)
        self.assertIn("CreatureAuraGapCloserVetoed(", veto)
        self.assertLess(veto.index("KeepoutRingAt(ai, lx, ly, nullptr, isTank)"), veto.index("SnapshotRingedAt(ai, lx, ly, target->GetPositionZ(), target, true, isTank)"))
        self.assertNotIn("Cell::", veto)
        self.assertIn("class CastFeralChargeBearAction : public CastReachTargetSpellAction",
                      (ROOT / "ai/playerbot/strategy/druid/DruidActions.h").read_text())
        move = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
        for start in ("bool SetBehindTargetAction::Execute(", "bool RearFlankAction::Execute(",
                      "bool MoveOutOfCollisionAction::Execute("):
            self.assertIn("EndPointInCreatureAuraKeepout(", body(move, start, "\n}\n"), start)

    def test_gap_closer_lookup_waits_for_a_castable_spell(self):
        # Review #882 round 2: the engine asks isUseful() before the stance
        # prerequisite and isPossible(), every tick the trigger fires. The
        # landing checks sit behind the readiness gate; the grid visit lives
        # only in the per-bot hazard snapshot.
        values = (VALUES / "PossibleTargetsValue.cpp").read_text()
        veto = body(values, "bool PossibleTriggersValue::GapCloserIntoKeepout(", "\n}\n")
        gate = veto.index("if (!GapCloserCouldFire(ai, spell, target))\n        return false;")
        self.assertLess(veto.index("if (ordered)"), gate)
        self.assertLess(veto.index("CreatureAuraMapHasNonTankExemptRow("), gate)
        for later in ("KeepoutFor(target)", "KeepoutRingAt(ai, lx, ly, nullptr, isTank)", "SnapshotRingedAt("):
            self.assertLess(gate, veto.index(later), later)
        # The gate mirrors isPossible's refusals and leaves the stance to the
        # prerequisite (gating on it would switch stance, then veto).
        ready = body(values, "bool PossibleTriggersValue::GapCloserCouldFire(", "\n}\n")
        for probe in ('"spell id", spell', "UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL", "sServerFacade.IsSpellReady(bot, spellId)",
                      "Spell::CalculatePowerCost(spellInfo, bot, nullptr, nullptr)", "IsNonCombatSpell()",
                      "IsInCombat()", "ai->GetSpellRange(spell, &maxRange, &minRange)",
                      "CreatureAuraGapCloserCouldFire(r)"):
            self.assertIn(probe, ready)
        self.assertNotIn("GetErrorAtShapeshiftedCast", ready)
        self.assertNotIn("Cell::", ready)
        header = (VALUES / "PossibleTargetsValue.h").read_text()
        self.assertNotIn("LandingMemo", header)
        self.assertNotIn("never per tick", header)

    def test_end_points_and_landings_read_one_hazard_snapshot(self):
        # Reviews of #995: nothing per bot tick scans the world (AGENTS.md).
        # The snapshot's behaviour (cell and instance key, budget, live skip,
        # height, cover, drift) is tested through CreatureAuraSnapshotRinged
        # in test_creature_aura_keepout_policy.cpp; here only the wiring.
        move = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
        end = body(move, "bool MovementAction::EndPointInCreatureAuraKeepout(", "\n}\n")
        self.assertIn("PossibleTriggersValue::EndPointRinged(ai, x, y, z, ", end)
        values = (VALUES / "PossibleTargetsValue.cpp").read_text()
        ring = body(values, "bool PossibleTriggersValue::EndPointRinged(", "\n}\n")
        self.assertLess(ring.index("KeepoutRingAt("), ring.index("SnapshotRingedAt("))
        snap = body(values, "bool PossibleTriggersValue::SnapshotRingedAt(", "\n}\n")
        self.assertIn("CreatureAuraSnapshotRinged(", snap)
        self.assertIn("GetInstanceId()", snap)
        self.assertIn('"possible triggers"', snap)  # live-list creatures are skipped
        self.assertLess(snap.index("if (maxHazard <= 0.0f)"), snap.index("CreatureAuraSnapshotRinged("))
        # The one grid visit: inside the snapshot refresh, never loading a grid.
        self.assertIn("Cell::VisitAllObjects(px, py, map, searcher, radius, true)", snap)
        self.assertEqual(snap.count("Cell::VisitAllObjects("), 1)
        # A gap-closer landing never fails open: without a usable snapshot it
        # gets #882's bounded lookup (CreatureAuraLandingRinged).
        self.assertIn("if (landingLookup)", snap)
        self.assertIn("CreatureAuraLandingRinged(", snap)
        for name in ("EndPointRinged", "GapCloserIntoKeepout", "KeepoutRingAt"):
            fn = body(values, "PossibleTriggersValue::" + name + "(", "\n}\n")
            self.assertNotIn("Cell::", fn, name)
        # Budget reset once per world update before any AI runs; counters on BOTPERF.
        manager = (ROOT / "runtime/BotManager.cpp").read_text()
        update = body(manager, "void BotManager::OnWorldUpdate(", "\n}\n")
        self.assertLess(update.index("PossibleTriggersValue::BeginKeepoutTick()"), update.index("UpdateBots(diff)"))
        self.assertIn("keepoutScans=%u keepoutBudgetMiss=%u", manager)

    def test_end_point_height_is_the_destination_floor(self):
        # Review of #995 (bdeb36cc): the snapshot's 5 yd height filter must
        # judge the floor the bot will stand on, so each candidate is ground-
        # resolved before the keep-out check, never checked at the mob's Z.
        move = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
        for start in ("bool RearFlankAction::Execute(", "bool SetBehindTargetAction::Execute("):
            fn = body(move, start, "\n}\n")
            calls = [m.start() for m in re.finditer(r"EndPointInCreatureAuraKeepout\(", fn)]
            self.assertTrue(calls, start)
            for at in calls:
                self.assertLess(fn.index("UpdateGroundPositionZ("), at, start)
                call = fn[at:fn.index(")", fn.index(")", at) + 1) + 1]
                self.assertNotIn("getPositionZ()", call, start)
        flank = body(move, "bool RearFlankAction::Execute(", "\n}\n")
        self.assertIn("EndPointInCreatureAuraKeepout(leftX, leftY, leftZ)", flank)
        self.assertIn("EndPointInCreatureAuraKeepout(rightX, rightY, rightZ)", flank)
        self.assertLess(flank.index("resolve(rightX, rightY, rightZ)"), flank.index("EndPointInCreatureAuraKeepout("))
        self.assertIn("float const destZ = leftFirst ? leftZ : rightZ;", flank)
        # Move out of collision: the random point is already on the ground;
        # the old-style fallback moves at the bot's own height and is checked there.
        unstack = body(move, "bool MoveOutOfCollisionAction::Execute(", "\n}\n")
        self.assertIn("GetReachableRandomPointOnGround(", unstack)
        self.assertIn("randomPoint.getZ())", unstack)
        self.assertIn("EndPointInCreatureAuraKeepout(oldX, oldY, bot->getPositionZ())", unstack)
        self.assertIn("MoveTo(bot->GetMapId(), oldX, oldY, bot->getPositionZ())", unstack)

    def test_lethal_keepout_retries_and_steps_straight_away(self):
        # Codex review of #1038: a lethal row (Living Poison) skips the 2 s
        # no-landing back-off and, with every landing refused, steps straight
        # away from the creature.
        move = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
        execute = body(move, "bool AvoidAoeAction::Execute(", "\n}\n")
        self.assertIn("CreatureAuraNoLandingUntilMs(*keepoutRow", execute)
        self.assertNotIn("+ 2000", execute)
        strafe = body(move, "bool AvoidAoeAction::StrafeToSafety(", "\n}\n")
        self.assertIn("CreatureAuraStepAwayPoint(", strafe)
        self.assertIn("!keepoutRow->tankExempt", strafe)
        self.assertLess(strafe.index("MoveTo(bot->GetMapId(), out.getX()"), strafe.index("CreatureAuraStepAwayPoint("))


if __name__ == "__main__":
    unittest.main()
