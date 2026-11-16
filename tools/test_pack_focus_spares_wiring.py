"""The run leader's pack-focus spare rule reaches the game.

tools/test_dungeon_run_policy.cpp tests the policy itself; this file pins the plumbing: the key is read in the
reloadable loader and shipped off in the template, and commands/Commander.cpp hands the policy the facts bit that
PickPackFocus reads before it gives a pulled pack the fallback skull.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
COMMANDS = (ROOT / "commands/Commander.cpp").read_text()
KEY = "AiPlayerbot.DungeonRunPackFocusSparesTacticUnits"
MEMBER = "dungeonRunPackFocusSparesTacticUnits"
FACT = "packFocusSparesTacticUnits"


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class PackFocusSparesWiringTest(unittest.TestCase):
    def test_key_is_read_in_the_reloadable_loader(self):
        config = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
        header = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
        keys = body(config, "void PlayerbotAIConfig::LoadDungeonRunKeys()\n{", "\n}\n")
        self.assertIn('%s =\n        config.GetBoolDefault("%s", false);' % (MEMBER, KEY), keys)
        self.assertIn("bool %s = false;" % MEMBER, header)

    def test_the_template_ships_it_off(self):
        template = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
        self.assertIn("\n%s = 0\n" % KEY, template)
        self.assertIn("Recommended on.", template[template.index(KEY) - 900:template.index("%s = 0" % KEY)])

    def test_the_glue_hands_the_policy_the_key(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertIn("f.%s = sPlayerbotAIConfig.%s;" % (FACT, MEMBER), facts)
        self.assertIn("bool %s = false;" % FACT, POLICY)

    def test_pick_pack_focus_spares_the_tactic_units(self):
        pick = body(POLICY, "inline const UnitFact* PickPackFocus(const Run& r, const Facts& f)", "\n}\n")
        self.assertIn("bool const sparesTacticUnits = f.%s;" % FACT, pick)
        self.assertIn("sparesTacticUnits && PackFocusSparesEntry(r, tacticKeys, u.entry)", pick)
        spare = body(POLICY, "inline bool PackFocusSparesEntry(", "\n}\n")
        # (a) a unit whose entry has its own live rows, (b) one a live row names as its target.
        self.assertIn("const std::vector<ai::BossTacticRow>* const own = r.liveTactics->Rows(r.plan->map, entry);", spare)
        self.assertIn("BossTacticOwnsEntryIn(r, keys, entry, true)", spare)

    def test_the_docs_carry_the_key(self):
        for doc in ("docs/server/settings/dungeons-raids.md", "docs/play/boss-tactics.md",
                    "docs/play/dungeon-run-leader.md"):
            self.assertIn(KEY, (ROOT / doc).read_text(), doc)


if __name__ == "__main__":
    unittest.main()
