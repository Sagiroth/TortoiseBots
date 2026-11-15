"""The magma guard's glue (RFC r3-19z20, MagmaHazardPolicy.h).

The lead tank's idle formation station walk took it into Ragefire Chasm's
lava basin and the party followed (5 deaths). The pure rules are tested in
tools/test_magma_hazard_policy.cpp; this pins where the movement layer and the
run leader call them, so the glue cannot drift away silently:
- MoveTo2 checks the refusal memory first, then the end point, then the
  route, each refusal returns false with MOVE_FAIL_MAGMA, and a route into
  magma is refused whole (nothing dispatched, no hop to its dry part);
- a route refusal is remembered from the bot's origin cell only, and logged
  to the module log;
- a commander task's walk nudge (a plain core MovePoint) waits after a magma
  refusal and never walks onto a stand in magma;
- the formation station trigger does not fire for a slot in magma;
- the run leader holds the lead on "stay" while resting, unless it stands in
  magma, before the rest order goes out.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MOVES = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/RangeTriggers.h").read_text()
COMMANDS = (ROOT / "commands/Commander.cpp").read_text()
WORLDPOS = (ROOT / "ai/playerbot/WorldPosition.cpp").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


def at(text, needle):
    i = text.find(needle)
    if i < 0:
        raise AssertionError("missing: " + needle)
    return i


class MagmaGuardWiringTest(unittest.TestCase):
    def setUp(self):
        self.move2 = body(MOVES, "bool MovementAction::MoveTo2(", "\n}\n")

    def test_order_in_move_to2(self):
        f = self.move2
        guard = at(f, "bool const magmaGuard = ai::MagmaGuardApplies(bot->GetMap()->IsDungeon());")
        memory = at(f, "ai::MagmaRefusalActive(lastMove.magmaRefusal, endPos.GetMapId()")
        end = at(f, "ai::MagmaMoveProbe const endMove = ai::MagmaAtMoveZ(bot, endPos.getX()")
        clip = at(f, "movePath.ClipPath(ai, mover, ignoreEnemyTargets, packHold);")
        route = at(f, "RouteEntersMagma(bot, startPos, movePath, magmaAt)")
        dispatch = at(f, "DispatchMovement(movePath, generatePath, masterWalking)")
        teleport = at(f, "bot->TeleportTo(teleportPosition.GetMapId(),")
        self.assertTrue(guard < memory < end < clip < route < teleport < dispatch)

    def test_each_refusal_returns_false_with_magma(self):
        f = self.move2
        memory = f[at(f, "ai::MagmaRefusalActive("):]
        self.assertRegex(memory, r"^[^}]*lastMove\.moveFailReason = MOVE_FAIL_MAGMA;\s*return false;")
        end = f[at(f, "ai::MagmaMoveProbe const endMove = ai::MagmaAtMoveZ(bot, endPos.getX()"):]
        self.assertRegex(end, r"^[^}]*PlanMagmaMoveZ\(true, endMove\.moveInMagma, endMove\.navmeshDry, "
                               r"endMove\.navmeshZ,\s*endMove\.moveZ\) == ai::MagmaMoveZPlan::Refuse[\s\S]*?"
                               r"RefuseMagmaMove\(bot, lastMove, endPos, magmaAt, nullptr, \"end point\"\);\s*return false;")
        route = f[at(f, "RouteEntersMagma(bot, startPos, movePath, magmaAt)"):]
        self.assertRegex(route, r"^[^}]*RefuseMagmaMove\(bot, lastMove, endPos, magmaAt, &startPos, \"route\"\);\s*return false;")

    def test_refusal_sets_memory_and_reason(self):
        f = body(MOVES, "static void RefuseMagmaMove(", "\n}\n")
        self.assertIn("lastMove.magmaRefusal = origin", f)
        self.assertIn("? ai::MagmaRouteRefusalFor(endPos.GetMapId()", f)
        self.assertIn("origin->getX(), origin->getY(), origin->getZ(), now)", f)
        self.assertIn(": ai::MagmaRefusalFor(endPos.GetMapId()", f)
        self.assertIn("lastMove.moveFailReason = MOVE_FAIL_MAGMA;", f)

    def test_memory_is_keyed_on_the_bots_origin(self):
        f = self.move2
        self.assertIn("bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), static_cast<std::int64_t>(time(nullptr))))",
                      f[at(f, "ai::MagmaRefusalActive("):])

    def test_refusal_logs_to_the_module_log(self):
        f = body(MOVES, "static void LogMagmaRefusal(", "\n}\n")
        self.assertIn("TB_LOG_DETAIL(", f)
        self.assertIn('#include "host/ModuleLog.h"', MOVES)

    def test_task_walk_nudge_waits_after_a_magma_refusal(self):
        f = body(COMMANDS, "void NudgeTaskWalk(CommanderTask& task, Player* bot, uint32 diff)", "\n}\n")
        blocked = at(f, "if (ai::MagmaNudgeBlocked(guard, lastMove.magmaRefusal, bot->GetMapId(),")
        # The refusal memory alone decides (Codex review 2026-10-23): MoveTo2 resets the
        # last-try reason, so a later non-magma try must not unblock the nudge.
        self.assertNotIn("moveFailReason", f)
        self.assertIn("bool const guard = ai::MagmaGuardApplies(bot->GetMap()->IsDungeon());", f)
        self.assertIn("guard && ai::WorldPosition(bot->GetMapId(), task.stand.x, task.stand.y, task.stand.z).isInMagma(", f)
        walk = at(f, "ai::MovementAction::LaunchLandWalk(bot, ai::WorldPosition(bot->GetMapId(), task.stand.x, task.stand.y, task.stand.z));")
        self.assertLess(blocked, walk)
        self.assertEqual(f.count("MovePoint("), 0)  # the guarded checked walk is the only launch
        self.assertEqual(f.count("LaunchLandWalk("), 1)
        self.assertRegex(f[blocked:walk], r"isInMagma\(\s*bot->GetMap\(\)->GetTerrain\(\)\)\)\)\s*return;")

    def test_dispatch_keeps_the_confirmed_floor_only(self):
        f = body(MOVES, "bool MovementAction::DispatchMovement(", "\n}\n")
        clampedAt = at(f, "float const clampedZ = p.z;")
        drop = at(f, "ai::KeepStairNavmeshZ(magmaFloorLowered,")
        keep = at(f, "ai::KeepNavmeshZOverMagma(dispatchMagmaGuard, magmaFloorLowered,")
        self.assertLess(clampedAt, drop)
        self.assertLess(drop, keep)
        self.assertIn("TerrainInfo const* dispatchTerrain = bot->GetMap()->GetTerrain();", f[:keep])
        self.assertIn("bool const dispatchMagmaGuard = ai::MagmaGuardApplies(bot->GetMap()->IsDungeon());", f[:keep])
        # `lowered` is the live comparison on the ORIGINAL clamp, not a
        # literal: passing true again would fire the guard on an unclamped
        # point, and reading p.z after the stair restore would classify the
        # restored point instead of the clamp.
        self.assertIn("float const clampedZ = p.z;", f[:keep])
        self.assertIn("bool const magmaFloorLowered = clampedZ < navmeshZ;", f[clampedAt:keep])
        # Cheap first: the clamped point's magma/slime status is checked before
        # the navmesh point's, and the vmap query only on the guard path,
        # before the restore. Round 2: the stair workaround must see clampedZ
        # and refuse a wet big drop, so it cannot restore around the
        # confirmed-floor check.
        clamped = at(f, "WorldPosition(bot->GetMapId(), p.x, p.y, clampedZ).isInMagma(dispatchTerrain)")
        slime = at(f, "WorldPosition(bot->GetMapId(), p.x, p.y, clampedZ).isInSlime(dispatchTerrain)")
        navmesh = at(f, "WorldPosition(bot->GetMapId(), p.x, p.y, navmeshZ).isInMagma(dispatchTerrain)")
        vmap = at(f, "VMAP::VMapFactory::createOrGetVMapManager()->getHeight(")
        restore = at(f[keep:], "p.z = navmeshZ;") + keep
        self.assertLess(clampedAt, clamped)
        self.assertLess(clampedAt, slime)
        self.assertLess(clamped, navmesh)
        self.assertLess(slime, navmesh)
        self.assertLess(navmesh, vmap)
        self.assertLess(vmap, keep)
        self.assertLess(keep, restore)
        self.assertIn("bot->GetMapId(), p.x, p.y, navmeshZ + kMagmaFloorProbeAboveZ, kMagmaFloorSearchDistZ)",
                      f[vmap:vmap + 400])
        self.assertIn("kMagmaFloorSearchDistZ", f)
        # The vmap ray is collision-only: no .map terrain fallback, no
        # dynamic tree, no Map::GetHeight retry with a 10000 yd search.
        self.assertNotIn("bot->GetMap()->GetHeight(p.x, p.y, navmeshZ", f[clampedAt:restore])
        self.assertNotIn("GetDynamicObjectHeight", f[clampedAt:restore])
        # The stair gate takes the ORIGINAL clamp: a wet big drop must not
        # restore there.
        self.assertIn("navmeshZ - clampedZ > kMaxWaypointDrop, magmaFloorClampedWet", f[drop:drop + 300])
        # The guard path runs only when the stair path refused: one restore.
        self.assertIn("!stairRestore", f[clampedAt:keep])
        # The vmap query runs only on the guard path: inside the
        # transport+lowered+guard+wet+dry `if`, before the restore.
        self.assertIn("if (!bot->GetTransport() && magmaFloorLowered && dispatchMagmaGuard && magmaFloorClampedWet &&",
                      f[clampedAt:vmap])
        self.assertIn('"TortoiseBots: %s: waypoint kept on the floor over magma at %.1f %.1f %.1f (clamp %.2f)"',
                      f[restore:])
        # The transport term: no restore while riding (passenger z is not a
        # floor height). Dropping it would restore over a transport ride.
        self.assertIn("!bot->GetTransport()", f[clampedAt:keep])
        # (void) casts only in mutation scratch, never in shipped tests.
        self.assertNotIn("(void)", f[clampedAt:at(f, "CalculatePassengerOffset(p.x, p.y, p.z);")])
    def test_route_check_never_edits_the_route(self):
        # Round 2: the ONE sampled liquid-hazard guard (magma, and slime for
        # the hand-off) owns the traversal; the magma-only entry keeps its name.
        f = body(MOVES, "static bool RouteEntersLiquidHazard(", "\n}\n")
        self.assertIn("ai::CutPathBeforeMagma(start, route,", f)
        self.assertIn("MAP_ALL_LIQUIDS", f)
        self.assertIn("ai::ClassifyLiquidHazard(", f)
        for edit in ("erase(", "addPoint(", "push_back(PathNodePoint", "points.clear("):
            self.assertNotIn(edit, f)

    def test_ordinary_moves_stay_magma_only(self):
        # Round 2: ordinary moves pass slime off, so slime stays allowed outside
        # the hand-off (Undercity); the hand-off passes true (pinned in
        # test_swim_path_wiring.py).
        f = body(MOVES, "static bool RouteEntersMagma(", "\n}\n")
        self.assertIn("RouteEntersLiquidHazard(bot, startPos, movePath, magmaAt, false,", f)

    def test_magma_query_is_the_cores(self):
        f = body(WORLDPOS, "bool WorldPosition::isInMagma(", "\n}\n")
        self.assertIn("getLiquidStatus(x, y, z + 0.01f, MAP_ALL_LIQUIDS, &liquid)", f)
        self.assertIn("ai::IsMagmaHazard(liquid.type_flags", f)

    def test_formation_station_skips_a_slot_in_magma(self):
        f = body(TRIGGERS, "class FormationStationTrigger", "\n    };\n")
        probe = at(f, "ai::MagmaMoveProbe const move = ai::MagmaAtMoveZ(bot, loc.x, loc.y, loc.z);")
        plan = at(f, "ai::PlanMagmaMoveZ(true, move.moveInMagma, move.navmeshDry, move.navmeshZ, move.moveZ)")
        step = at(f, "return FormationStationStep(memory, formation->GetSerial(),")
        self.assertLess(probe, step)
        self.assertLess(plan, step)
        # Only a clamp the navmesh cannot rescue skips the station; a dry
        # navmesh floor above the clamp is a walk MoveTo makes.
        self.assertIn("ai::MagmaMoveZPlan::Refuse)", f[plan:step])
        self.assertRegex(f[plan:step], r"Refuse\)\s*return false;")
        self.assertIn("TB_LOG_BASIC(", f[probe:plan])

    def test_run_leader_holds_the_lead_while_resting(self):
        f = body(COMMANDS, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)", "\n}\n")
        tick = at(f, "TortoiseBots::DungeonRun::Tick(slot.run, facts, dt);")
        due = at(f, "TortoiseBots::DungeonRun::LeadStayDueWhileResting(slot.run, lead->IsAlive(), lead->IsInCombat(),")
        issue = at(f, "IssueRunOrder(commander, order, answer,")
        self.assertTrue(tick < due < issue)
        hold = f[due:issue]
        self.assertIn('leadAi->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT), lead->IsInMagma(), facts.staleCombat))',
                      hold)
        # The stale flag is this tick's facts (THREAT F1 review round).
        self.assertLess(at(f, "ReadRunFacts(slot, lead, bots, facts, binding->raid);"), due)
        self.assertIn("FormUpOnLead(commander, lead, std::vector<Player*>{ lead }, true);", hold)
        self.assertIn("leadAi->StopMoving();", hold)

    def test_run_moves_are_logged_not_refused(self):
        f = body(COMMANDS, "static bool CommanderMove(ChatHandler* handler, CommanderOrder const& order, std::string const& args,",
                 "\n}\n")
        note = at(f, "NoteRunMagma(order.lead, request.point.x, request.point.y, request.point.z,")
        self.assertIn("request.direct ? RunMagmaNote::DirectMovePoint : RunMagmaNote::MovePoint,", f)
        direct = at(f, "return CommanderMoveDirect(handler, order, request.point, intent);")
        self.assertLess(note, direct)
        helper = body(COMMANDS, "static void NoteRunMagma(", "\n}\n")
        self.assertIn("time_t& last = lastLog[static_cast<size_t>(kind)];", helper)
        self.assertNotIn("return true", helper)
        self.assertNotIn("SendActionError", helper)

    def test_follow_holds_out_of_magma(self):
        # RFC first-bridge deaths 2026-11-13: the follow slot comes off the
        # navmesh (a shallow lava shelf reads as plain ground) and the native
        # follow is never sampled. The hold runs before the FOLLOW_MOTION_TYPE
        # early return, so a follow already running is re-checked, and before
        # the native dispatch for a new one.
        f = body(MOVES, "bool MovementAction::Follow(Unit* target, float distance, float angle)\n", "\n}\n")
        hold = at(f, "if (HoldFollowOutOfMagma(target, distance, angle))")
        running = at(f, "if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)")
        dispatch = at(f, "mm.MoveFollow(target, distance, angle);")
        self.assertLess(hold, running)
        self.assertLess(hold, dispatch)
        # Both follow call sites reach this one guarded dispatch: the 2-arg
        # overload (Follow(target, distance)) delegates, and every party
        # form-up (FormUpOnLead, FormUpOnPlayer) orders the "follow chat
        # shortcut" action, which is this MovementAction.
        two = body(MOVES, "bool MovementAction::Follow(Unit* target, float distance)\n", "\n}\n")
        self.assertIn("return Follow(target, distance, GetFollowAngle());", two)
        self.assertNotIn("MoveFollow(", two)
        for formup in ("uint32 FormUpOnPlayer(Player* commander",
                       "uint32 FormUpOnLead(Player* commander"):
            site = body(COMMANDS, formup, "\n}\n")
            self.assertIn('"follow chat shortcut", ai::Event("follow", "", commander)', site)
            self.assertNotIn("MoveFollow(", site)

    def test_follow_hold_rule_and_fallbacks(self):
        f = body(MOVES, "bool MovementAction::HoldFollowOutOfMagma(", "\n}\n")
        slot = at(f, "WorldPosition const slot(target->GetMapId(),")
        check = at(f, "slotInMagma = SlotGoesIntoMagma(bot, slot, magmaAt, &slotMove);")
        self.assertLess(slot, check)
        # The slot is probed at the z the core will MOVE the bot to (its own
        # clamp), not the navmesh z it was planned at (RFC 2026-11-14).
        self.assertIn("ai::MagmaMoveProbe slotMove;", f)
        # A bot already standing in magma walks out instead of holding.
        self.assertIn("if (WorldPosition(bot).isInMagma())\n        return engaged;", f)
        # The slot is magma but the target's own spot is dry: follow the
        # target itself; otherwise stop and hold.
        self.assertIn("targetInMagma = SlotGoesIntoMagma(bot, WorldPosition(target), targetMagmaAt);", f)
        self.assertIn("switch (ai::PlanMagmaFollow(slotInMagma, targetInMagma, engaged))", f)
        self.assertIn("case ai::MagmaFollowAction::HoldOnTarget:", f)
        self.assertIn("mm.MoveFollow(target, 0.0f, 0.0f);", f)
        self.assertIn("case ai::MagmaFollowAction::HoldInPlace:", f)
        self.assertIn("ai->StopMoving();", f)
        self.assertIn("magmaFollowEngaged = targetGuid;", f)
        self.assertIn('LogMagmaMoveZ(bot, AI_VALUE(LastMovement&, "last movement"), "follow slot", slotX, slotY, slotMove);', f)
        # Round 6: a sinking slot is walked at the dry navmesh z by the module,
        # not held (the core follow cannot reach the floor above the lava).
        self.assertIn("if (slotInMagma && magmaFollowMemo.escape)", f)
        self.assertIn("MoveTo(escape.GetMapId(), escape.getX(), escape.getY(), escape.getZ())", f)
        self.assertIn("DryNavmeshPoint(bot, slotX, slotY, target->GetPositionZ(), escape)", f)
        self.assertIn("float const behind = target->GetOrientation() + 3.14159265f;", f)
        # Round 4 regression: the hold re-pointed the generator at distance 0,
        # and the FOLLOW_MOTION_TYPE early return would keep it there. The
        # clearing slot must send a fresh MoveFollow with the caller's slot.
        restore = at(f, "case ai::MagmaFollowAction::RestoreFollow:")
        hold = at(f, "case ai::MagmaFollowAction::HoldOnTarget:")
        self.assertLess(restore, hold)
        restore_body = f[restore:hold]
        self.assertIn("magmaFollowEngaged = ObjectGuid();", restore_body)
        self.assertIn("if (distance > 0.0f)", restore_body)
        self.assertIn("mm.MoveFollow(target, distance, angle);", restore_body)
        # Leaving the guard's map ends a hold too.
        gate = at(f, "if (!MagmaGuardOn(bot))")
        self.assertLess(gate, restore)
        self.assertIn("if (!engaged)\n            return false;", f[gate:restore])
        self.assertIn("mm.MoveFollow(target, distance, angle);", f[gate:restore])
        # Per-tick cost: a fresh memo answers before any terrain work.
        memo = at(f, "ai::MagmaRecheckDue(magmaFollowMemo.valid, slotX, slotY,")
        self.assertLess(memo, check)
        self.assertIn("if (!recheck && !magmaFollowMemo.held)\n        return false;", f[memo:check])
        self.assertIn("magmaFollowMemo.held = slotInMagma;", f[check:])
        # The route goes through the SAME sampled guard MoveTo uses, not a
        # second copy of the sampler.
        helper = body(MOVES, "static bool SlotGoesIntoMagma(", "\n}\n")
        route_gate = at(helper, "ai::MagmaRouteCheckAllowed(ai::MagmaMapLiquidListed(bot->GetMapId()),")
        self.assertIn("MagmaSeenOnMap(bot->GetMapId()));", helper)
        self.assertIn("if (!routeCheck)\n        return false;", helper[route_gate:])
        calc = at(helper, "path.calculate(slot.getX(), slot.getY(), slot.getZ(), false);")
        self.assertLess(route_gate, calc)
        self.assertIn("RouteEntersMagma(bot, WorldPosition(bot), route, magmaAt)", helper)
        self.assertIn("ai::MagmaGuardApplies(bot->GetMap()->IsDungeon())", helper)
        # The end-point probe that also learns the map runs before the gate.
        probe = at(helper, "ai::MagmaMoveProbe const move = ai::MagmaAtMoveZ(bot, slot.getX(), slot.getY(), slot.getZ());")
        self.assertLess(probe, route_gate)
        self.assertIn("NoteMagmaSeenOnMap(slot.GetMapId());", helper)
        self.assertIn("ai::CutPathBeforeMagma(start, route,", body(MOVES, "static bool RouteEntersLiquidHazard(", "\n}\n"))

    def test_move_z_probe_is_the_cores_clamp(self):
        # RFC 2026-11-14: the guard probed the unclamped z, where magma under
        # a thin floor reads ABOVE_WATER and dry, while the core's own
        # UpdateAllowedPositionZ (swim=true) answered liquid level - 2, in the
        # lava. The probe applies the core's clamp to a copy, then the core's
        # in-magma rule at that height.
        f = body(WORLDPOS, "MagmaMoveProbe ai::MagmaAtMoveZ(", "\n}\n")
        clamp = at(f, "mover->UpdateAllowedPositionZ(x, y, probe.moveZ);")
        magma = at(f, "probe.moveInMagma = WorldPosition(mover->GetMapId(), x, y, probe.moveZ).isInMagma(terrain);")
        self.assertLess(clamp, magma)
        self.assertIn("probe.moveZ = z;", f[:clamp])
        self.assertIn("z - probe.moveZ > ai::kMagmaDryFloorAboveYd", f)
        # A guard probes the slot through it, not with its own isInMagma.
        helper = body(MOVES, "static bool SlotGoesIntoMagma(", "\n}\n")
        self.assertIn("ai::MagmaMoveProbe const move = ai::MagmaAtMoveZ(bot, slot.getX(), slot.getY(), slot.getZ());", helper)
        self.assertNotIn("slot.isInMagma(", helper)

    def test_sinking_follow_slot_walks_a_navmesh_z_instead_of_holding(self):
        # Round 6: the slot's core move point is 2 yd under the dry navmesh
        # floor. Holding would strand the follower; the module walks the dry
        # navmesh point (the slot, else behind the leader) and the native
        # follow resumes once the slot clears (RestoreFollow above).
        f = body(MOVES, "bool MovementAction::HoldFollowOutOfMagma(", "\n}\n")
        escape = at(f, "if (slotInMagma && magmaFollowMemo.escape)")
        plan = at(f, "switch (ai::PlanMagmaFollow(slotInMagma, targetInMagma, engaged))")
        self.assertLess(escape, plan)
        block = f[escape:plan]
        self.assertIn("MoveTo(escape.GetMapId(), escape.getX(), escape.getY(), escape.getZ())", block)
        self.assertIn("return true;", block)
        self.assertIn("magmaFollowEngaged = targetGuid;", block)
        self.assertIn("DryNavmeshPoint(bot, slotX, slotY, target->GetPositionZ(), escape)", f)
        helper = body(MOVES, "static bool DryNavmeshPoint(", "\n}\n")
        self.assertIn("Vector3 const& end = points.back();", helper)
        self.assertIn("out = WorldPosition(bot->GetMapId(), end.x, end.y, end.z);", helper)
        self.assertIn("ai::MagmaMoveZPlan::Refuse", helper)

    def test_dispatch_refuses_a_sinking_waypoint(self):
        # Round 6: a point the spline would send into lava is raised to the dry
        # navmesh z when that is more than 1 yd above, and the whole move is
        # refused otherwise. Round 7 (Codex r6): the backing point generator
        # used to be created before this loop, so a refusal only cleared a
        # movement that had already launched and the generator kept the
        # unvalidated endpoint. No generator exists until every waypoint has
        # passed the guard, and the one created takes the checked end z.
        f = body(MOVES, "bool MovementAction::DispatchMovement(", "\n}\n")
        sink = at(f, "bool const magmaMoveSinks = magmaFloorClampedMagma && !stairRestore && !magmaFloorGuardFire;")
        plan = at(f, "ai::MagmaMoveZPlan const plan = ai::PlanMagmaMoveZ(true, move.moveInMagma,")
        keep = at(f[plan:], "p.z = navmeshZ;") + plan
        refuse = at(f, "RefuseMagmaMove(bot, dispatchMove, magmaAt, magmaAt, nullptr, \"clamped waypoint\");")
        generator = at(f, "mm.MovePoint(path.back().GetMapId(), pointPath.back().x, pointPath.back().y, "
                          "pointPath.back().z,")
        launch = at(f, "init.Launch();")
        self.assertLess(sink, plan)
        self.assertLess(plan, keep)
        self.assertLess(keep, refuse)
        self.assertLess(refuse, generator)
        self.assertLess(generator, launch)
        # Nothing launches before the verdict: the leg from the hazard rewrite
        # to the sinking check carries no point generator at all.
        self.assertNotIn("mm.MovePoint(",
            f[at(f, "bool const hazardRewrote = GeneratePathAvoidingHazards(path);"):sink])
        # A refusal is the whole move's answer: the MagmaRefuse row, then a
        # return, with no movement call behind it.
        self.assertNotIn("mm.", f[refuse:at(f[refuse:], "return false;") + refuse])
        # Every decision row names the site and both heights, at level 1.
        log = body(MOVES, "static void LogMagmaMoveZ(", "\n}\n")
        self.assertIn("TB_LOG_BASIC(", log)
        self.assertIn("magma %s at %.1f %.1f %.1f: move z %.1f, navmesh z %.1f", log)
        self.assertLess(at(log, "lastMove.magmaLogAt = now;"), at(log, "TB_LOG_BASIC("))
        self.assertIn('"clamped waypoint"', f[sink:launch])

    def test_every_dispatch_move_waits_for_its_magma_verdict(self):
        # Round 8 (Codex r7): the single-point legs launched before the
        # waypoint guard, so a clipped route whose point clamps into lava
        # still moved. One shared verdict now runs on the ACTUAL dispatch
        # point before every move in DispatchMovement.
        f = body(MOVES, "bool MovementAction::DispatchMovement(", "\n}\n")
        verdict = at(f, "if (!DispatchPointMagmaChecked(bot, dispatchMove, checkedX, checkedY, checkedZ, "
                        "\"dispatch point\"))")
        plain = at(f, "mm.MovePoint(movePosition.GetMapId(),")
        checked = at(f, "if (!WalkChecked(movePosition, moveMode == FORCED_MOVEMENT_WALK))")
        endpoint = at(f, "mm.MovePoint(path.back().GetMapId(), pointPath.back().x")
        launch = at(f, "init.Launch();")
        self.assertLess(verdict, plain)
        self.assertLess(verdict, checked)
        self.assertLess(endpoint, launch)
        # The multi-point generator sits behind its per-waypoint verdict: no
        # point generator between the hazard rewrite and the sinking check.
        sink = at(f, "bool const magmaMoveSinks")
        self.assertLess(sink, endpoint)
        self.assertNotIn("mm.MovePoint(",
            f[at(f, "bool const hazardRewrote = GeneratePathAvoidingHazards(path);"):sink])
        # A refused single point is the branch's whole answer: the row (named
        # for the site) and a return, with nothing launched behind it.
        refused = f[verdict:at(f[verdict:], "return false;") + verdict]
        self.assertNotIn("mm.", refused)
        self.assertIn('"dispatch point"', f[verdict:plain])

    def test_land_walk_checks_its_reached_point(self):
        # Round 8 (Codex r7): WalkChecked dispatches through LaunchLandWalk,
        # whose MovePoint used the navmesh-reached endpoint without a magma
        # check, so a checked single point could still walk into lava.
        f = body(MOVES, "bool MovementAction::LaunchLandWalk(", "\n}\n")
        verdict = at(f, "if (!DispatchPointMagmaChecked(bot, lastMove, reachedX, reachedY, reachedZ, \"land walk\"))")
        move = at(f, "bot->GetMotionMaster()->MovePoint(endPos.GetMapId(), reachedX, reachedY, reachedZ,")
        launch = at(f, "init.Launch();")
        self.assertLess(verdict, move)
        self.assertLess(move, launch)

    def test_chase_holds_out_of_magma(self):
        f = body(MOVES, "bool MovementAction::ChaseTo(", "\n}\n")
        # The angle MoveChase is handed is final before the guard plans the
        # goal (the behind strategy replaces it), so the guard checks the slot
        # the core chase walks to, not a radial point on the bot's side.
        behind = at(f, 'if (ai->HasStrategy("behind", BotState::BOT_STATE_COMBAT))')
        clamp = at(f, "if (angle > 20)")
        hold = at(f, "if (HoldChaseOutOfMagma(obj, distance, angle))")
        far = at(f, "if (distanceToTarget > sPlayerbotAIConfig.sightDistance")
        # After the far/out-of-sight hand-off (a MoveTo to the target itself,
        # guarded by MoveTo), before the point move and core chase.
        self.assertLess(behind, hold)
        self.assertLess(clamp, hold)
        self.assertLess(far, hold)
        # One angle clamp, before the guard: no later replacement between the
        # guarded goal and the MoveChase dispatch.
        self.assertEqual(f.count("angle > 20"), 1)
        self.assertEqual(f.count("angle = 0"), 1)
        g = body(MOVES, "bool MovementAction::HoldChaseOutOfMagma(", "\n}\n")
        goal = at(g, "ai::ChaseGoalPoint(target->GetPositionX(), target->GetPositionY(),")
        memo = at(g, "ai::MagmaRecheckDue(magmaChaseMemo.valid, goalPoint.x, goalPoint.y,")
        check = at(g, "held = SlotGoesIntoMagma(bot, goal, magmaAt, &goalMove);")
        self.assertLess(goal, memo)
        self.assertLess(memo, check)
        # The memo verdict answers a goal inside the re-check window before any
        # terrain query or route calculation.
        self.assertIn("if (!recheck && !magmaChaseMemo.held)\n        return false;", g[memo:check])
        self.assertIn("magmaChaseMemo.held = held;", g[check:])
        self.assertIn("target->GetOrientation(), slotDistance, angle);", g)
        self.assertIn("distance + target->GetObjectBoundingRadius() + bot->GetObjectBoundingRadius();", g)
        # A zero-offset (melee) chase has no angle: the ring guard decides.
        self.assertIn("if (!(distance > 0.0f))\n        return HoldMeleeChaseOutOfMagma(target);", g)
        self.assertNotIn("CreatureAuraChaseGoal", g)
        self.assertIn("ai->StopMoving();", g)
        # Lava kills a tank too: no tank pass on this hold.
        self.assertNotIn("IsTank", g)
        self.assertIn('LogMagmaRefusal(bot, AI_VALUE(LastMovement&, "last movement"), magmaAt, "chase goal");', g)

    def test_melee_chase_ring(self):
        # Round 3 (Codex HIGH r2): a zero-offset chase has no angle; the core
        # paths to a random attack point on a ring around the target, so the
        # target's own spot says nothing. Dry ring keeps the core's pick, a
        # mixed ring chases the dry bearing nearest the bot with an explicit
        # angle the core honours for a nonzero offset, all-magma holds.
        g = body(MOVES, "bool MovementAction::HoldMeleeChaseOutOfMagma(", "\n}\n")
        # Already in melee reach with line of sight: the core does not move.
        self.assertLess(at(g, "bot->CanReachWithMeleeAutoAttack(chased)"),
                        at(g, "ai::kMeleeRingSamples"))
        self.assertIn("float const radius = target->GetObjectBoundingRadius() + bot->GetMeleeReach();", g)
        # The random point can be as close as the bounding radii: a target
        # standing in lava holds before any ring sample decides.
        self.assertIn("if (targetPos.isInMagma())", g)
        self.assertLess(at(g, "if (targetPos.isInMagma())"), at(g, "for (int i = 0; i < ai::kMeleeRingSamples; ++i)"))
        self.assertIn('LogMagmaRefusal(bot, AI_VALUE(LastMovement&, "last movement"), targetPos, "melee chase ring");', g)
        # Round 4 per-tick cost: the target lookup is the whole guard on a map
        # whose liquid has never answered magma; the eight ring samples and
        # their height queries run only where magma is known.
        gate = at(g, "if (!ai::MagmaRouteCheckAllowed(ai::MagmaMapLiquidListed(bot->GetMapId()), MagmaSeenOnMap(bot->GetMapId())))")
        loop = at(g, "for (int i = 0; i < ai::kMeleeRingSamples; ++i)")
        self.assertLess(at(g, "if (targetPos.isInMagma())"), gate)
        self.assertLess(gate, loop)
        self.assertIn("return false;", g[gate:loop])
        self.assertIn("ai::MeleeRingPoint(target->GetPositionX(), target->GetPositionY(), radius, i)", g[loop:])
        plan = at(g, "ai::PlanMeleeRing(inMagma, target->GetAngle(bot))")
        self.assertLess(loop, plan)
        # The ring samples are reused inside the re-check window instead of
        # being taken again every tick.
        self.assertIn("ai::MagmaRecheckDue(ringUnchanged, target->GetPositionX(), target->GetPositionY(),", g)
        self.assertLess(at(g, "ai::MagmaRecheckDue(ringUnchanged,"), plan)
        keep = at(g, "if (plan.step == ai::MeleeRingStep::KeepRandom)")
        self.assertRegex(g[keep:keep + 200], r"return false;")
        hold = at(g, "if (plan.step == ai::MeleeRingStep::Hold)")
        self.assertLess(plan, hold)
        self.assertGreater(at(g[hold:], "ai->StopMoving();") + hold, hold)
        self.assertIn('LogMagmaRefusal(bot, lastMove, magmaAt, "melee chase ring");', g[hold:])
        # Mixed: offset such that the core's target-radius + offset + the bot's
        # radius lands on the sampled ring, and the sampled bearing relative
        # to the target's facing (the core adds the facing back).
        self.assertIn(
            "float const offset = radius - target->GetObjectBoundingRadius() - bot->GetObjectBoundingRadius();", g)
        self.assertIn("float const chaseAngle = plan.angle - target->GetOrientation();", g)
        self.assertIn("mm.MoveChase(chased, offset, chaseAngle);", g)
        # A chase already running on that bearing is left alone: re-dispatching
        # every tick would reset the generator.
        self.assertIn("magmaRingChaseTarget == target->getObjectGuid()", g)
        self.assertIn("magmaRingChaseAngle - chaseAngle", g)
        # An offset the generator would round to zero falls back to a walk.
        fallback = at(g, "if (!(offset > 0.0f))")
        self.assertLess(fallback, at(g, "mm.MoveChase(chased, offset, chaseAngle);"))
        self.assertIn("return MoveTo(dryPoint.GetMapId(), dryPoint.getX(), dryPoint.getY(), dryPoint.getZ());", g)

    def test_terrain_queries_wait_for_the_memo(self):
        # Round 5: the per-tick cost was still open. The memo answers first on
        # every map the guard runs on; the bot's own liquid, the goal height,
        # the target's liquid and the ring samples run only on a held memo hit
        # or a due re-check, and a map where magma cannot be costs nothing.
        for start in ("bool MovementAction::HoldFollowOutOfMagma(", "bool MovementAction::HoldChaseOutOfMagma("):
            f = body(MOVES, start, "\n}\n")
            gate = at(f, "if (!MagmaGuardOn(bot))")
            memo = at(f, "ai::MagmaRecheckDue(")
            self.assertLess(gate, memo)
            self.assertLess(memo, at(f, "if (WorldPosition(bot).isInMagma())"))

        helper = body(MOVES, "static bool MagmaGuardOn(Player* bot)", "\n}\n")
        self.assertIn("ai::MagmaGuardApplies(bot->GetMap()->IsDungeon())", helper)
        self.assertIn("ai::MagmaRouteCheckAllowed(ai::MagmaMapLiquidListed(bot->GetMapId()), "
                      "MagmaSeenOnMap(bot->GetMapId()))", helper)

        g = body(MOVES, "bool MovementAction::HoldChaseOutOfMagma(", "\n}\n")
        memo = at(g, "ai::MagmaRecheckDue(magmaChaseMemo.valid,")
        self.assertLess(memo, at(g, "float const ground = goal.GetHeight();"))

        m = body(MOVES, "bool MovementAction::HoldMeleeChaseOutOfMagma(", "\n}\n")
        memo = at(m, "ai::MagmaRecheckDue(ringUnchanged,")
        self.assertLess(memo, at(m, "bot->CanReachWithMeleeAutoAttack(chased)"))
        self.assertLess(memo, at(m, "if (targetPos.isInMagma())"))
        self.assertLess(memo, at(m, "for (int i = 0; i < ai::kMeleeRingSamples; ++i)"))

    def test_healer_stand_path_is_calculated(self):
        # The healer stand's PathFinder was read without calculate(), so its end
        # was (0, 0, 0) and every stand was rejected (found 2026-11-13).
        f = body(COMMANDS, "bool HealerPullStand(Player* lead, Unit* target, Player* healer, float& ox, float& oy, float& oz)",
                 "\n}\n")
        calc = at(f, "path.calculate(px, py, ground, false);")
        kind = at(f, "path.getPathType()")
        end = at(f, "path.getActualEndPosition()")
        self.assertLess(calc, kind)
        self.assertLess(calc, end)
        self.assertEqual(f.count("path.calculate("), 1)


if __name__ == "__main__":
    unittest.main()
