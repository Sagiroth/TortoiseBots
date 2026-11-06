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
        self.assertIn("const KillFirstPickMode mode = KillFirstPickModeFor(IsBattleDps(bot), !ordered.IsEmpty(),\n"
                      "        OrderIsAutomated(ordered.GetRawValue(), AI_VALUE(ObjectGuid, \"automated attack target\")"
                      ".GetRawValue()),", ACTIONS)
        self.assertIn("return PickBossKillFirst(ai, bot, fight, mode, CarrierReachYd(ai->IsRanged(bot)));", ACTIONS)
        self.assertIn("return PickBossKillFirst(ai, bot, fight, KillFirstPickMode::Carrier, kCarrierHoldReachYd);",
                      ACTIONS)
        self.assertIn("? PickCarrierOverride(fight.rows, candidates, carrierReachYd)", ACTIONS)
        # The old gate is gone from the pick: an explicit order no longer returns before the carrier override.
        start = ACTIONS.index("Unit* ai::FindBossKillFirst(")
        self.assertNotIn("IsFreeDps", ACTIONS[start:ACTIONS.index("Unit* ai::FindBossCarrierPick(", start)])

    def test_the_kill_first_action_drops_the_order(self):
        start = ACTIONS.index("bool BossTacticsKillFirstAction::Execute(")
        action = ACTIONS[start:ACTIONS.index("\n}\n", start)]
        self.assertIn("context->GetValue<ObjectGuid>(\"explicit attack target\")->Set(ObjectGuid());", action)

    def test_only_the_runs_orders_are_marked_automated(self):
        # Every command commit clears the mark; only the run's focus and attack orders set it again (t1105h review).
        attack = read("ai/playerbot/strategy/actions/AttackAction.cpp")
        commit = attack[attack.index("void CommitExplicitAttackTarget("):]
        commit = commit[:commit.index("\n}\n")]
        self.assertIn("context->GetValue<ObjectGuid>(\"automated attack target\")->Set(ObjectGuid());", commit)
        self.assertIn("NoteRunOrder(ai, automatedKillFirst);", COMMANDS)
        self.assertIn("NoteRunOrder(ai, s_runIssuing);", COMMANDS)
        # A run attack held for a bot out of control keeps its source and is marked when it fires (Codex HIGH).
        self.assertIn("held.automated = s_runIssuing;", COMMANDS)
        self.assertIn("if (RunBotAttackOrder(ai, bot, bot, ObjectGuid(order.targetRaw)))\n"
                      "                NoteRunOrder(ai, order.automated);", COMMANDS)
        self.assertIn('creators["automated attack target"]', read("ai/playerbot/strategy/values/ValueContext.h"))
        self.assertIn("CapHuntedBoost(candidates, bands);", HEAL)

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
