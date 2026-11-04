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
STRATEGY = (PB / "strategy/warlock/WarlockStrategy.cpp").read_text()


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

    def test_manual_curse_elements_order_is_not_gated(self):
        for start in ("bool CurseOfTheElementsTrigger::IsActive()",
                      "bool CurseOfTheElementsOnAttackerTrigger::IsActive()"):
            fn = body(TRIGGERS, start, "\n}\n")
            self.assertIn('ai->HasStrategy("curse elements", BotState::BOT_STATE_COMBAT)', fn, start)

    def test_spread_rows_use_the_spread_gate(self):
        for start in ("bool CorruptionOnAttackerTrigger::IsActive()",
                      "bool SiphonLifeOnAttackerTrigger::IsActive()",
                      "bool ImmolateOnAttackerTrigger::IsActive()",
                      "bool CurseOfTheElementsOnAttackerTrigger::IsActive()"):
            fn = body(TRIGGERS, start, "\n}\n")
            self.assertIn("WarlockSpreadTargetAllowed(", fn, start)
        # The raid cap lives in the policy, not in a trigger loop copy.
        helper = body(TRIGGERS, "bool WarlockSpreadTargetAllowed(", "\n}\n")
        self.assertIn("TortoiseBots::WarlockSpreadCapApplies(", helper)
        self.assertIn("TortoiseBots::WarlockSpreadAllowed(true, true,", helper)
        # The curse spread skips attackers below the spread line as well, and
        # the Agony spread is capped like every other spread row.
        fn = body(TRIGGERS, "bool NoCurseOnAttackerTrigger::IsActive()", "\n}\n")
        self.assertIn("WarlockDotTargetAllowed(ai, attacker, true)", fn)
        self.assertIn('WarlockExtraDottedTargets(ai, "curse of agony", true)', fn)
        self.assertIn("TortoiseBots::WarlockSpreadAllowed(", fn)

    def test_drain_soul_uses_the_boss_and_shard_gate(self):
        fn = body(TRIGGERS, "bool DrainSoulTrigger::IsActive()", "\n}\n")
        self.assertIn("WarlockBossTarget(ai, ", fn)
        # The lookup is only paid for with the key on.
        self.assertIn("raidRotation && WarlockBossTarget(ai, ", fn)
        boss = body(TRIGGERS, "bool WarlockBossTarget(PlayerbotAI* ai, Unit* target)", "\n}\n")
        self.assertIn("TortoiseBots::WarlockIsBossTarget(", boss)
        self.assertIn("creature->IsWorldBoss()", boss)
        self.assertIn("CREATURE_FLAG_EXTRA_INSTANCE_BIND", boss)
        self.assertIn("GetDungeonPlans().IsBossEntry(", boss)
        self.assertIn("TortoiseBots::WarlockDrainSoulAllowed(", fn)
        # Below Shadowburn with the key on, above the rotation without it.
        fn = body(STRATEGY, '"drain soul",', "NULL)));")
        self.assertIn("sPlayerbotAIConfig.warlockRaidRotation ? ACTION_HIGH - 1 : ACTION_HIGH + 1", fn)

    def test_life_tap_uses_the_two_filler_gate_in_combat(self):
        fn = body(TRIGGERS, "bool LifeTapTrigger::IsActive()", "\n}\n")
        self.assertIn("WarlockShadowBoltCost(ai)", fn)
        self.assertIn("TortoiseBots::WarlockCombatTapAllowed(", fn)
        # The two-filler test replaces the mana line in combat: a cheaper
        # band would keep the 40% rule in force.
        self.assertNotIn("DecideWarlockTap(inputs, inCombat) != TortoiseBots::WarlockTapDecision::TapUrgent", fn)
        # Never gate the out-of-combat urgent or top-up rows on a cost.
        topup = body(TRIGGERS, "bool LifeTapTopUpTrigger::IsActive()", "\n}\n")
        self.assertNotIn("WarlockCombatTapAllowed(", topup)

    def test_boss_and_raid_group_decisions_are_policy_functions(self):
        gate = body(TRIGGERS, "bool WarlockDotTargetAllowed(", "\n}\n")
        self.assertIn("TortoiseBots::WarlockIsBossTarget(", body(
            TRIGGERS, "bool WarlockBossTarget(", "\n}\n"))
        self.assertIn("WarlockBossTarget(ai, target)", gate)
        spread = body(TRIGGERS, "bool WarlockSpreadTargetAllowed(", "\n}\n")
        self.assertIn("TortoiseBots::WarlockSpreadCapApplies(", spread)
        self.assertIn("WarlockBossTarget(ai, target)", spread)

    def test_bosses_and_raid_utility_curses_skip_the_life_gate(self):
        for start in ("bool CurseOfTheElementsTrigger::IsActive()",
                      "bool CurseOfRecklessnessRaidTrigger::IsActive()",
                      "bool CurseOfShadowRaidTrigger::IsActive()"):
            fn = body(TRIGGERS, start, "\n}\n")
            self.assertIn("WarlockDotTargetAllowed(ai, GetTarget(), false, true)", fn, start)
        fn = body(TRIGGERS, "bool CurseOfTheElementsOnAttackerTrigger::IsActive()", "\n}\n")
        self.assertIn('WarlockSpreadTargetAllowed(ai, GetTarget(), "curse of the elements", false, true)', fn)

    def test_policy_header_is_the_single_threshold_source(self):
        policy = (ROOT / "runtime/policy/class/WarlockRaidRotationPolicy.h").read_text()
        for constant in ("WARLOCK_DOT_MIN_TARGET_HEALTH = 35",
                         "WARLOCK_DOT_MIN_SPREAD_HEALTH = 50",
                         "WARLOCK_SPREAD_MAX_EXTRA_TARGETS = 2",
                         "WARLOCK_DRAIN_SOUL_MAX_SHARDS = 2",
                         "WARLOCK_COMBAT_TAP_NUKE_COUNT = 2"):
            self.assertIn(constant, policy)
        for function in ("WarlockIsBossTarget(", "WarlockSpreadCapApplies("):
            self.assertIn(function, policy)
            self.assertNotIn("bool " + function, TRIGGERS)
        # Triggers call the decisions; they do not re-type the numbers.
        self.assertNotRegex(TRIGGERS, r"\bWARLOCK_(DOT|SPREAD|DRAIN|COMBAT)_")


if __name__ == "__main__":
    unittest.main()
