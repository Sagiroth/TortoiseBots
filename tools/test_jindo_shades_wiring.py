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
SPELLS = read("ai/playerbot/strategy/actions/GenericSpellActions.cpp")
ATTACK = read("ai/playerbot/strategy/actions/AttackAction.cpp")
TRIGGERS = read("ai/playerbot/strategy/triggers/BossTacticsTriggers.cpp")


class JindoShadesWiring(unittest.TestCase):
    def test_kill_first_reads_the_carrier_mode(self):
        self.assertIn("const KillFirstPickMode mode = KillFirstPickModeFor(IsBattleDps(bot), !ordered.IsEmpty(),\n"
                      "        OrderIsAutomated(ordered.GetRawValue(), AI_VALUE(ObjectGuid, \"automated attack target\")"
                      ".GetRawValue()),", ACTIONS)
        # t1106b: one reach for every bot (the hold reach, 40 yd), not the old 15/30 yd first override.
        self.assertIn("return PickBossKillFirst(ai, bot, fight, mode, kCarrierHoldReachYd);", ACTIONS)
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

    def test_a_carrier_that_does_not_switch_is_logged_with_its_reason(self):
        # t1106b: the walk could not tell why a cursed DPS stayed on Jin'do; the action now says it, once per 5 s.
        start = ACTIONS.index("bool BossTacticsKillFirstAction::Execute(")
        action = ACTIONS[start:ACTIONS.index("\n}\n", start)]
        self.assertIn("constexpr uint32 kCarrierNoSwitchLogGapMs = 5000;", ACTIONS)
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsKillFirst", info1, info2);', ACTIONS)
        self.assertIn("CarrierNoSwitchReason(true, shade != nullptr, pickable, shadeYd, kCarrierHoldReachYd)", ACTIONS)
        # The no-pick case is only the trigger's to see (the action runs once there is a pick).
        self.assertIn("LogCarrierNoSwitch(ai, bot, fight, nullptr, AI_VALUE(Unit*, \"current target\"));", TRIGGERS)
        # t1106b r2: the target value can name the pick while the live victim is another unit; the trigger must still
        # fire and the action must re-attack, not only log.
        self.assertIn('LogCarrierNoSwitch(ai, bot, fight, pick, current, "reattack");', ACTIONS)
        self.assertIn('return KillFirstNeedsAttack(true, AI_VALUE(Unit*, "current target") == pick, '
                      "bot->GetVictim() == pick);", TRIGGERS)
        self.assertIn("if (bot->GetVictim() != pick)", action)
        self.assertIn("return Attack(bot, pick);", action)
        self.assertIn("reason = \"not_read\";", ACTIONS)
        self.assertIn("reason = \"player_order\";", ACTIONS)
        # The reason rule lives in the policy, not the glue.
        policy = read("ai/playerbot/BossTacticsPolicy.h")
        self.assertIn("enum class CarrierNoSwitch : std::uint8_t { None, NoShade, NotAttackable, BeyondReach };", policy)
        self.assertIn("if (distanceYd > reachYd)\n            return CarrierNoSwitch::BeyondReach;", policy)

    def test_a_carrier_is_known_without_a_shade_in_reach(self):
        self.assertIn("return HasCarrierKillFirstRow(ScanBossTactics(ai, bot).rows);", ACTIONS)
        self.assertIn("bool const gatedCarrier = totemTarget && ai::CarriesAuraGatedKillFirst(ai, bot);", COMMANDS)

    def test_no_dot_and_no_pet_on_a_needs_aura_subject(self):
        # A Shade takes direct damage only from its carrier: a DoT tick from an expired curse and a pet's hit count
        # for nothing (boss_jindo.cpp DamageTaken), so both paths refuse it.
        policy = read("ai/playerbot/BossTacticsPolicy.h")
        self.assertIn("inline bool AuraGatedDirectOnlyDamage(bool gateKeyOn, std::uint32_t requiredAura)", policy)
        dot = SPELLS[SPELLS.index("bool DotCastAtTarget(PlayerbotAI* ai, Unit* target, uint32 spellId)"):]
        dot = dot[:dot.index("\n    }")]
        self.assertIn("AuraGatedDirectOnlyDamage(sPlayerbotAIConfig.dungeonRunAuraGatedTargets,", dot)
        self.assertIn("GetBossTactics().RequiredAura(ai->GetBot()->GetMapId(), target->GetEntry())", dot)
        pet = ATTACK[ATTACK.index("bool AttackAction::CanPetAttack("):]
        pet = pet[:pet.index("\n}\n")]
        self.assertIn("AuraGatedDirectOnlyDamage(sPlayerbotAIConfig.dungeonRunAuraGatedTargets,", pet)
        self.assertIn("GetBossTactics().RequiredAura(bot->GetMapId(), target->GetEntry())", pet)

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
