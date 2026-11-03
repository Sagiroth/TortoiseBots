"""Wiring pins for the crowd-control area guards (ZG walk 32).

The pure rules live in ai/playerbot/strategy/values/CcMarkHoldPolicy.h
(CcDotTargetAllowed, CcCancelSwingSplashBuff; behaviour in
tools/test_cc_mark_hold_policy.cpp) and the game-state reads in
ai/playerbot/strategy/values/TargetValue.cpp (CcGuard::IsCcMarkedUnit and
CcGuard::IsCrowdControlledUnit). These pins only prove the live code calls them:

 - Sweeping Strikes is not started beside a protected enemy, and a running one is
   dropped ("remove sweeping strikes" on the "sweeping strikes near cc" trigger),
   the warrior's copy of the Blade Flurry rule;
 - the DoT rows ("attacker without aura") skip a CC mark or a crowd-controlled
   unit while AiPlayerbot.DungeonRunAutoCc is on, and keep the held-mark skip
   without it.

Run: python3 -m unittest tools/test_cc_area_guards_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRATEGY = ROOT / "ai/playerbot/strategy"
VALUES = STRATEGY / "values"
WARRIOR_ACTIONS = (STRATEGY / "warrior/WarriorActions.h").read_text()
WARRIOR_TRIGGERS = (STRATEGY / "warrior/WarriorTriggers.cpp").read_text()
WARRIOR_TRIGGERS_H = (STRATEGY / "warrior/WarriorTriggers.h").read_text()
WARRIOR_CONTEXT = (STRATEGY / "warrior/WarriorAiObjectContext.cpp").read_text()
ARMS = (STRATEGY / "warrior/ArmsWarriorStrategy.cpp").read_text()
TARGET_VALUE = (VALUES / "TargetValue.cpp").read_text()
TARGET_VALUE_H = (VALUES / "TargetValue.h").read_text()
DOT_TARGET = (VALUES / "AttackerWithoutAuraTargetValue.cpp").read_text()
POLICY = (VALUES / "CcMarkHoldPolicy.h").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class SweepingStrikesWiring(unittest.TestCase):
    """The warrior's swing copy gets the Blade Flurry treatment."""

    def test_start_and_cancel_share_the_rule(self):
        near = body(WARRIOR_ACTIONS, "inline bool SweepingStrikesNearCc(")
        self.assertIn("ProtectedEnemyNear(bot, radius, true, bot->GetVictim())", near)
        self.assertIn("ReadyCcMarkNear(bot, radius)", near)
        self.assertIn("CcCancelSwingSplashBuff(true, protectedNear", near)
        start = body(WARRIOR_ACTIONS, "class CastSweepingStrikesAction")
        self.assertIn("CastBuffSpellAction::isUseful()", start)
        self.assertIn("SweepingStrikesNearCc(ai, bot, CcSweepingStrikesRadius)", start)
        self.assertIn("constexpr float CcSweepingStrikesRadius = 8.0f;", POLICY)
        self.assertIn("inline bool CcCancelSwingSplashBuff(bool buffUp, bool protectedEnemyInReach, bool readyCcMarkInReach)",
                      POLICY)

    def test_the_running_buff_is_dropped(self):
        trigger = body(WARRIOR_TRIGGERS, "bool SweepingStrikesNearCcTrigger::IsActive(")
        # Aura first: only a warrior with the buff up pays for the scan.
        self.assertIn('ai->HasAura("sweeping strikes", bot)', trigger)
        self.assertIn("SweepingStrikesNearCc(ai, bot, CcSweepingStrikesRadius)", trigger)
        self.assertIn("class SweepingStrikesNearCcTrigger", WARRIOR_TRIGGERS_H)
        self.assertIn('creators["sweeping strikes near cc"]', WARRIOR_CONTEXT)
        self.assertIn('creators["remove sweeping strikes"]', WARRIOR_CONTEXT)
        self.assertIn('"remove sweeping strikes"', ARMS)
        self.assertIn("ACTION_INTERRUPT + 5", ARMS)


class DotTargetWiring(unittest.TestCase):
    """No DoT on a CC mark or a crowd-controlled unit (auto crowd control on)."""

    def test_the_dot_rows_call_the_policy(self):
        fn = body(DOT_TARGET, "Unit* AttackerWithoutAuraTargetValue::Calculate()")
        self.assertIn("sPlayerbotAIConfig.dungeonRunAutoCc", fn)
        self.assertIn("ccGuard.IsCcMarkedUnit(unit)", fn)
        self.assertIn("ccGuard.IsCrowdControlledUnit(unit)", fn)
        self.assertIn(
            "CcDotTargetAllowed(sPlayerbotAIConfig.dungeonRunAutoCc, ccGuard.IsHeldCcMark(unit), ccMarkOrControlled)",
            fn)
        # The two reads behind the policy are the guard's own mark and CC tests.
        marked = body(TARGET_VALUE, "bool CcGuard::IsCcMarkedUnit(")
        self.assertIn("IsCcMark(unit)", marked)
        controlled = body(TARGET_VALUE, "bool CcGuard::IsCrowdControlledUnit(")
        self.assertIn("IsCrowdControlled(ai, unit)", controlled)
        self.assertIn("bool CcDotTargetAllowed(bool runAutoCc, bool heldMark, bool ccMarkOrControlled)", POLICY)


if __name__ == "__main__":
    unittest.main()
