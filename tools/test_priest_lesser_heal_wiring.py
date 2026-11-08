"""A priest that knows Heal rank 2+ picks it over Lesser Heal on its own.

The suppression is a multiplier, not an action gate: it only shapes
automatic selection while the bot knows Heal rank 2+ (level 22: 2055, 6063,
6064) and has the mana for at least one known rank (a busy Heal does not
let Lesser Heal through). Explicit `do lesser heal` orders
and low-mana fallbacks still use Lesser Heal. The wand probe picks the same
spell the multiplier would allow through a shared helper.

Run: python3 -m unittest tools/test_priest_lesser_heal_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PRIEST = ROOT / "ai/playerbot/strategy/priest"


def body(path, start, end="\n}\n"):
    text = (PRIEST / path).read_text()
    section = text[text.index(start):]
    return section[:section.index(end) + len(end)]


class PriestLesserHealWiringTest(unittest.TestCase):
    def test_macros_back_without_isuseful_override(self):
        actions = (PRIEST / "PriestActions.h").read_text()
        self.assertIn("HEAL_ACTION(CastLesserHealAction", actions)
        self.assertIn("HEAL_PARTY_ACTION_E(CastLesserHealOnPartyAction", actions)
        for cls in ("class CastLesserHealAction", "class CastLesserHealOnPartyAction"):
            self.assertNotIn(cls, actions, cls)

    def test_multiplier_registered_on_both_engines(self):
        for function in ("void PriestStrategy::InitCombatMultipliers",
                         "void PriestStrategy::InitNonCombatMultipliers"):
            section = body("PriestStrategy.cpp", function)
            self.assertIn("new PriestLesserHealMultiplier(ai)", section, function)
        self.assertIn("PriestLesserHealMultiplier", (PRIEST / "PriestMultipliers.h").read_text())

    def test_multiplier_checks_rank2_and_affordability(self):
        source = (PRIEST / "PriestMultipliers.cpp").read_text()
        for rank in ("2055", "6063", "6064"):
            self.assertIn(rank, source)
        self.assertNotIn("2054", source)
        # Mana against the cost of each known Heal rank 2+ (the core's price), never CanCastSpell("heal"), which also
        # fails on a cast in progress or a cooldown (walk 55: Durgrim cast Lesser Heal with Heal known).
        self.assertNotIn("CanCastSpell", source)
        self.assertIn("Spell::CalculatePowerCost(spellInfo, bot, nullptr, nullptr)", source)
        self.assertIn("PriestAnyHealRankAffordable(costs, known, bot->GetPower(POWER_MANA))", source)
        self.assertIn("PriestSuppressLesserHeal(true, PriestAnyHealRankAffordable(", source)
        self.assertIn("if (!bot->HasSpell(spellId))", source)
        self.assertIn("PriestSuppressLesserHeal", source)
        self.assertIn('"lesser heal"', source)
        self.assertIn('"lesser heal on party"', source)

    def test_wand_probe_uses_shared_helper(self):
        probe = body("PriestTriggers.cpp", "bestHeal = BetterWandSpellProbe")
        self.assertIn("PriestShouldSuppressLesserHeal(ai)", probe)
        self.assertIn('"heal"', probe)


if __name__ == "__main__":
    unittest.main()
