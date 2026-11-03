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
        end = at(f, "if (endPos.isInMagma())")
        clip = at(f, "movePath.ClipPath(ai, mover, ignoreEnemyTargets, packHold);")
        route = at(f, "RouteEntersMagma(bot, startPos, movePath, magmaAt)")
        dispatch = at(f, "DispatchMovement(movePath, generatePath, masterWalking)")
        teleport = at(f, "bot->TeleportTo(teleportPosition.GetMapId(),")
        self.assertTrue(guard < memory < end < clip < route < teleport < dispatch)

    def test_each_refusal_returns_false_with_magma(self):
        f = self.move2
        memory = f[at(f, "ai::MagmaRefusalActive("):]
        self.assertRegex(memory, r"^[^}]*lastMove\.moveFailReason = MOVE_FAIL_MAGMA;\s*return false;")
        end = f[at(f, "if (endPos.isInMagma())"):]
        self.assertRegex(end, r"^[^}]*RefuseMagmaMove\(bot, lastMove, endPos, endPos, nullptr, \"end point\"\);\s*return false;")
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
        walk = at(f, "bot->GetMotionMaster()->MovePoint(0, task.stand.x, task.stand.y, task.stand.z,")
        self.assertLess(blocked, walk)
        self.assertEqual(f.count("MovePoint("), 1)  # the guarded walk is the only one
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
        magma = at(f, "if (MagmaGuardApplies(bot->GetMap()->IsDungeon()) && WorldPosition(loc).isInMagma())")
        step = at(f, "return FormationStationStep(memory, formation->GetSerial(),")
        self.assertLess(magma, step)

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


if __name__ == "__main__":
    unittest.main()
