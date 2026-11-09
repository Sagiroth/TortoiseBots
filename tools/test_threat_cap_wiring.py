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

    def test_the_policy_needs_the_key_and_the_pull_line(self):
        self.assertIn(
            "inline bool ThreatCapInterruptExempt(bool interrupt, bool interruptIgnoresThreat, float botThreat, "
            "float tankThreat,\n                                         float flatThreat, bool ranged)",
            THREAT_POLICY)
        self.assertIn("if (!interrupt || !interruptIgnoresThreat || tankThreat <= 0.0f)\n            return false;",
                      THREAT_POLICY)
        self.assertIn("return botThreat + flatThreat < AggroPullFactor(ranged) * tankThreat;", THREAT_POLICY)
        # The core's 110 % / 130 % rule (ThreatManager::getHostileTarget).
        self.assertIn("constexpr float kAggroPullMeleeFactor = 1.1f;", THREAT_POLICY)
        self.assertIn("constexpr float kAggroPullRangedFactor = 1.3f;", THREAT_POLICY)
        self.assertIn("return ranged ? kAggroPullRangedFactor : kAggroPullMeleeFactor;", THREAT_POLICY)
        # No ThreatCapExempt here: the cure branches own a 3-bool ThreatCapExempt(heal, partyCure, cureIgnoresThreat).
        self.assertNotIn("ThreatCapExempt(", THREAT_POLICY)
        self.assertNotIn("kThreatCapInterruptCeilingPct", THREAT_POLICY)

    def test_the_multiplier_exempts_interrupts_behind_the_key(self):
        multiplier = body(STRATEGY, "float ThreatMultiplier::GetValue(Action* action)")
        self.assertIn("bool const heal = dynamic_cast<CastHealingSpellAction*>(action) != nullptr;", multiplier)
        self.assertIn("dynamic_cast<CastInterruptSpellAction*>(action)", multiplier)
        self.assertIn("bool const interrupt = interruptAction != nullptr && interruptAction->IgnoresThreatCap();",
                      multiplier)
        self.assertIn(
            "if (heal || ThreatCapInterruptExempt(interrupt, sPlayerbotAIConfig.interruptIgnoresThreat, "
            "interruptBotThreat,\n                                         interruptTankThreat, interruptFlatThreat, "
            "interruptRanged))\n        return 1.0f;", multiplier)
        self.assertIn('#include "playerbot/ThreatCapPolicy.h"', STRATEGY)
        # The exemption runs before the high-threat veto it skips (and before the AoE veto, which an interrupt never
        # reaches: it is a SINGLE-threat action).
        exempt = multiplier.index("ThreatCapInterruptExempt(")
        self.assertLess(exempt, multiplier.index('if (AI_VALUE2(bool, "trigger active", "high threat"))\n    {'))
        self.assertLess(exempt, multiplier.index("ActionThreatType::ACTION_THREAT_AOE)\n"))
        # The old heal-only exit is gone, so heals pass through the same call.
        self.assertNotIn("if (dynamic_cast<CastHealingSpellAction*>(action))\n", multiplier)

    def test_the_numbers_are_read_on_the_interrupt_target(self):
        multiplier = body(STRATEGY, "float ThreatMultiplier::GetValue(Action* action)")
        inputs = multiplier[multiplier.index("if (interruptKey)\n    {"):
                            multiplier.index("if (heal || ThreatCapInterruptExempt(")]
        self.assertIn("bool const interruptKey = interrupt && sPlayerbotAIConfig.interruptIgnoresThreat;", multiplier)
        self.assertIn("Unit* const interruptTarget = action->GetTarget();", inputs)
        self.assertIn("interruptBotThreat = ThreatValue::GetThreat(bot, interruptTarget);", inputs)
        self.assertIn("interruptTankThreat = ThreatValue::GetTankThreat(ai, interruptTarget);", inputs)
        # The flat threat is the core's spell threat table entry for the interrupt's spell id, 0 without one.
        self.assertIn(
            "if (SpellThreatEntry const* threatEntry = sSpellMgr.GetSpellThreatEntry(interruptAction->GetSpellID()))"
            "\n            interruptFlatThreat = threatEntry->threat;", inputs)
        # Melee or ranged by the core's own test: can the mob reach the bot with a melee swing.
        self.assertIn("interruptRanged = !interruptTarget || !interruptTarget->CanReachWithMeleeAutoAttack(bot);",
                      inputs)
        # Not the current target's numbers: those belong to the high-threat trigger.
        self.assertNotIn("current target", inputs)
        self.assertNotIn('"threat"', inputs)

    def test_a_veto_logs_its_numbers(self):
        multiplier = body(STRATEGY, "float ThreatMultiplier::GetValue(Action* action)")
        veto = body(multiplier, 'if (AI_VALUE2(bool, "trigger active", "high threat"))\n    {')
        self.assertIn("if (interruptKey)\n            LogInterruptVeto(ai, bot, interruptAction, interruptBotThreat, "
                      "interruptTankThreat, interruptFlatThreat,\n                             interruptRanged);"
                      "\n        return 0.0f;", veto)
        log = body(STRATEGY, "void LogInterruptVeto(")
        self.assertIn('"ThreatCap interrupt spell=%s pct=%.0f flat=%.0f line=%.2f bot=%.0f tank=%.0f"', log)
        self.assertIn("ai::botdiag::BotActionLog::Write(ai, \"THREAT\", \"%s\", info);", log)
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "ThreatCapInterrupt", info, "");', log)
        self.assertIn("constexpr uint32 kInterruptVetoLogGapMs = 1000;", STRATEGY)

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
        section = SETTINGS[SETTINGS.index("### `AiPlayerbot.InterruptIgnoresThreat`"):]
        self.assertIn("110 %", section)
        self.assertIn("130 %", section)
        self.assertIn("ThreatCapInterrupt", section)
        self.assertIn("on enemy healer", SETTINGS[SETTINGS.index("### `AiPlayerbot.InterruptIgnoresThreat`"):])


if __name__ == "__main__":
    unittest.main()
