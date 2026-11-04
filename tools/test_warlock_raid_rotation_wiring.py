"""The warlock raid rotation rows must consult the policy, not a copy of it.

`runtime/policy/class/WarlockRaidRotationPolicy.h` (key
`AiPlayerbot.WarlockRaidRotation`, off by default) owns the rotation's
decisions. The trigger rows have to call them: a hand-copied threshold in a
trigger would drift from the policy and from
`tools/test_warlock_raid_rotation_policy.cpp`.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PB = ROOT / "ai/playerbot"
CONFIG_H = (PB / "PlayerbotAIConfig.h").read_text()
CONFIG_CPP = (PB / "PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (PB / "aiplayerbot.conf.dist.in").read_text()
TRIGGERS = (PB / "strategy/warlock/WarlockTriggers.cpp").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class WarlockRaidRotationWiringTest(unittest.TestCase):
    def test_key_defaults_off_and_is_documented(self):
        self.assertIn("bool warlockRaidRotation;", CONFIG_H)
        self.assertIn(
            'warlockRaidRotation = config.GetBoolDefault("AiPlayerbot.WarlockRaidRotation", false);',
            CONFIG_CPP)
        self.assertIn("AiPlayerbot.WarlockRaidRotation = 0", TEMPLATE)
        self.assertIn("# 0 - current rotation (Default)", TEMPLATE)

    def test_current_target_dot_rows_use_the_life_gate(self):
        for start in ("bool CorruptionTrigger::IsActive()",
                      "bool SiphonLifeTrigger::IsActive()",
                      "bool ImmolateTrigger::IsActive()",
                      "bool NoCurseTrigger::IsActive()",
                      "bool CurseOfTheElementsTrigger::IsActive()",
                      "bool CurseOfRecklessnessRaidTrigger::IsActive()",
                      "bool CurseOfShadowRaidTrigger::IsActive()"):
            fn = body(TRIGGERS, start, "\n}\n")
            self.assertIn("WarlockDotTargetAllowed(", fn, start)

    def test_spread_rows_use_the_spread_gate(self):
        for start in ("bool CorruptionOnAttackerTrigger::IsActive()",
                      "bool SiphonLifeOnAttackerTrigger::IsActive()",
                      "bool ImmolateOnAttackerTrigger::IsActive()",
                      "bool CurseOfTheElementsOnAttackerTrigger::IsActive()"):
            fn = body(TRIGGERS, start, "\n}\n")
            self.assertIn("WarlockSpreadTargetAllowed(", fn, start)
        # The raid cap lives in the policy, not in a trigger loop copy.
        helper = body(TRIGGERS, "bool WarlockSpreadTargetAllowed(", "\n}\n")
        self.assertIn("TortoiseBots::WarlockSpreadAllowed(true, true, dotted)", helper)
        # The curse spread skips attackers below the spread line as well.
        fn = body(TRIGGERS, "bool NoCurseOnAttackerTrigger::IsActive()", "\n}\n")
        self.assertIn("WarlockDotTargetAllowed(attacker, true)", fn)

    def test_policy_header_is_the_single_threshold_source(self):
        policy = (ROOT / "runtime/policy/class/WarlockRaidRotationPolicy.h").read_text()
        for constant in ("WARLOCK_DOT_MIN_TARGET_HEALTH = 35",
                         "WARLOCK_DOT_MIN_SPREAD_HEALTH = 50",
                         "WARLOCK_SPREAD_MAX_EXTRA_TARGETS = 2"):
            self.assertIn(constant, policy)
        # Triggers call the decisions; they do not re-type the numbers.
        self.assertNotRegex(TRIGGERS, r"\bWARLOCK_(DOT|SPREAD|DRAIN|COMBAT)_")


if __name__ == "__main__":
    unittest.main()
