"""Threat back-off wiring pins (ThreatMultiplier::GetValue and its policy, ai/playerbot/ThreatCapPolicy.h).

InterruptIgnoresThreat: behind AiPlayerbot.InterruptIgnoresThreat (default 0) a pure interrupt joins the heals in the
exemption the multiplier checks before its high-threat veto. A rogue at the 80 % line stood idle at full energy in 21
of 50 Kick windows (Tortoise Trials DPS-ROG-KICK 0.42). The Earth Shock and Death Coil interrupt rows are real nukes
and stay gated.

Run: python3 -m unittest tools/test_threat_cap_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
THREAT_POLICY = (ROOT / "ai/playerbot/ThreatCapPolicy.h").read_text()
STRATEGY = (ROOT / "ai/playerbot/strategy/generic/ThreatStrategy.cpp").read_text()
SPELL_ACTIONS = (ROOT / "ai/playerbot/strategy/actions/GenericSpellActions.h").read_text()
SHAMAN_ACTIONS = (ROOT / "ai/playerbot/strategy/shaman/ShamanActions.h").read_text()
WARLOCK_ACTIONS = (ROOT / "ai/playerbot/strategy/warlock/WarlockActions.h").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONF = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS = (ROOT / "docs/server/settings/combat.md").read_text()
ROGUE_DOC = (ROOT / "docs/classes/rogue.md").read_text()
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


class InterruptIgnoresThreat(unittest.TestCase):
    """AiPlayerbot.InterruptIgnoresThreat: pure interrupts join the heals in the threat back-off's exemption."""

    def test_the_key_ships_off(self):
        self.assertIn("bool interruptIgnoresThreat = false;", CONFIG_H)
        self.assertIn('config.GetBoolDefault("AiPlayerbot.InterruptIgnoresThreat", false)', CONFIG_CPP)
        self.assertIn("\nAiPlayerbot.InterruptIgnoresThreat = 0\n", CONF)
        # Reloadable with the other run keys, so a BEFORE/AFTER trial needs no restart.
        self.assertIn('config.GetBoolDefault("AiPlayerbot.InterruptIgnoresThreat", false)',
                      body(CONFIG_CPP, "void PlayerbotAIConfig::LoadDungeonRunKeys()"))

    def test_the_policy_needs_the_key(self):
        self.assertIn("inline bool ThreatCapExempt(bool heal, bool interrupt, bool interruptIgnoresThreat)",
                      THREAT_POLICY)
        self.assertIn("return heal || (interrupt && interruptIgnoresThreat);", THREAT_POLICY)

    def test_the_multiplier_exempts_interrupts_behind_the_key(self):
        multiplier = body(STRATEGY, "float ThreatMultiplier::GetValue(Action* action)")
        self.assertIn("dynamic_cast<CastInterruptSpellAction*>(action)", multiplier)
        self.assertIn("ThreatCapExempt(dynamic_cast<CastHealingSpellAction*>(action) != nullptr,", multiplier)
        self.assertIn("interruptAction != nullptr && interruptAction->IgnoresThreatCap(),", multiplier)
        self.assertIn("sPlayerbotAIConfig.interruptIgnoresThreat))", multiplier)
        self.assertIn('#include "playerbot/ThreatCapPolicy.h"', STRATEGY)
        # The exemption runs before the high-threat veto it skips (and before the AoE veto, which an interrupt never
        # reaches: it is a SINGLE-threat action).
        exempt = multiplier.index("ThreatCapExempt(")
        self.assertLess(exempt, multiplier.index('if (AI_VALUE2(bool, "trigger active", "high threat"))\n    {'))
        self.assertLess(exempt, multiplier.index("ActionThreatType::ACTION_THREAT_AOE)\n"))
        # The old heal-only exit is gone, so heals pass through the same call.
        self.assertNotIn("if (dynamic_cast<CastHealingSpellAction*>(action))\n", multiplier)

    def test_pure_interrupts_opt_in_and_nukes_stay_gated(self):
        interrupt = body(SPELL_ACTIONS, "class CastInterruptSpellAction : public CastSpellAction")
        self.assertIn("virtual bool IgnoresThreatCap() const { return true; }", interrupt)
        earth_shock = body(SHAMAN_ACTIONS, "class CastEarthShockInterruptAction : public CastInterruptSpellAction")
        self.assertIn("bool IgnoresThreatCap() const override { return false; }", earth_shock)
        death_coil = body(WARLOCK_ACTIONS, "class CastDeathCoilAction : public CastInterruptSpellAction")
        self.assertIn("bool IgnoresThreatCap() const override { return false; }", death_coil)

    def test_no_other_interrupt_overrides_the_exemption(self):
        # Kick, Pummel, Shield Bash, Bash, Counterspell and Silence keep the base answer: only the two nukes override.
        overrides = []
        for path in (ROOT / "ai/playerbot/strategy").rglob("*.h"):
            text = path.read_text(errors="replace")
            overrides += [path.name for _ in re.finditer(r"IgnoresThreatCap\(\) const override", text)]
        self.assertEqual(sorted(overrides), ["ShamanActions.h", "WarlockActions.h"])

    def test_docs_and_changelog(self):
        self.assertIn("### `AiPlayerbot.InterruptIgnoresThreat`", SETTINGS)
        self.assertIn("(#aiplayerbotinterruptignoresthreat)", SETTINGS)
        self.assertIn("InterruptIgnoresThreat", ROGUE_DOC)
        self.assertIn("InterruptIgnoresThreat", CHANGELOG)


if __name__ == "__main__":
    unittest.main()
