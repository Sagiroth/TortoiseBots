"""Wiring pins for the navmesh-island unstick (AiPlayerbot.DungeonRunPlaceUnstick, ZG walk 55 FROZEN-REPRO).

The rules are pure and tested in tools/test_place_unstick_policy.cpp (the NOPATH clock, the 10 s streak, the 20 yd
master gate, battlegrounds, the 30 s rate limit, the fight exception, flag off). These pins only prove the live code
feeds that clock from the one failure that matters (MoveTo2's dispatch-short with a NOPATH route) and moves the bot
with its own combat-keeping teleport to a spot the summon command's helper picks.
Run: python3 -m unittest tools/test_place_unstick_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MOVEMENT = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
MOVEMENT_H = (ROOT / "ai/playerbot/strategy/actions/MovementActions.h").read_text()
SUMMON_H = (ROOT / "ai/playerbot/strategy/actions/UseMeetingStoneAction.h").read_text()
LAST_MOVE = (ROOT / "ai/playerbot/strategy/values/LastMovementValue.h").read_text()
POLICY = (ROOT / "ai/playerbot/PlaceUnstickPolicy.h").read_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


class PlaceUnstickWiring(unittest.TestCase):
    def test_nopath_dispatch_short_feeds_the_clock(self):
        # MoveTo2's DispatchMovement failure (the site that stamps dispatch-short) calls the unstick, after the
        # route type of this try (movePathType) was stamped, and before it returns false.
        move = body(MOVEMENT, "bool MovementAction::MoveTo2(const WorldPosition& endPos")
        stamp = move.index("lastMove.movePathType = WorldPosition::LastPathStepType();")
        fail = move.index("if (!DispatchMovement(movePath, generatePath, masterWalking))")
        branch = move[fail:move.index("return false;", fail)]
        self.assertLess(stamp, fail)
        self.assertIn("TryPlaceUnstick(lastMove);", branch)
        # The clock is fed with this try's own fail reason and path type, and only that pair counts.
        unstick = body(MOVEMENT, "bool MovementAction::TryPlaceUnstick(LastMovement& lastMove)")
        self.assertIn(
            "ai::PlaceUnstickOnMoveResult(state, nowMs, false, lastMove.moveFailReason, lastMove.movePathType, at);",
            unstick)
        # The clock is fed only inside an instance, with the map, instance and spot of the bot.
        spot = body(MOVEMENT, "static ai::PlaceUnstickSpot PlaceUnstickSpotOf(Player* bot)")
        self.assertIn("at.inInstance = map && map->IsDungeon();", spot)
        self.assertIn("at.instanceId = bot->GetInstanceId();", spot)
        self.assertIn("ai::PlaceUnstickSpot const at = PlaceUnstickSpotOf(bot);", unstick)
        self.assertIn("if (!ai::PlaceUnstickCountsFail(lastMove.moveFailReason, lastMove.movePathType))", unstick)
        # DispatchMovement's short-path refusal is the dispatch-short the policy counts.
        dispatch = body(MOVEMENT, "bool MovementAction::DispatchMovement(")
        self.assertIn('AI_VALUE(LastMovement&, "last movement").moveFailReason = MOVE_FAIL_DISPATCH_SHORT;', dispatch)
        self.assertIn("static_assert(ai::kPlaceUnstickFailDispatchShort == MOVE_FAIL_DISPATCH_SHORT", MOVEMENT)
        self.assertIn("static_assert(ai::kPlaceUnstickPathNoPath == ai::TRAVEL_PATHFIND_NOPATH", MOVEMENT)

    def test_a_launched_move_resets_the_clock(self):
        move = body(MOVEMENT, "bool MovementAction::MoveTo2(const WorldPosition& endPos")
        fail = move.index("if (!DispatchMovement(movePath, generatePath, masterWalking))")
        tail = move[fail:]
        self.assertIn(
            "ai::PlaceUnstickOnMoveResult(lastMove.placeUnstick, WorldTimer::getMSTime(), true, MOVE_FAIL_NONE, 0,",
            tail)

    def test_flag_off_is_todays_behaviour(self):
        unstick = body(MOVEMENT, "bool MovementAction::TryPlaceUnstick(LastMovement& lastMove)")
        first = unstick[unstick.index("{"):]
        self.assertTrue(first.lstrip("{ \n").startswith("if (!sPlayerbotAIConfig.dungeonRunPlaceUnstick)\n        return false;"))
        move = body(MOVEMENT, "bool MovementAction::MoveTo2(const WorldPosition& endPos")
        self.assertIn("if (sPlayerbotAIConfig.dungeonRunPlaceUnstick)\n        ai::PlaceUnstickOnMoveResult(", move)

    def test_the_decision_is_the_policy(self):
        unstick = body(MOVEMENT, "bool MovementAction::TryPlaceUnstick(LastMovement& lastMove)")
        self.assertIn("if (ai::PlaceUnstickDecide(in) != ai::PlaceUnstickVerdict::Teleport)", unstick)
        self.assertIn("in.inInstance = map->IsDungeon();", unstick)
        self.assertIn("in.inBattleground = bot->InBattleGround() || map->IsBattleGround();", unstick)
        self.assertIn("in.botCharmed = !bot->GetCharmerGuid().IsEmpty();", unstick)
        self.assertIn("in.botCharming = !bot->GetCharmGuid().IsEmpty();", unstick)
        self.assertIn("in.botCasting = bot->IsNonMeleeSpellCasted(true);", unstick)
        self.assertIn("ai::PlaceUnstickIsFight(hasMaster && master->IsInCombat(), liveTarget,", unstick)
        # The misuse cap: the summon command's sight bound, or a commander-bound group.
        self.assertIn("in.masterInSight = hasMaster && in.masterYd <= sPlayerbotAIConfig.sightDistance;", unstick)
        self.assertIn("in.runCommanded = TortoiseBots::BotCommands::IsInCommandedGroup(bot);", unstick)

    def test_the_isolation_query_runs_before_the_teleport(self):
        unstick = body(MOVEMENT, "bool MovementAction::TryPlaceUnstick(LastMovement& lastMove)")
        cheap = unstick.index("in.startIsolated = true;")
        probe = unstick.index("in.startIsolated = ProbePlaceUnstickStart(bot, master);")
        teleport = unstick.index("bot->TeleportTo(")
        self.assertLess(cheap, probe)
        self.assertLess(probe, teleport)
        # A connected start restarts the streak: the query runs at most once per 10 s streak.
        self.assertIn("ai::PlaceUnstickRestartStreak(state, nowMs);", unstick[probe:teleport])
        query = body(MOVEMENT, "static bool ProbePlaceUnstickStart(Player* bot, Player* master)")
        self.assertIn("PathInfo::FindWalkPoly(query, start, filter, startNearest)", query)
        self.assertIn("PathInfo::FindWalkPoly(query, end, filter, endNearest)", query)
        self.assertIn("query->findPath(startRef, endRef", query)
        self.assertIn("bool const reachedEnd = polys[polyCount - 1] == endRef;", query)
        self.assertIn("return ai::PlaceUnstickStartIsolated(true, true, true, reachedEnd, reachYd);", query)
        self.assertIn("filter.setIncludeFlags(NAV_GROUND | NAV_WATER);", query)

    def test_own_teleport_keeps_combat_and_pet(self):
        unstick = body(MOVEMENT, "bool MovementAction::TryPlaceUnstick(LastMovement& lastMove)")
        self.assertIn("SummonAction::FindSpotBeside(master, bot, ai->GetRange(\"follow\"), GetFollowAngle(), x, y, z)", unstick)
        self.assertIn("bot->TeleportTo(bot->GetMapId(), x, y, z, bot->GetOrientation(),\n"
                      "        TELE_TO_NOT_LEAVE_COMBAT | TELE_TO_NOT_UNSUMMON_PET);", unstick)
        # The summon helper (its whispers, stay/guard rewrite, resurrect) is not called any more.
        self.assertNotIn("TeleportBotToMaster", MOVEMENT + SUMMON_H)
        self.assertNotIn("summon->", unstick)
        self.assertNotIn("Teleport(master", unstick)
        self.assertNotIn("TellPlayer", unstick)
        self.assertNotIn("TellDebug", unstick)
        self.assertNotIn('GetAction("summon")', unstick)
        # The pet goes with the bot.
        self.assertIn("ai::PlaceUnstickMovesPet(", unstick)
        self.assertIn("pet->NearTeleportTo(x, y, z, pet->GetOrientation());", unstick)
        # Stay and guard are cleared only when they point at the island.
        self.assertIn('for (char const* anchor : {"stay", "guard"})', unstick)
        self.assertIn("ai::PlaceUnstickClearsAnchor(pos.isSet(), pos.mapId == bot->GetMapId(),", unstick)
        self.assertIn('RESET_AI_VALUE2(PositionEntry, "pos", anchor);', unstick)
        self.assertIsNone(re.search(r"(?<!RE)SET_AI_VALUE2\(PositionEntry", unstick))
        # The cooldown is stamped before the teleport, so a refused one is rate limited too.
        self.assertLess(unstick.index("ai::PlaceUnstickStamp(state, nowMs);"), unstick.index("bot->TeleportTo("))
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BotUnstick"', unstick)
        self.assertIn('"reason=nopath_island secs="', unstick)
        self.assertIn('" master_yd="', unstick)
        self.assertIn("static bool FindSpotBeside(Player* summoner, Player* player, float followRange, float followAngle,", SUMMON_H)
        self.assertIn("bool TryPlaceUnstick(LastMovement& lastMove);", MOVEMENT_H)
        # The summon itself still picks its spot with the shared helper.
        summon = (ROOT / "ai/playerbot/strategy/actions/UseMeetingStoneAction.cpp").read_text()
        teleport = body(summon, "bool SummonAction::Teleport(Player* requester, Player *summoner, Player *player)")
        self.assertIn('FindSpotBeside(summoner, player, ai->GetRange("follow"), GetFollowAngle(), x, y, z)', teleport)

    def test_state_survives_a_move_clear(self):
        self.assertIn("PlaceUnstickState placeUnstick;", LAST_MOVE)
        self.assertIn("placeUnstick = other.placeUnstick;", LAST_MOVE)
        clear = LAST_MOVE[LAST_MOVE.index("void clear()"):LAST_MOVE.index("void Set(Unit* lastFollow)")]
        self.assertNotIn("placeUnstick", clear)

    def test_policy_constants(self):
        self.assertIn("constexpr uint32_t kPlaceUnstickStreakMs = 10000;", POLICY)
        self.assertIn("constexpr uint32_t kPlaceUnstickCooldownMs = 30000;", POLICY)
        self.assertIn("constexpr float kPlaceUnstickMasterMinYd = 20.0f;", POLICY)
        self.assertIn("constexpr float kPlaceUnstickFightYd = 5.0f;", POLICY)
        self.assertIn("constexpr float kPlaceUnstickMoveYd = 3.0f;", POLICY)
        self.assertIn("constexpr float kPlaceUnstickReachYd = 2.0f;", POLICY)
        self.assertIn("constexpr float kPlaceUnstickAnchorYd = 5.0f;", POLICY)
        self.assertIn("constexpr float kPlaceUnstickPetYd = 30.0f;", POLICY)

    def test_flag_and_docs(self):
        config_h = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
        config = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
        template = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
        settings = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
        play = (ROOT / "docs/play/dungeons-and-raids.md").read_text()
        self.assertIn("bool dungeonRunPlaceUnstick = false;", config_h)
        self.assertIn('GetBoolDefault("AiPlayerbot.DungeonRunPlaceUnstick", false)', config)
        self.assertIn("AiPlayerbot.DungeonRunPlaceUnstick = 0", template)
        self.assertIn("### `AiPlayerbot.DungeonRunPlaceUnstick`", settings)
        self.assertIn("DungeonRunPlaceUnstick", play)


if __name__ == "__main__":
    unittest.main()
