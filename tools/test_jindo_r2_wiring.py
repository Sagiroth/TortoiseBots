"""Pins the glue of fix/jindo-r2: the carrier hold (AiPlayerbot.DungeonRunCarrierHold: "not dps target active" stays
quiet for a Delusions carrier on its Shade, the kill-first trigger checks every second), the taunt gate's holding_boss
refusal and the per-(bot, word) refusal-row throttle.

The rules are unit-tested in tools/test_boss_tactics_policy.cpp (CarrierHoldsCurrent, KillFirstCheckSeconds,
TauntGateFor, TauntGateLogDueFor). These pins only prove the live code calls them, behind the key, and that the key
ships off.
"""
import os
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as handle:
        return handle.read()


ACTIONS = read("ai/playerbot/strategy/actions/BossTacticsActions.cpp")
POLICY = read("ai/playerbot/BossTacticsPolicy.h")
GENERIC = read("ai/playerbot/strategy/triggers/GenericTriggers.cpp")
TRIGGERS_H = read("ai/playerbot/strategy/triggers/BossTacticsTriggers.h")
CONFIG_H = read("ai/playerbot/PlayerbotAIConfig.h")
CONFIG_CPP = read("ai/playerbot/PlayerbotAIConfig.cpp")
CONF = read("ai/playerbot/aiplayerbot.conf.dist.in")
DOCS = read("docs/play/boss-tactics.md")
DOCS_RUNS = read("docs/server/settings/dungeons-raids.md")
DOCS_ROWS = read("docs/play/refusal-rows.md")


def member(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


class JindoR2Wiring(unittest.TestCase):
    def test_the_hold_key_ships_off(self):
        self.assertIn("bool dungeonRunCarrierHold = false;", CONFIG_H)
        self.assertIn('config.GetBoolDefault("AiPlayerbot.DungeonRunCarrierHold", false)', CONFIG_CPP)
        self.assertIn("AiPlayerbot.DungeonRunCarrierHold = 0", CONF)

    def test_not_dps_target_active_calls_the_hold_helper(self):
        body = member(GENERIC, "bool NotDpsTargetActiveTrigger::IsActive()", "\nbool NotDpsAoeTargetActiveTrigger")
        self.assertIn("if (CarrierHoldsCurrent(sPlayerbotAIConfig.dungeonRunCarrierHold, requiredAura != 0,", body)
        self.assertIn("if (sPlayerbotAIConfig.dungeonRunCarrierHold)\n", body)
        self.assertIn("requiredAura != 0 && bot->HasAura(requiredAura)))", body)
        self.assertIn("GetBossTactics().RequiredAura(bot->GetMapId(), target->GetEntry());", body)
        # The hold answers "not active" and runs before the dps-target comparison, so the carrier is never pulled.
        self.assertLess(body.index("CarrierHoldsCurrent("), body.index('AI_VALUE(Unit*, "dps target")'))
        self.assertIn("return false;", body[body.index("CarrierHoldsCurrent("):body.index('AI_VALUE(Unit*, "enemy player target")')])

    def test_the_kill_first_interval_reads_the_policy(self):
        self.assertIn("KillFirstCheckSeconds(sPlayerbotAIConfig.dungeonRunTotemFocus,", TRIGGERS_H)
        self.assertIn("sPlayerbotAIConfig.dungeonRunCarrierHold)) {}", TRIGGERS_H)
        self.assertIn("inline std::uint32_t KillFirstCheckSeconds(bool totemFocus, bool carrierHold)", POLICY)
        self.assertIn("return (totemFocus || carrierHold) ? 1 : 2;", POLICY)

    def test_the_policy_helpers_are_pure(self):
        self.assertIn("inline bool CarrierHoldsCurrent(bool keyOn, bool currentIsNeedsAura, bool botHasRequiredAura)", POLICY)
        self.assertIn("return keyOn && currentIsNeedsAura && botHasRequiredAura;", POLICY)
        self.assertIn("inline bool TauntGateLogDueFor(TauntGateLogTimes& lastMs, TauntGate gate, std::uint32_t nowMs,", POLICY)

    def test_the_taunt_gate_reads_the_boss_victim(self):
        gate = member(ACTIONS, "bool ai::TauntRefusedByGate(", "\n}\n")
        self.assertIn("bool const holdsBoss = botHasAura && BotHoldsBoss(ai, bot, target);", gate)
        self.assertIn("TauntGateLogDue(bot, gate)", gate)
        holds = member(ACTIONS, "static bool BotHoldsBoss(", "\n}\n")
        self.assertIn("attacker->GetVictim() != bot", holds)
        self.assertIn("GetBossTactics().Rows(", holds)
        self.assertIn("attacker == exclude", holds)

    def test_the_log_throttle_is_per_word(self):
        due = member(ACTIONS, "static bool TauntGateLogDue(", "\n}\n")
        self.assertIn("return TauntGateLogDueFor(g_tauntGateLog[bot->GetObjectGuid().GetRawValue()], gate, nowMs,", due)
        self.assertIn("std::unordered_map<uint64, TauntGateLogTimes> g_tauntGateLog;", ACTIONS)

    def test_docs_name_the_flag_and_the_word(self):
        self.assertIn("DungeonRunCarrierHold", DOCS)
        self.assertIn("DungeonRunCarrierHold", DOCS_RUNS)
        self.assertIn("holding_boss", DOCS)
        self.assertIn("holding_boss", DOCS_ROWS)
        self.assertIn("CarrierScan is redundant while KillFirstGrid is on", DOCS)


if __name__ == "__main__":
    unittest.main()
