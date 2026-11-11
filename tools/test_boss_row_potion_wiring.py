"""Wiring pins for the spread-row protection potion (DungeonRunBossProtectionPotion, r11-4).

The pure rules live in ai/playerbot/ProtectionPotionPolicy.h and are tested in
tools/test_protection_potion_policy.cpp. These pins hold the call sites: the entry table
answers before the row, the row read is the key's own (it does not need
DungeonRunBossSpread), the potion action is queued once by the generic boss tactics, and
Zul'Gurub's own strategy stays empty so no raid gains a second node.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY = (ROOT / "ai" / "playerbot" / "ProtectionPotionPolicy.h").read_text()
ACTIONS = (ROOT / "ai" / "playerbot" / "strategy" / "actions" / "BossTacticsActions.cpp").read_text()
USE_ITEM = (ROOT / "ai" / "playerbot" / "strategy" / "actions" / "UseItemAction.cpp").read_text()
STRATEGY = (ROOT / "ai" / "playerbot" / "strategy" / "generic" / "BossTacticsStrategy.cpp").read_text()
CLASSIC_RAIDS = (ROOT / "ai" / "playerbot" / "strategy" / "generic" / "ClassicRaidDungeonStrategies.cpp").read_text()
TRIGGER_CONTEXT = (ROOT / "ai" / "playerbot" / "strategy" / "triggers" / "TriggerContext.h").read_text()
CONFIG_H = (ROOT / "ai" / "playerbot" / "PlayerbotAIConfig.h").read_text()
CONFIG_CPP = (ROOT / "ai" / "playerbot" / "PlayerbotAIConfig.cpp").read_text()
CONF = (ROOT / "ai" / "playerbot" / "aiplayerbot.conf.dist.in").read_text()
SETTINGS = (ROOT / "docs" / "server" / "settings" / "dungeons-raids.md").read_text()
BOSS_TACTICS = (ROOT / "docs" / "play" / "boss-tactics.md").read_text()
DUNGEON_TRIGGERS = (ROOT / "ai" / "playerbot" / "strategy" / "triggers" / "DungeonTriggers.cpp").read_text()

# The key does nothing without the boss-tactics strategy, which the shipped default DungeonTactics = 0 leaves off
# outside a commander-bound group. One sentence, carried by the template, the settings doc and the boss-tactics doc.
DEPENDENCY = ("It works only for bots that run the boss tactics: `AiPlayerbot.DungeonTactics = 1` "
              "or a commander-bound run group.")


def conf_prose(text):
    """The template's comment lines without their `# ` prefixes, so a wrapped sentence reads as one line."""
    lines = []
    for line in text.splitlines():
        if line.startswith("# "):
            lines.append(line[2:])
        elif line.startswith("#"):
            lines.append(line[1:].lstrip())
        else:
            lines.append("")
    return " ".join(lines)


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


class BossRowPotionWiring(unittest.TestCase):
    def test_the_entry_table_answers_before_the_row(self):
        fn = body(USE_ITEM, "uint32 ai::FindBossProtectionPotion(PlayerbotAI* ai, uint32* buffSpellId)")
        self.assertIn("BossProtectionPotion(", fn)
        self.assertIn("BossRowProtectionPotion(", fn)
        self.assertLess(fn.index("BossProtectionPotion("), fn.index("BossRowProtectionPotion("))
        # The row is asked only after the attacker loop has found nothing.
        self.assertLess(fn.index("for (const ObjectGuid& attackerGuid : attackers)"),
                        fn.index("BossRowProtectionPotion("))

    def test_the_row_read_is_the_potion_keys_own(self):
        fn = body(ACTIONS, "ProtectionPotion ai::BossRowProtectionPotion(PlayerbotAI* ai, Player* bot)")
        self.assertIn("dungeonRunBossProtectionPotion", fn)
        self.assertNotIn("dungeonRunBossSpread)", fn)
        self.assertIn("BossTacticKind::Spread", fn)
        self.assertIn("HasSpreadMap(", fn)
        # The school comes from the row's spell, the role mask from its priority.
        self.assertIn("LookupSpellInfo(row->spellId)", fn)
        self.assertIn("SpreadRowPotion(", fn)
        self.assertIn("row->priority", fn)

    def test_the_trigger_is_the_keys_own_and_stays_due(self):
        fn = body(DUNGEON_TRIGGERS, "bool BossRowProtectionPotionReadyTrigger::IsActive()")
        self.assertIn("dungeonRunBossProtectionPotion", fn)
        self.assertIn("RowPotionDue(", fn)
        self.assertIn("ItemReadyTrigger::IsActive()", fn)
        self.assertIn('creators["boss row protection potion ready"]', TRIGGER_CONTEXT)

    def test_the_combat_strategy_queues_one_node(self):
        nodes = re.findall(r'new TriggerNode\(\s*"boss row protection potion ready"', STRATEGY)
        self.assertEqual(1, len(nodes), "the row potion node must be queued exactly once")
        node = STRATEGY[STRATEGY.index('"boss row protection potion ready"'):]
        self.assertIn('new NextAction("boss protection potion"', node[:200])
        # Combat only: the non-combat and reaction blocks never queue it.
        non_combat = STRATEGY[STRATEGY.index("void BossTacticsStrategy::InitNonCombatTriggers"):]
        self.assertNotIn('"boss row protection potion ready"', non_combat)

    def test_zulgurub_keeps_an_empty_strategy(self):
        fn = body(CLASSIC_RAIDS, "void ZulgurubDungeonStrategy::InitCombatTriggers(")
        self.assertNotIn("boss row protection potion ready", fn)
        self.assertNotIn("boss protection potion", fn)

    def test_the_key_ships_off_everywhere(self):
        self.assertIn("bool dungeonRunBossProtectionPotion = false;", CONFIG_H)
        self.assertIn('dungeonRunBossProtectionPotion = config.GetBoolDefault("AiPlayerbot.DungeonRunBossProtectionPotion", '
                      'false);', CONFIG_CPP)
        self.assertIn("AiPlayerbot.DungeonRunBossProtectionPotion = 0", CONF)
        self.assertIn("[`AiPlayerbot.DungeonRunBossProtectionPotion`](#aiplayerbotdungeonrunbossprotectionpotion) | `0`",
                      SETTINGS)
        self.assertIn("### `AiPlayerbot.DungeonRunBossProtectionPotion`", SETTINGS)
        # Key 0 keeps today's behaviour, so the section may not claim it is the recommended setting.
        section = SETTINGS[SETTINGS.index("### `AiPlayerbot.DungeonRunBossProtectionPotion`"):]
        section = section[:section.index("### ", 5)]
        self.assertNotIn("recommended on", section)

    def test_the_boss_tactics_dependency_is_stated(self):
        # The trigger is installed by the boss-tactics strategy and BossRowProtectionPotion refuses a bot without it,
        # so enabling this key alone does nothing under the shipped DungeonTactics = 0. Every place an operator reads
        # the key has to say that.
        self.assertIn(DEPENDENCY, conf_prose(CONF))
        self.assertIn(DEPENDENCY, SETTINGS)
        self.assertIn(DEPENDENCY, BOSS_TACTICS)

    def test_the_policy_names_no_boss(self):
        self.assertNotIn("14834", POLICY)
        self.assertNotIn("24328", POLICY)
        self.assertIn("PotionRoleBit(", POLICY)
        self.assertIn("ProtectionPotionForSchool(", POLICY)
        self.assertIn("RowPotionDue(", POLICY)


if __name__ == "__main__":
    unittest.main()
