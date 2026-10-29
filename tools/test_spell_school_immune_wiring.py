"""The damage rotation drops a spell of a school the creature template is immune to.

A creature template's school_immune_mask carries one bit per SpellEntry school
(mask 4 is fire immunity), so a fire-immune creature takes nothing from a fire
spell; the core applies that in Creature::IsImmuneToSpell.
runtime/SpellSchoolImmunePolicy.h holds the pure rule with its own standalone
test (tools/test_spell_school_immune_policy.cpp), but nothing pinned the wiring:
deleting the call from CastSpellAction::isUseful broke no test while the
rotation went back to spending casts (and mana) on the immunity. This test pins
the include, the call inside isUseful, and its guards: a creature target other
than the bot itself (GetCreatureInfo, so never a player), and a non-positive
spell only.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPELL_ACTIONS = (ROOT / "ai/playerbot/strategy/actions/GenericSpellActions.cpp").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


CODE = strip_comments(SPELL_ACTIONS)
USEFUL = body(CODE, "bool CastSpellAction::isUseful()")


def immune_block():
    # The guard block around the helper call, through the cast it drops.
    i = USEFUL.index("TortoiseBots::SpellSchoolImmune(")
    start = USEFUL.rindex("if (spellTarget != bot", 0, i)
    return USEFUL[start:USEFUL.index("return false;", i) + len("return false;")]


class SpellSchoolImmuneWiringTest(unittest.TestCase):
    def test_is_useful_includes_the_policy(self):
        self.assertIn('#include "../../../../runtime/SpellSchoolImmunePolicy.h"', CODE)

    def test_the_school_mask_is_judged_against_the_template_mask(self):
        self.assertRegex(USEFUL,
                         r"TortoiseBots::SpellSchoolImmune\(static_cast<uint32>\(info->GetSpellSchoolMask\(\)\),\s*"
                         r"creatureInfo->school_immune_mask\)")
        self.assertIn("return false;", immune_block())

    def test_only_a_creature_target(self):
        block = immune_block()
        self.assertIn("spellTarget != bot && spellTarget->IsCreature()", block)
        self.assertIn("static_cast<Creature*>(spellTarget)->GetCreatureInfo()", block)

    def test_only_a_non_positive_spell(self):
        block = immune_block()
        self.assertIn("!info->IsPositiveSpell()", block)
        # The guard is the same `if` condition as the helper: a spell the
        # target is immune to is skipped, not cast and logged.
        self.assertRegex(block,
                         r"if \(creatureInfo && !info->IsPositiveSpell\(\) &&\s*"
                         r"TortoiseBots::SpellSchoolImmune\(")


if __name__ == "__main__":
    unittest.main()
