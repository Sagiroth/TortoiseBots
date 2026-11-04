"""Wiring pins for the burn_mana and ranged_only boss-tactic kinds.

The rules are pure (ai/playerbot/policy/raid/BurnManaPolicy.h, RangedOnlyPolicy.h) and tested in
tools/test_burn_mana_policy.cpp and tools/test_ranged_only_policy.cpp; the loader shape is in
tools/test_boss_tactics_policy.cpp and the live rows in tools/test_boss_tactics_sql.py. These pins only prove the live
code calls them: the boss-tactic triggers and actions exist and are registered, the dps and tank target values leave a
ranged_only subject alone, and both keys are read, templated and documented.

Run: python3 -m unittest tools/test_ranged_only_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/BossTacticsTriggers.cpp").read_text()
STRATEGY = (ROOT / "ai/playerbot/strategy/generic/BossTacticsStrategy.cpp").read_text()
ACTION_CONTEXT = (ROOT / "ai/playerbot/strategy/actions/ActionContext.h").read_text()
TRIGGER_CONTEXT = (ROOT / "ai/playerbot/strategy/triggers/TriggerContext.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
DOCS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
BOSS_DOCS = (ROOT / "docs/play/boss-tactics.md").read_text()
DPS_VALUE = (ROOT / "ai/playerbot/strategy/values/DpsTargetValue.cpp").read_text()
TANK_VALUE = (ROOT / "ai/playerbot/strategy/values/TankTargetValue.cpp").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class RangedOnlyWiring(unittest.TestCase):
    def test_dps_picker_skips_a_held_subject(self):
        held = body(DPS_VALUE, "    bool HeldDpsTarget(PlayerbotAI* ai, Player* bot, Unit* unit)")
        self.assertIn("if (IsRangedOnlyHeld(ai, bot, unit))", held)
        # Before the map lookup and the free-DPS gate: the ranged_only row's own key and role check do the gating.
        self.assertLess(held.index("IsRangedOnlyHeld(ai, bot, unit)"), held.index("table.HasMap("))

    def test_tank_picker_skips_a_held_subject(self):
        calculate = body(TANK_VALUE, "Unit* TankTargetValue::Calculate()")
        self.assertIn("if (rti && IsRangedOnlyHeld(ai, bot, rti))", calculate)
        self.assertIn("rti = nullptr;", calculate)
        tournament = body(TANK_VALUE, "    virtual void CheckAttacker(Unit* attacker, ThreatManager* /*threatManager*/) override")
        self.assertIn("if (IsRangedOnlyHeld(ai, ai->GetBot(), attacker)) return;", tournament)

    def test_hold_is_keyed_and_melee_only(self):
        hold = body(ACTIONS, "bool ai::IsRangedOnlyHeld(PlayerbotAI* ai, Player* bot, Unit* unit, float* gapYdOut)")
        self.assertIn("sPlayerbotAIConfig.dungeonRunRangedOnly", hold)
        self.assertIn("ai->IsRanged(bot)", hold)
        self.assertIn("PlayerbotAI::IsHeal(bot)", hold)
        self.assertIn("RangedOnlyHolds(true, unit->GetPositionZ(), bot->GetPositionZ(), row.param)", hold)
        self.assertIn("BossTacticKind::RangedOnly", hold)
        self.assertIn("float gap = unit->GetPositionZ() - bot->GetPositionZ();", hold)

    def test_trigger_drops_the_target_and_logs(self):
        trigger = body(TRIGGERS, "bool BossTacticsRangedOnlyTrigger::IsActive()")
        self.assertIn("IsRangedOnlyHeld(ai, bot, current, &gapYd)", trigger)
        self.assertIn("LogBossRangedOnly(ai, bot, current, gapYd)", trigger)
        action = body(ACTIONS, "bool BossTacticsRangedOnlyAction::Execute(Event& event)")
        self.assertIn("IsRangedOnlyHeld(ai, bot, held)", action)
        self.assertIn("FindBossKillFirst(ai, bot, fight)", action)

    def test_trigger_and_action_are_registered(self):
        self.assertIn('creators["boss tactics ranged only"] = [](PlayerbotAI* ai) '
                      '{ return new BossTacticsRangedOnlyTrigger(ai); };', TRIGGER_CONTEXT)
        self.assertIn('creators["boss tactics ranged only"] = [](PlayerbotAI* ai) '
                      '{ return new BossTacticsRangedOnlyAction(ai); };', ACTION_CONTEXT)
        self.assertIn('"boss tactics ranged only"', STRATEGY)
        self.assertIn('new NextAction("boss tactics ranged only", ACTION_HIGH + 2)', STRATEGY)

    def test_config_key_is_read_templated_and_documented(self):
        self.assertIn('dungeonRunRangedOnly = config.GetBoolDefault("AiPlayerbot.DungeonRunRangedOnly", false);',
                      CONFIG_CPP)
        self.assertIn("bool dungeonRunRangedOnly = false;", CONFIG_H)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.DungeonRunRangedOnly = 0$")
        self.assertIn("AiPlayerbot.DungeonRunRangedOnly", DOCS)

    def test_kind_word_parses(self):
        policy = (ROOT / "ai/playerbot/BossTacticsPolicy.h").read_text()
        self.assertIn('if (text == "ranged_only")', policy)
        self.assertIn("RangedOnly = 21", policy)


class BurnManaWiring(unittest.TestCase):
    def test_trigger_and_action_are_live(self):
        trigger = body(TRIGGERS, "bool BossTacticsBurnManaTrigger::IsActive()")
        self.assertIn("sPlayerbotAIConfig.dungeonRunBurnMana", trigger)
        self.assertIn("FindBossBurnMana(ai, bot, ScanBossTactics(ai, bot))", trigger)
        action = body(ACTIONS, "bool BossTacticsBurnManaAction::Execute(Event& event)")
        self.assertIn("FindBossBurnMana(ai, bot, ScanBossTactics(ai, bot), &manaPct)", action)
        self.assertIn("BossBurnManaSpell(ai, boss)", action)
        self.assertIn("ai->CastSpell(spell, boss)", action)
        self.assertIn('"BossTacticsBurnMana"', ACTIONS)
        self.assertIn('const char* const kBurnManaSpells[] = { "mana burn", "drain mana", "viper sting" };',
                      ACTIONS)

    def test_trigger_and_action_are_registered(self):
        self.assertIn('creators["boss tactics burn mana"] = [](PlayerbotAI* ai) '
                      '{ return new BossTacticsBurnManaTrigger(ai); };', TRIGGER_CONTEXT)
        self.assertIn('creators["boss tactics burn mana"] = [](PlayerbotAI* ai) '
                      '{ return new BossTacticsBurnManaAction(ai); };', ACTION_CONTEXT)
        self.assertIn('new NextAction("boss tactics burn mana", ACTION_HIGH + 3)', STRATEGY)

    def test_finder_reads_the_row_through_the_policy(self):
        finder = body(ACTIONS, "Unit* ai::FindBossBurnMana(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight, int* manaPctOut)")
        self.assertIn("BurnManaWanted(static_cast<float>(manaPct), row->param, true, rank, row->priority)", finder)
        self.assertIn("BossTacticKind::BurnMana", finder)
        self.assertIn("boss->GetMaxPower(POWER_MANA)", finder)
        self.assertIn("bot->GetDistance(boss) > 30.0f", finder)
        rank = body(ACTIONS, "int ai::BossBurnManaRank(PlayerbotAI* ai, Player* bot)")
        self.assertIn("LiveGroupMembers(group)", rank)
        self.assertIn("member->GetObjectGuid().GetRawValue() >= self", rank)
        self.assertIn("PlayerbotAI::IsTank(player) || PlayerbotAI::IsHeal(player)", ACTIONS)

    def test_config_key_is_read_templated_and_documented(self):
        self.assertIn('dungeonRunBurnMana = config.GetBoolDefault("AiPlayerbot.DungeonRunBurnMana", false);',
                      CONFIG_CPP)
        self.assertIn("bool dungeonRunBurnMana = false;", CONFIG_H)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.DungeonRunBurnMana = 0$")
        self.assertIn("AiPlayerbot.DungeonRunBurnMana", DOCS)

    def test_vanish_rows_documented(self):
        self.assertIn("burn_mana", BOSS_DOCS)
        self.assertIn("ranged_only", BOSS_DOCS)


if __name__ == "__main__":
    unittest.main()
