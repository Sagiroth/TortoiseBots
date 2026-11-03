"""The bot walk/run state: no spline of a bot with a walking view may launch
between reconciles.

Observed players keep the walk/run mode a spline launch leaves behind (the core
re-sends the pre-launch mode for player units after the change), so a run
spline launched while the bot's movement flags still say walk leaves every
watching client in walk mode until it reloads - what players reported as alt
bots that walk everywhere until a relog. Pins the module side:
- the only walk flag a bot movement writes is the RPG approach flag
  (RandombotsWalkingRPG, indoors); the master's walking is mimicked purely by
  the dispatch's walkDistance gate, no bot movement flag for it;
- leftover walk flags are cleared before the tick's moves are dispatched
  (PlayerbotAI::DoNextAction, before the engine call);
- DispatchMovement walks the dispatch for either source (master mimic gate or
  the bot's own walk flag), and never launches run on walk flags.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
AI = ROOT / "ai/playerbot"


class WalkModeWiringTest(unittest.TestCase):
    def load(self, *parts):
        return AI.joinpath(*parts).read_text()

    def test_only_rpg_action_sets_a_walk_flag(self):
        offenders = []
        for path in list(AI.glob("**/*.cpp")) + list(AI.glob("**/*.h")):
            text = path.read_text()
            if "AddMovementFlag(MOVEFLAG_WALK_MODE)" in text and path.name != "MoveToRpgTargetAction.cpp":
                offenders.append(path.name)
            if "SetWalk(true" in text and path.name not in ("MovementActions.cpp", "MoveToRpgTargetAction.cpp"):
                offenders.append(path.name)
        self.assertEqual(offenders, [])

    def test_walk_flags_cleared_before_the_engine_dispatch(self):
        body = self.load("PlayerbotAI.cpp")
        i = body.index("void PlayerbotAI::DoNextAction(bool min, bool forceActivity)")
        fn = body[i:body.index("void PlayerbotAI::ReInitCurrentEngine", i)]
        clear = fn.index("RemoveMovementFlag(MOVEFLAG_WALK_MODE)")
        self.assertLess(clear, fn.index("currentEngine->DoNextAction("))
        self.assertNotIn("AddMovementFlag(MOVEFLAG_WALK_MODE)", fn)

    def test_no_hardcoded_mimic_distance_is_left(self):
        self.assertNotIn("distance2d(bot, master) < 20.0f) bot->m_movementInfo", self.load("PlayerbotAI.cpp"))

    def test_dispatch_walks_for_either_walk_source(self):
        body = self.load("strategy/actions/MovementActions.cpp")
        i = body.index("bool MovementAction::DispatchMovement(TravelPath movePath, bool generatePath, bool masterWalking)\n{")
        fn = body[i:body.index("bool MovementAction::MoveTo(uint32 mapId", i)]
        self.assertIn("ai::WalkModePolicy::DispatchWalks(masterWalking, bot->m_movementInfo.HasMovementFlag(MOVEFLAG_WALK_MODE))", fn)

    def test_rpg_walk_flag_gates_on_the_config(self):
        body = self.load("strategy/actions/MoveToRpgTargetAction.cpp")
        self.assertIn("if (sPlayerbotAIConfig.RandombotsWalkingRPG)", body)
        self.assertIn("AddMovementFlag(MOVEFLAG_WALK_MODE)", body)


if __name__ == "__main__":
    unittest.main()
