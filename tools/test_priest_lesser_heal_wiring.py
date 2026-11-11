"""A priest that knows Heal rank 2+ picks it over Lesser Heal on its own.

The suppression is a multiplier, not an action gate: it only shapes
automatic selection while the bot knows Heal rank 2+ (level 22: 2055, 6063,
6064) and has the mana for the rank the "heal" action probes (the top known
rank; a busy Heal does not let Lesser Heal through). Explicit `do lesser heal` orders
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

    def test_multiplier_prices_the_rank_the_heal_action_probes(self):
        source = (PRIEST / "PriestMultipliers.cpp").read_text()
        fn = source[source.index("bool ai::PriestShouldSuppressLesserHeal"):source.index("float PriestLesserHealMultiplier")]
        for rank in ("2055", "6063", "6064"):
            self.assertIn(rank, source)
        self.assertNotIn("2054", source)
        # Mana against the cost of the spell "heal" resolves to (the top known rank), never CanCastSpell("heal"), which
        # also fails on a cast in progress or a cooldown (walk 55: Durgrim cast Lesser Heal with Heal known).
        self.assertNotIn("->CanCastSpell(", source)
        self.assertIn('GetValue<uint32>("spell id", "heal")', fn)
        self.assertIn("LookupSpellInfo(probedId)", fn)
        self.assertIn("Spell::CalculatePowerCost(spellInfo, bot, nullptr, nullptr)", fn)
        self.assertIn("PriestSuppressLesserHeal(true, PriestProbedHealAffordable(cost, bot->GetPower(POWER_MANA)))", fn)
        # The priced spell is the probed one: no per-rank loop prices a cheaper rank (review H1).
        self.assertNotIn("PriestAnyHealRankAffordable", source)
        self.assertNotIn("costs[", source)
        self.assertIn('"lesser heal"', source)
        self.assertIn('"lesser heal on party"', source)

    def test_the_heal_action_probes_the_spell_id_the_multiplier_prices(self):
        # "heal on party" clears the rank override and probes CanCastSpell(spell name), which resolves
        # "spell id" + "heal": the same value the multiplier reads.
        actions = (ROOT / "ai/playerbot/strategy/actions/GenericSpellActions.cpp").read_text()
        self.assertIn("ClearSpellIdOverride()", actions)
        spells = (ROOT / "ai/playerbot/PlayerbotAISpells.cpp").read_text()
        self.assertIn("CanCastSpell(aiObjectContext->GetValue<uint32>(\"spell id\", name)->Get(), target", spells)

    def test_wand_probe_uses_shared_helper(self):
        probe = body("PriestTriggers.cpp", "bestHeal = BetterWandSpellProbe")
        self.assertIn("PriestSlowHealProbeSpell(PriestShouldSuppressLesserHeal(ai))", probe)


if __name__ == "__main__":
    unittest.main()
