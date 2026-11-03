"""Wiring pins for the aura tactic kinds' shared ground: the row gates, the loader columns, the master key and the
channel-slot interrupt.

A row of `ai_boss_tactic` may carry a boss-health band (`hp_min`, `hp_max`), a bot aura (`bot_aura`), a per-kind mode
word (`tactic_mode`) and a linked_death window (`window_s`). Only a row a fight scan reads can answer a gate, and with
AiPlayerbot.DungeonRunAuraTactics off a gated row is closed and the six aura kinds are not read at all, so the table
behaves exactly as it did before them. The rules live in ai/playerbot/BossTacticsPolicy.h (RowGateSet, RowGateOpen,
ParseTacticMode, IsAuraTacticKind, LinkedDeathWindowMs; their behaviour is tested in
tools/test_boss_tactics_policy.cpp). These pins only prove the live code calls them:

 - the scan reads the boss entry's health and this bot's gate aura, through RowGateOpen;
 - the loader selects and parses the five columns and lets the policy refuse a bad row;
 - the master key is read from the config, reloaded with the other DungeonRun keys, and documented in the template;
 - FindBossInterrupt reads both cast slots, so a channelled rotation (Mar'li's Drain Life) can be kicked;
 - the linked_death latch takes the group's own window instead of the constant.

Run: python3 -m unittest tools/test_aura_tactics_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
POLICY = (ROOT / "ai/playerbot/BossTacticsPolicy.h").read_text()
LOADER = (ROOT / "ai/playerbot/BossTacticsMgr.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
POLICY_TEST = (ROOT / "tools/test_boss_tactics_policy.cpp").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class RowGateWiring(unittest.TestCase):
    def test_the_scan_gates_each_row_on_the_boss_health_and_the_bot_aura(self):
        # The band reads the unit that activated the row (the first living of the boss entry the scan sees), never
        # an add's health: NX-46's "gate on the add's own health" is deliberately not supported.
        scan = body(ACTIONS, "BossTacticsFight ai::ScanBossTactics(")
        self.assertIn("int const bossHpPct = static_cast<int>(unit->GetHealthPercent());", scan)
        self.assertIn("bool const keyOn = sPlayerbotAIConfig.dungeonRunAuraTactics;", scan)
        self.assertIn("RowGateOpen(row, keyOn, bossHpPct, row.botAura != 0 && bot->HasAura(row.botAura))", scan)
        self.assertIn("fight.rows.push_back(&row);", scan)
        self.assertNotIn("GetHealthPercent()) && !RowGateOpen", scan)
        # The six aura kinds are the key's own: with it off their rows never reach a consumer.
        gated = scan[scan.index("for (const BossTacticRow& row : *rows)"):]
        self.assertIn("if (!keyOn && IsAuraTacticKind(row.kind))", gated)
        self.assertLess(gated.index("IsAuraTacticKind(row.kind)"), gated.index("RowGateOpen("))

    def test_the_policy_keeps_the_gate_pure(self):
        self.assertIn("inline bool RowGateSet(const BossTacticRow& row)", POLICY)
        self.assertIn("inline bool RowGateOpen(const BossTacticRow& row, bool keyOn, int bossHpPct, "
                      "bool botHasGateAura)", POLICY)
        gate = body(POLICY, "inline bool RowGateOpen(", "\n    }\n")
        self.assertIn("if (RowGateSet(row) && !keyOn)", gate)
        self.assertIn("if (bossHpPct < row.hpMin || bossHpPct > row.hpMax)", gate)
        self.assertIn("if (row.botAura != 0 && !botHasGateAura)", gate)
        self.assertIn("std::uint8_t hpMin = 0;", POLICY)
        self.assertIn("std::uint8_t hpMax = 100;", POLICY)
        self.assertIn("std::uint32_t botAura = 0;", POLICY)

    def test_only_a_scan_read_kind_may_gate(self):
        add = body(POLICY, "bool Add(const BossTacticRow& row)", "\n        }\n")
        self.assertIn("if (RowGateSet(row) && (row.kind == BossTacticKind::HoldKill || "
                      "row.kind == BossTacticKind::TankHold ||", add)
        self.assertIn("row.kind == BossTacticKind::OneAtATime || row.kind == BossTacticKind::LinkedDeath))", add)
        self.assertIn("if (row.hpMin > row.hpMax || row.hpMax > 100)", add)
        # The duplicate key includes the band, so one boss can carry the same kind once per band.
        self.assertIn("have.hpMin == row.hpMin && have.hpMax == row.hpMax", add)
        # ... and the test file pins it.
        self.assertIn("CHECK(table.Add(Gated(Row(531, 15263, BossTacticKind::DpsHoldAfter, 0, 0, 0, 3.0f), 0, 50)));",
                      POLICY_TEST)
        self.assertIn("CHECK(!table.Add(Gated(Row(531, 15263, BossTacticKind::DpsHoldAfter, 0, 0, 0, 3.0f), 0, 50)));",
                      POLICY_TEST)

    def test_new_kinds_and_modes_are_parsed_and_gated(self):
        self.assertIn("enum class TacticMode : std::uint8_t", POLICY)
        self.assertIn("inline TacticMode ParseTacticMode(BossTacticKind kind, const std::string& text)", POLICY)
        self.assertIn("inline bool IsAuraTacticKind(BossTacticKind kind)", POLICY)
        for name, value in (("dps_hold_after", 9), ("dispel_priority", 10), ("flee_on_aura", 11),
                            ("freeze_on_aura", 12), ("taunt_after_cast", 13), ("swap_on_aura", 14)):
            self.assertIn('if (text == "%s")' % name, POLICY)
            self.assertIn("%s = %d," % ("".join(part[:1].upper() + part[1:] for part in name.split("_")), value),
                          POLICY)
        self.assertIn("return TacticMode::Invalid;", POLICY)
        add = body(POLICY, "bool Add(const BossTacticRow& row)", "\n        }\n")
        self.assertIn("if (row.mode == TacticMode::Invalid)", add)
        self.assertIn("row.mode = ParseTacticMode(row.kind, fields[11].GetCppString());", LOADER)

    def test_the_map_flags_exist(self):
        self.assertIn("bool HasAuraTacticMap(std::uint32_t map) const", POLICY)
        self.assertIn("bool HasDispelMap(std::uint32_t map) const", POLICY)
        self.assertIn("if (IsAuraTacticKind(row.kind))", POLICY)
        self.assertIn("auraTacticMaps_[row.map] = true;", POLICY)
        self.assertIn("if (row.kind == BossTacticKind::DispelPriority)", POLICY)
        self.assertIn("dispelMaps_[row.map] = true;", POLICY)


class LoaderWiring(unittest.TestCase):
    def test_the_loader_selects_the_five_columns_after_burn_order(self):
        select = LOADER[LOADER.index("SELECT `map`"):LOADER.index("FROM `ai_boss_tactic`")]
        for column in ("burn_order", "hp_min", "hp_max", "bot_aura", "tactic_mode", "window_s"):
            self.assertIn("`%s`" % column, select)
        order = [select.index("`%s`" % column) for column in ("burn_order", "hp_min", "hp_max", "bot_aura",
                                                              "tactic_mode", "window_s")]
        self.assertEqual(sorted(order), order, "the SELECT's five new columns are not after burn_order")
        for index, field in ((8, "hp_min"), (9, "hp_max"), (10, "bot_aura"), (12, "window_s")):
            self.assertIn("fields[%d]" % index, LOADER)
        self.assertIn("row.windowSec = fields[12].GetFloat();", LOADER)
        self.assertIn("row.hpMin = fields[8].GetUInt8();", LOADER)
        self.assertIn("row.hpMax = fields[9].GetUInt8();", LOADER)
        self.assertIn("row.botAura = fields[10].GetUInt32();", LOADER)

    def test_the_skip_message_names_the_new_rules(self):
        message = LOADER[LOADER.index('sLog.outError("ai_boss_tactic:'):]
        message = message[:message.index("row.bossEntry")]
        # The message is one literal split over several lines: join it before matching a phrase.
        message = re.sub(r'"\s*(?:\n\s*)"', "", message)
        message = " ".join(message.split())
        for phrase in ("a health band outside 0-100", "a gate on a kind read straight from the table",
                       "a mode word its kind does not take", "linked_death window that is not positive",
                       "a second row of the key"):
            self.assertIn(phrase, message)


class MasterKeyWiring(unittest.TestCase):
    def test_the_key_is_read_reloaded_and_shipped(self):
        self.assertIn("bool dungeonRunAuraTactics = false;", CONFIG_H)
        load = body(CONFIG_CPP, "void PlayerbotAIConfig::LoadDungeonRunKeys()")
        self.assertIn('dungeonRunAuraTactics = config.GetBoolDefault("AiPlayerbot.DungeonRunAuraTactics", false);',
                      load)
        self.assertIn('config.GetBoolDefault("AiPlayerbot.DungeonRunAuraTactics", false)', CONFIG_CPP)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.DungeonRunAuraTactics = 0$")
        self.assertRegex(TEMPLATE, r"(?m)^# 0 - off: .*\(Default\)$")
        # The key is read by the same block `.reload config` runs for the other DungeonRun keys.
        reload_block = body(CONFIG_CPP, "bool PlayerbotAIConfig::ReloadDungeonRunKeys()")
        self.assertIn("LoadDungeonRunKeys();", reload_block)

    def test_the_gated_rows_read_the_key_everywhere(self):
        for text, name in ((ACTIONS, "BossTacticsActions.cpp"), (POLICY, "BossTacticsPolicy.h")):
            self.assertNotIn("dungeonRunAuraTactics = true", text, name)
        self.assertIn("sPlayerbotAIConfig.dungeonRunAuraTactics", ACTIONS)


class ChannelInterruptWiring(unittest.TestCase):
    def test_find_boss_interrupt_reads_both_slots(self):
        fn = body(ACTIONS, "BossInterruptScan ai::FindBossInterrupt(")
        self.assertIn("CurrentCastIds(unit, genericId, channeledId);", fn)
        self.assertIn("IsInterruptListed(fight.rows, unit->GetEntry(), genericId)", fn)
        self.assertIn("IsInterruptListed(fight.rows, unit->GetEntry(), channeledId)", fn)
        # The generic slot is tried first, so one id in both slots resolves the same way for every bot.
        self.assertLess(fn.index("IsInterruptListed(fight.rows, unit->GetEntry(), genericId)"),
                        fn.index("IsInterruptListed(fight.rows, unit->GetEntry(), channeledId)"))
        ids = body(ACTIONS, "void CurrentCastIds(")
        self.assertIn("unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL)", ids)
        self.assertNotIn("CURRENT_GENERIC_SPELL) == nullptr", fn)


class LinkedDeathWindowWiring(unittest.TestCase):
    def test_the_latch_takes_the_group_window(self):
        self.assertIn("float windowSec = 7.0f;", POLICY)
        self.assertIn("inline std::uint32_t LinkedDeathWindowMs(float windowSec)", POLICY)
        latch = body(POLICY, "inline bool LinkedDeathLatchTick(", "\n    }\n")
        self.assertIn("std::uint32_t windowMs", latch)
        self.assertIn("if (latch.latched && nowMs - latch.sinceMs >= windowMs)", latch)
        self.assertIn("out.windowSec = row->windowSec;", POLICY)
        self.assertIn("out.windowSec = std::max(out.windowSec, other->windowSec);", POLICY)
        scan = body(ACTIONS, "BossLinkedDeathScan ai::ScanBossLinkedDeath(")
        self.assertIn("LinkedDeathWindowMs(group.windowSec)", scan)
        # The row's own window reaches the group, never the constant directly.
        self.assertNotIn("kLinkedDeathLatchMs)", scan)


if __name__ == "__main__":
    unittest.main()
