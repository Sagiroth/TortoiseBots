"""Pin the airborne-nukes glue (package N1, Onyxia walk 2e).

The pure rules live in tools/test_wand_policy.cpp (AirborneWandYields)
and tools/test_ranged_approach_policy.cpp (HoverCastHoldsSpread); this pins
the call sites in ai/:
- the "onyxia airborne" row names "airborne shoot" and no "shoot";
- creators["airborne shoot"] exists next to "shoot";
- CastAirborneShootAction::isUseful calls AirborneWandYields under the key;
- RaidSpreadAction::isUseful asks HoverCastHolds' verdict under hoverCastHoldsSpread,
  after the FindBossStand block, logs "hover_cast" when the hold applies and
  "hover_cast_spread" when it yields to an ally in the spread radius;
- the verdict keeps the hold only with no ally inside RaidSpreadAction's own
  spread radius;
- CastSpellAction::isPossible notes all three refusals (no target, out of
  reach, CanCastSpell result);
- both keys are read in PlayerbotAIConfig.cpp with a false default.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRATEGY = (ROOT / "ai/playerbot/strategy/generic/OnyxiasLairDungeonStrategies.cpp").read_text()
ACTIONS_CTX = (ROOT / "ai/playerbot/strategy/actions/ActionContext.h").read_text()
SHOOT_H = (ROOT / "ai/playerbot/strategy/actions/GenericSpellActions.h").read_text()
SHOOT_CPP = (ROOT / "ai/playerbot/strategy/actions/GenericSpellActions.cpp").read_text()
DUNGEON_CPP = (ROOT / "ai/playerbot/strategy/actions/DungeonActions.cpp").read_text()
DUNGEON_H = (ROOT / "ai/playerbot/strategy/actions/DungeonActions.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class AirborneNukesWiringTest(unittest.TestCase):
    def test_airborne_row_names_airborne_shoot(self):
        row = body(STRATEGY, '"onyxia airborne"', "NULL)));")
        self.assertIn('"airborne shoot"', row)
        self.assertNotIn('"shoot"', row.replace('"airborne shoot"', ''))

    def test_airborne_shoot_creator_exists(self):
        self.assertIn('creators["airborne shoot"]', ACTIONS_CTX)
        self.assertIn('creators["shoot"]', ACTIONS_CTX)

    def test_airborne_shoot_useful_yields_under_key(self):
        self.assertIn("class CastAirborneShootAction : public CastShootAction", SHOOT_H)
        fn = body(SHOOT_CPP, "bool CastAirborneShootAction::isUseful()", "\n}\n")
        self.assertIn("AirborneWandYields(sPlayerbotAIConfig.airborneWandYieldsToNukes", fn)

    def test_spread_useful_holds_after_stand(self):
        fn = body(DUNGEON_CPP, "bool RaidSpreadAction::isUseful()", "\n}\n")
        self.assertIn("if (sPlayerbotAIConfig.hoverCastHoldsSpread)", fn)
        self.assertIn("HoverCastHold const hold = HoverCastVerdict();", fn)
        self.assertLess(fn.index("if (FindBossStand(ai, bot).valid)"),
                        fn.index("HoverCastVerdict()"))
        self.assertIn('"hover_cast"', fn)
        self.assertIn('"hover_cast_spread"', fn)
        # The rows describe the hold, so they no longer wait for a live boss spread row (Onyxia has none).
        hover = fn[fn.index("if (sPlayerbotAIConfig.hoverCastHoldsSpread)"):]
        self.assertNotIn("BossSpreadLive", hover)
        self.assertIn("if (hold == HoverCastHold::Held)\n            return false;", fn)
        self.assertIn("HoverCastHold HoverCastVerdict();", DUNGEON_H)

    def test_spread_hold_asks_hover_range_and_live_row(self):
        fn = body(DUNGEON_CPP, "RaidSpreadAction::HoverCastHold RaidSpreadAction::HoverCastVerdict()", "\n}\n")
        self.assertIn("HoverReachTakerIsRangedDps(", fn)
        self.assertIn("HoverReachApplies(true, bot->GetMap()->IsDungeon()", fn)
        self.assertIn('ai->GetRange("spell")', fn)
        self.assertIn("IsWithinLOSInMap(target, true)", fn)
        self.assertIn("BossSpreadLive(ai, bot)", fn)
        self.assertIn("NearestStackedAlly(nearestYd)", fn)
        self.assertIn("SpreadRadiusYd()", fn)
        self.assertIn("HoverCastHoldsSpread(true,", fn)
        self.assertIn("Player* NearestStackedAlly(float& distance);", DUNGEON_H)
        self.assertIn("float SpreadRadiusYd();", DUNGEON_H)

    def test_cast_fail_reasons_noted(self):
        fn = body(SHOOT_CPP, "bool CastSpellAction::isPossible()", "\n    return true;\n}")
        self.assertIn("ai->NoteCastCheckFail(SPELL_FAILED_BAD_TARGETS);", fn)
        self.assertIn("ai->NoteCastCheckFail(SPELL_FAILED_OUT_OF_RANGE);", fn)
        self.assertIn("ai->NoteCastCheckFail(result);", fn)

    def test_keys_are_read_with_false_default(self):
        self.assertIn('airborneWandYieldsToNukes = config.GetBoolDefault("AiPlayerbot.AirborneWandYieldsToNukes", false);',
                      CONFIG_CPP)
        self.assertIn('hoverCastHoldsSpread = config.GetBoolDefault("AiPlayerbot.HoverCastHoldsSpread", false);',
                      CONFIG_CPP)


if __name__ == "__main__":
    unittest.main()
