"""Target facing precheck wiring (AiPlayerbot.SpellFacingPrecheck, ai/playerbot/policy/combat/TargetFacingPolicy.h).

PlayerbotAI::CheckCanCastSpell (the unit-target CanCastSpell) asks TargetFacingRefuses before it builds the probe
Spell and calls CheckCast(true): the core's CheckCast broadcasts a spell failure on every probe of a Gouge from behind
or a Backstab from the front, and a `behind` rogue probed Gouge about 20 times a second (Tortoise Trials DPS-ROG-KICK).

Run: python3 -m unittest tools/test_target_facing_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPELLS = (ROOT / "ai/playerbot/PlayerbotAISpells.cpp").read_text()
POLICY = (ROOT / "ai/playerbot/policy/combat/TargetFacingPolicy.h").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONF = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS = (ROOT / "docs/server/settings/combat.md").read_text()
CHANGELOG = (ROOT / "CHANGELOG.md").read_text()


def body(text, signature):
    """The brace-matched body that follows `signature` in `text`."""
    start = text.index(signature)
    open_brace = text.index("{", start)
    depth = 0
    for i in range(open_brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace:i + 1]
    raise AssertionError("unbalanced braces after %s" % signature)


UNIT_CHECK = body(SPELLS, "bool PlayerbotAI::CheckCanCastSpell(uint32 spellid, Unit* target,")


class SpellFacingPrecheck(unittest.TestCase):

    def test_the_key_ships_off(self):
        self.assertIn("bool spellFacingPrecheck = false;", CONFIG_H)
        self.assertIn('config.GetBoolDefault("AiPlayerbot.SpellFacingPrecheck", false)',
                      body(CONFIG_CPP, "void PlayerbotAIConfig::LoadDungeonRunKeys()"))
        self.assertIn("\nAiPlayerbot.SpellFacingPrecheck = 0\n", CONF)

    def test_the_precheck_runs_before_the_probe(self):
        self.assertIn('#include "playerbot/policy/combat/TargetFacingPolicy.h"', SPELLS)
        gate = UNIT_CHECK.index("if (sPlayerbotAIConfig.spellFacingPrecheck)")
        call = UNIT_CHECK.index(
            "ai::TargetFacingRefuses(spellInfo->Attributes, fromBehindOnly, targetFacesCaster)")
        self.assertLess(gate, call)
        self.assertLess(call, UNIT_CHECK.index("Spell *spell = new Spell(bot, spellInfo, false);"))
        self.assertLess(call, UNIT_CHECK.index("spell->CheckCast(true)"))
        self.assertEqual(UNIT_CHECK.count("ai::TargetFacingRefuses("), 1)

    def test_the_core_inputs(self):
        # The core's own tests: IsFromBehindOnlySpell, and HasInArc on the target with the default arc.
        self.assertIn("bool const fromBehindOnly = spellInfo->IsFromBehindOnlySpell();", UNIT_CHECK)
        # The arc is read only for the two facing rules (no angle math for every other spell).
        self.assertIn("bool const facingMatters = fromBehindOnly || "
                      "spellInfo->Attributes == ai::kTargetMustFaceCasterAttributes;", UNIT_CHECK)
        self.assertIn("bool const targetFacesCaster = facingMatters && target->HasInArc(bot);", UNIT_CHECK)
        self.assertIn("if (facingMatters && ai::TargetFacingRefuses(", UNIT_CHECK)
        precheck = UNIT_CHECK[UNIT_CHECK.index("if (sPlayerbotAIConfig.spellFacingPrecheck)"):
                              UNIT_CHECK.index("Spell *spell = new Spell(bot, spellInfo, false);")]
        self.assertIn(
            "*checkResult = fromBehindOnly && targetFacesCaster ? SPELL_FAILED_NOT_BEHIND : SPELL_FAILED_NOT_INFRONT;",
            precheck)
        self.assertIn("return false;", precheck)

    def test_the_policy_mirrors_the_core(self):
        self.assertIn("constexpr std::uint32_t kTargetMustFaceCasterAttributes = 0x150010;", POLICY)
        self.assertIn("(fromBehindOnly && targetFacesCaster)", POLICY)
        self.assertIn("(attributes == kTargetMustFaceCasterAttributes && !targetFacesCaster)", POLICY)

    def test_docs_and_changelog(self):
        self.assertIn("### `AiPlayerbot.SpellFacingPrecheck`", SETTINGS)
        self.assertIn("(#aiplayerbotspellfacingprecheck)", SETTINGS)
        self.assertIn("SpellFacingPrecheck", CHANGELOG)
        section = SETTINGS[SETTINGS.index("### `AiPlayerbot.SpellFacingPrecheck`"):]
        self.assertIn("runs before the core's other cast checks", section)
        self.assertIn("It can only refuse a cast that the core always refuses on facing", section)
        self.assertNotIn("Nothing else about casting changes", section)


if __name__ == "__main__":
    unittest.main()
