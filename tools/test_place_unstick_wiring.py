"""Wiring pins for the navmesh-island unstick (AiPlayerbot.DungeonRunPlaceUnstick, ZG walk 55 FROZEN-REPRO).

The rules are pure and tested in tools/test_place_unstick_policy.cpp (the NOPATH clock, the 10 s streak, the 20 yd
master gate, battlegrounds, the 30 s rate limit, the fight exception, flag off). These pins only prove the live code
feeds that clock from the one failure that matters (MoveTo2's dispatch-short with a NOPATH route) and moves the bot
with the summon command's teleport.
Run: python3 -m unittest tools/test_place_unstick_wiring.py
"""
import pathlib
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
            "ai::PlaceUnstickOnMoveResult(state, nowMs, false, lastMove.moveFailReason, lastMove.movePathType);",
            unstick)
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
            "ai::PlaceUnstickOnMoveResult(lastMove.placeUnstick, WorldTimer::getMSTime(), true, MOVE_FAIL_NONE, 0);",
            tail)

    def test_flag_off_is_todays_behaviour(self):
        unstick = body(MOVEMENT, "bool MovementAction::TryPlaceUnstick(LastMovement& lastMove)")
        first = unstick[unstick.index("{"):]
        self.assertTrue(first.lstrip("{ \n").startswith("if (!sPlayerbotAIConfig.dungeonRunPlaceUnstick)\n        return false;"))
        move = body(MOVEMENT, "bool MovementAction::MoveTo2(const WorldPosition& endPos")
        self.assertIn("if (sPlayerbotAIConfig.dungeonRunPlaceUnstick)\n        ai::PlaceUnstickOnMoveResult(", move)

    def test_the_decision_is_the_policy_and_the_teleport_is_the_summon(self):
        unstick = body(MOVEMENT, "bool MovementAction::TryPlaceUnstick(LastMovement& lastMove)")
        self.assertIn("if (ai::PlaceUnstickDecide(in) != ai::PlaceUnstickVerdict::Teleport)", unstick)
        self.assertIn("in.inInstance = map->IsDungeon();", unstick)
        self.assertIn("in.inBattleground = bot->InBattleGround() || map->IsBattleGround();", unstick)
        self.assertIn("in.botCharmed = !bot->GetCharmerGuid().IsEmpty();", unstick)
        self.assertIn("in.botCasting = bot->IsNonMeleeSpellCasted(true);", unstick)
        self.assertIn("ai::PlaceUnstickIsFight(hasMaster && master->IsInCombat(), liveTarget,", unstick)
        self.assertIn('dynamic_cast<SummonAction*>(context->GetAction("summon"))', unstick)
        self.assertIn("summon->TeleportBotToMaster(master)", unstick)
        # The cooldown is stamped before the teleport, so a refused one is rate limited too.
        self.assertLess(unstick.index("ai::PlaceUnstickStamp(state, nowMs);"), unstick.index("summon->TeleportBotToMaster(master)"))
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BotUnstick"', unstick)
        self.assertIn('"reason=nopath_island secs="', unstick)
        self.assertIn('" master_yd="', unstick)
        self.assertIn("bool TeleportBotToMaster(Player* master) { return Teleport(master, master, bot); }", SUMMON_H)
        self.assertIn("bool TryPlaceUnstick(LastMovement& lastMove);", MOVEMENT_H)

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
