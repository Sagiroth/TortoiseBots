"""Pins the glue of t1105h part B (Jin'do): the carrier override, the healers' Shade-victim weighting and tank_ignore.

The rules themselves are unit-tested in tools/test_boss_tactics_policy.cpp (KillFirstPickModeFor, PickCarrierOverride,
TankIgnoresUnit, the tank_ignore table), tools/test_dungeon_run_policy.cpp (FocusSkipsBot's carrier, TankIgnoredFact,
TankSkipsUnit) and tools/test_healing_triage_policy.cpp (HealingCandidate::hunted). These pins only prove the live code
calls them.
"""
import os
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as handle:
        return handle.read()


ACTIONS = read("ai/playerbot/strategy/actions/BossTacticsActions.cpp")
TARGETS = read("ai/playerbot/strategy/values/PossibleAttackTargetsValue.cpp")
HEAL = read("ai/playerbot/strategy/values/PartyMemberToHeal.cpp")
COMMANDS = read("commands/Commander.cpp")


class JindoShadesWiring(unittest.TestCase):
    def test_kill_first_reads_the_carrier_mode(self):
        self.assertIn("const KillFirstPickMode mode = KillFirstPickModeFor(IsBattleDps(bot),\n"
                      "        !AI_VALUE(ObjectGuid, \"explicit attack target\").IsEmpty(), "
                      "HasCarrierKillFirstRow(fight.rows));", ACTIONS)
        self.assertIn("? PickCarrierOverride(fight.rows, candidates, CarrierReachYd(ai->IsRanged(bot)))", ACTIONS)
        # The old gate is gone from the pick: an explicit order no longer returns before the carrier override.
        start = ACTIONS.index("Unit* ai::FindBossKillFirst(")
        self.assertNotIn("IsFreeDps", ACTIONS[start:ACTIONS.index("Unit* ai::FindBossCarrierPick(", start)])

    def test_the_kill_first_action_drops_the_order(self):
        start = ACTIONS.index("bool BossTacticsKillFirstAction::Execute(")
        action = ACTIONS[start:ACTIONS.index("\n}\n", start)]
        self.assertIn("context->GetValue<ObjectGuid>(\"explicit attack target\")->Set(ObjectGuid());", action)

    def test_tanks_refuse_a_tank_ignore_target(self):
        self.assertIn("if (botAi && TankIgnoresTarget(botAi, player, target))", TARGETS)
        self.assertIn("if (TankIgnoresTarget(ai, bot, unit))", ACTIONS)
        self.assertIn("return TankIgnoresUnit(true, bosses, engaged);", ACTIONS)

    def test_the_run_facts_carry_the_tank_ignore_boss(self):
        self.assertEqual(COMMANDS.count("fact.tankIgnoreBoss = TankIgnoreBossOf("), 3)

    def test_healers_weight_the_hunted(self):
        self.assertIn("candidate.hunted = huntOn && HuntedByGatedUnit(target);", HEAL)
        self.assertIn("bool const huntOn = sPlayerbotAIConfig.dungeonRunAuraGatedTargets &&", HEAL)


if __name__ == "__main__":
    unittest.main()
