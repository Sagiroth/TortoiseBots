"""Wiring pins for the boss `gate_hold` kind (brief C: DPS spare a boss at its health gate).

The decision is pure and tested in tools/test_boss_tactics_policy.cpp (GateHeld, CountsAsFightingBlocker,
GateHoldRadius, the loader shape); this pin proves that the live bot reads it: the fight scan keeps the rows behind
AiPlayerbot.DungeonRunGateHold, the picker drops a spared unit, the hold action takes it off the current target, the
multiplier stands down area and DoT casts beside it, and the count uses the fighting-blocker value.
Run: python3 -m unittest tools/test_gate_hold_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY = (ROOT / "ai/playerbot/BossTacticsPolicy.h").read_text()
MGR = (ROOT / "ai/playerbot/BossTacticsMgr.cpp").read_text()
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
ACTIONS_H = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.h").read_text()
DPS_TARGET = (ROOT / "ai/playerbot/strategy/values/DpsTargetValue.cpp").read_text()
VALUE_H = (ROOT / "ai/playerbot/strategy/values/HoldKillBlockersValue.h").read_text()
VALUE = (ROOT / "ai/playerbot/strategy/values/HoldKillBlockersValue.cpp").read_text()
CONTEXT = (ROOT / "ai/playerbot/strategy/values/ValueContext.h").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
DOC = (ROOT / "docs/play/boss-tactics.md").read_text()


def body(text, signature, end="\n}\n"):
    start = text.index(signature)
    return text[start:text.index(end, start + len(signature))]


class GateHoldWiring(unittest.TestCase):
    def test_the_flag_is_off_by_default_and_recommended_on(self):
        self.assertIn("bool dungeonRunGateHold = false;", CONFIG_H)
        self.assertIn('dungeonRunGateHold = config.GetBoolDefault("AiPlayerbot.DungeonRunGateHold", false);', CONFIG)
        self.assertIn("# 1 - on\nAiPlayerbot.DungeonRunGateHold = 0", TEMPLATE)
        self.assertIn("recommended on", TEMPLATE.split("AiPlayerbot.DungeonRunGateHold")[0].rsplit("# The gate_hold", 1)[1])
        self.assertIn("`AiPlayerbot.DungeonRunGateHold`", SETTINGS)
        self.assertIn("(recommended on)", SETTINGS.split("DungeonRunGateHold", 1)[1].splitlines()[0])

    def test_the_kind_and_its_rule_live_in_the_policy(self):
        self.assertIn("GateHold = 26", POLICY)
        self.assertIn('if (text == "gate_hold")', POLICY)
        self.assertIn("inline bool GateHeld(const ActiveBossRows& rows, std::uint32_t entry, int hpPct,", POLICY)
        self.assertIn("inline bool CountsAsFightingBlocker(bool inCombat, bool evading, bool notSelectable)", POLICY)
        self.assertIn("return inCombat && !evading;", POLICY)
        self.assertIn("inline float GateHoldRadius(float windowSec)", POLICY)
        held = body(POLICY, "inline bool GateHeld(")
        self.assertIn("row->kind != BossTacticKind::GateHold || row->bossEntry != entry", held)
        self.assertIn("hpPct > static_cast<int>(row->param)", held)
        self.assertIn("countFighting(*row) > row->priority", held)

    def test_the_loader_refuses_a_bad_gate_row(self):
        add = body(POLICY, "bool Add(const BossTacticRow& row)")
        rule = add[add.index("if (row.kind == BossTacticKind::GateHold"):]
        rule = rule[:rule.index("return false;")]
        self.assertIn("row.targetEntry == 0", rule)
        self.assertIn("row.targetEntry == row.bossEntry", rule)
        self.assertIn("row.spellId != 0", rule)
        self.assertIn("row.param >= 1.0f", rule)
        self.assertIn("row.param > 99.0f", rule)
        self.assertIn("row.windowSec == 0.0f", rule)
        self.assertIn("row.windowSec >= 50.0f && row.windowSec <= 250.0f", rule)
        self.assertIn("RowGateSet(row)", rule)
        self.assertIn("row.mode != TacticMode::Default", rule)
        self.assertIn("gateHoldMaps_[row.map] = true;", add)
        self.assertIn("HasGateHoldMap", POLICY)
        self.assertIn("a gate_hold row it", MGR)
        self.assertIn("a gate_hold row whose blocker is its own boss", MGR)

    def test_the_count_is_the_fighting_blocker_value(self):
        self.assertIn('class GateHoldFightingBlockersValue', VALUE_H)
        self.assertIn('CalculatedValue<uint32>(ai, "fighting blockers", 1)', VALUE_H)
        self.assertIn('creators["fighting blockers"] = [](PlayerbotAI* ai) { return new GateHoldFightingBlockersValue(ai); };',
                      CONTEXT)
        calc = body(VALUE, "uint32 GateHoldFightingBlockersValue::Calculate()")
        self.assertIn('getMultiQualifiers(qualifier, "::")', calc)
        self.assertIn("CountsAsFightingBlocker(creature->IsInCombat(), creature->IsInEvadeMode(),", calc)
        self.assertIn("UNIT_FLAG_NOT_SELECTABLE", calc)
        self.assertNotIn("CountsAsBlocker(", calc)

    def test_every_live_reader_goes_through_the_gate(self):
        self.assertIn("std::uint32_t ai::GateHoldFighters(", ACTIONS)
        self.assertIn("bool ai::GateHoldsUnit(", ACTIONS)
        self.assertIn("GateHoldsUnit(PlayerbotAI* ai, const ActiveBossRows& rows, Unit* unit);", ACTIONS_H)
        # The fight scan keeps the rows only behind the key.
        scan = body(ACTIONS, "BossTacticsFight ai::ScanBossTactics(")
        self.assertIn("if (!gateHoldOn && row.kind == BossTacticKind::GateHold)", scan)
        # The picker: gate rows in the candidate's own rows, and the gate count handed to DpsLeavesAlone.
        held = body(DPS_TARGET, "bool HeldDpsTarget(PlayerbotAI* ai, Player* bot, Unit* unit)")
        self.assertIn("row.kind == BossTacticKind::GateHold && !sPlayerbotAIConfig.dungeonRunGateHold", held)
        self.assertIn("gateRow = gateRow || row.kind == BossTacticKind::GateHold;", held)
        self.assertIn("GateHoldFighters(ai, row.targetEntry, GateHoldRadius(row.windowSec))", held)
        self.assertIn("DpsLeavesAlone(true, active, unit->GetEntry(), static_cast<int>(unit->GetHealthPercent()), "
                      "countNear,", held)
        # The hold action takes it off the current target, and its fallback does not re-pick it.
        find = body(ACTIONS, "Unit* ai::FindBossHoldKill(")
        self.assertIn("GateHoldsUnit(ai, fight.rows, current)", find)
        action = body(ACTIONS, "bool BossTacticsHoldKillAction::Execute(")
        self.assertIn("!GateHoldsUnit(ai, fight.rows, unit)", action)
        # The area and DoT veto.
        veto = body(ACTIONS, "HoldReason HeldSubjectWithin(")
        self.assertIn("row->kind != BossTacticKind::GateHold", veto)
        self.assertIn("GateHoldsUnit(ai, fight.rows, unit)", veto)
        self.assertIn("HoldReason::Gate", veto)
        multiplier = body(ACTIONS, "float BossTacticsHoldMultiplier::GetValue(")
        self.assertIn("GateHoldsUnit(ai, fight.rows, picked)", multiplier)
        self.assertIn("GateHoldsUnit(ai, fight.rows, current)", multiplier)
        self.assertIn("HasGateHoldMap(bot->GetMapId())", multiplier)

    def test_the_docs_and_sql_checks_name_the_kind(self):
        kinds = DOC[DOC.index("### Kinds"):DOC.index("## Shipped rows")]
        self.assertIn("| `gate_hold` |", kinds)
        self.assertIn("`AiPlayerbot.DungeonRunGateHold = 1`", kinds)
        self.assertIn("`gate_hold` needs `AiPlayerbot.DungeonRunGateHold = 1`", DOC)
        self.assertIn("gate_hold", DOC[DOC.index("| `kind` |"):DOC.index("| `spell_id` |")])
        sql = (ROOT / "tools/test_boss_tactics_sql.py").read_text()
        self.assertIn('"seek_pull", "gate_hold"', sql)
        self.assertIn('if kind == "gate_hold":', sql)


if __name__ == "__main__":
    unittest.main()
