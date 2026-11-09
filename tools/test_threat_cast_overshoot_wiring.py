"""Threat back-off wiring pins for AiPlayerbot.ThreatCapCountsCastThreat (ThreatMultiplier::GetValue).

The high-threat veto reads only the bot's threat at cast start (80 % line). A Fireball started at 79 % landed about 3 s
later as one 400-690 threat hit and carried the mage past the core's 130 % ranged pull line (Tortoise Trials, non-fire
mages: 48 of 87 aggro onsets within 5 s of a Fireball hit). Behind the key (default 0) a single-target damage cast is
also held while the bot's threat plus the cast's expected threat reaches the tank's (ThreatCapCastOvershoots).

Run: python3 -m unittest tools/test_threat_cast_overshoot_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
THREAT_POLICY = (ROOT / "ai/playerbot/ThreatCapPolicy.h").read_text()
STRATEGY = (ROOT / "ai/playerbot/strategy/generic/ThreatStrategy.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONF = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS = (ROOT / "docs/server/settings/combat.md").read_text()
LOGGING = (ROOT / "docs/server/settings/logging.md").read_text()
CHANGELOG = (ROOT / "CHANGELOG.md").read_text()

CAST_CAP_IF = ("if (sPlayerbotAIConfig.threatCapCountsCastThreat &&\n"
               "        action->GetThreatType() == ActionThreatType::ACTION_THREAT_SINGLE && !PlayerbotAI::IsTank(bot))")
HIGH_THREAT_VETO = 'if (AI_VALUE2(bool, "trigger active", "high threat"))\n    {'


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


def multiplier():
    return body(STRATEGY, "float ThreatMultiplier::GetValue(Action* action)")


class CastThreatCap(unittest.TestCase):
    """AiPlayerbot.ThreatCapCountsCastThreat: the cast's own expected threat counts against the tank's."""

    def test_the_key_ships_off(self):
        self.assertIn("bool threatCapCountsCastThreat = false;", CONFIG_H)
        self.assertIn("\nAiPlayerbot.ThreatCapCountsCastThreat = 0\n", CONF)
        # Reloadable with the run keys, so a BEFORE/AFTER trial needs no restart.
        self.assertIn('config.GetBoolDefault("AiPlayerbot.ThreatCapCountsCastThreat", false)',
                      body(CONFIG_CPP, "void PlayerbotAIConfig::LoadDungeonRunKeys()"))

    def test_the_policy_line_is_the_tank_and_needs_the_key(self):
        self.assertIn("inline bool ThreatCapCastOvershoots(bool key, float botThreat, float castThreat, float tankThreat)",
                      THREAT_POLICY)
        self.assertIn("return key && tankThreat > 0.0f && castThreat > 0.0f && botThreat + castThreat >= tankThreat;",
                      THREAT_POLICY)

    def test_the_key_is_read_first(self):
        m = multiplier()
        self.assertIn(CAST_CAP_IF, m)
        block = body(m, CAST_CAP_IF)
        # Nothing is computed before the key: the key is the first operand of the guard, and no threat read or
        # estimate sits between the previous exit and the guard.
        self.assertTrue(CAST_CAP_IF.startswith("if (sPlayerbotAIConfig.threatCapCountsCastThreat &&"))
        before = m[:m.index(CAST_CAP_IF)]
        self.assertNotIn("ExpectedCastThreat(", before)
        self.assertNotIn("ThreatCapCastOvershoots(", before)
        self.assertIn("ExpectedCastThreat(bot, castTarget, castAction->GetSpellID())", block)

    def test_after_the_exemptions_and_before_the_high_threat_veto(self):
        m = multiplier()
        cap = m.index(CAST_CAP_IF)
        # After the heal exemption, the Fankriss burn, the neglect-threat exit and the group check ...
        self.assertGreater(cap, m.index("dynamic_cast<CastHealingSpellAction*>(action)"))
        self.assertGreater(cap, m.index("FankrissBurnSpawn(ai, action->GetTarget())"))
        self.assertGreater(cap, m.index('AI_VALUE(bool, "neglect threat")'))
        self.assertGreater(cap, m.index('if (!AI_VALUE(bool, "group"))\n        return 1.0f;'))
        # ... the interrupt exemption too, when the base carries it (fix/interrupt-threat).
        if "ThreatCapInterruptExempt(" in m:
            self.assertGreater(cap, m.index("ThreatCapInterruptExempt("))
        # ... and before the high-threat veto it adds to.
        self.assertLess(cap, m.index(HIGH_THREAT_VETO))

    def test_measured_on_the_action_target_held_by_a_tank(self):
        block = body(multiplier(), CAST_CAP_IF)
        self.assertIn("dynamic_cast<CastSpellAction*>(action)", block)
        self.assertIn("Unit* const castTarget = action->GetTarget();", block)
        self.assertIn("PlayerbotAI::IsTank(castVictimPlayer)", block)
        self.assertIn("float const castTankThreat = ThreatValue::GetTankThreat(ai, castTarget);", block)
        self.assertIn("float const castBotThreat = ThreatValue::GetThreat(bot, castTarget);", block)
        self.assertIn("ThreatCapCastOvershoots(sPlayerbotAIConfig.threatCapCountsCastThreat, castBotThreat, castThreat,\n"
                      "                                            castTankThreat))", block)
        self.assertNotIn("current target", block)

    def test_the_estimate_is_the_average_with_threat_modifiers(self):
        est = body(STRATEGY, "float ExpectedCastThreat(Player* bot, Unit* target, uint32 spellId)")
        self.assertIn("SPELL_EFFECT_SCHOOL_DAMAGE", est)
        self.assertIn("ThreatCapAverageEffectPoints(", est)
        self.assertIn("SpellDamageBonusDone(", est)
        self.assertIn("SPELLMOD_THREAT", est)
        self.assertIn("return bot->ApplyTotalThreatModifier(damage, info->GetSpellSchoolMask());", est)
        # No rolled value (CalculateSpellDamage rolls the dice) and no Spell passed, so no spell mod charge is used.
        self.assertNotIn("CalculateSpellDamage(", est)
        self.assertNotIn("irand(", est)

    def test_a_veto_logs_its_numbers(self):
        block = body(multiplier(), CAST_CAP_IF)
        self.assertIn("LogCastVeto(ai, bot, castAction, castBotThreat, castThreat, castTankThreat);\n"
                      "                    return 0.0f;", block)
        log = body(STRATEGY, "void LogCastVeto(")
        self.assertIn('"ThreatCap cast spell=%s pct=%.0f cast=%.0f bot=%.0f tank=%.0f"', log)
        self.assertIn('ai::botdiag::BotActionLog::Write(ai, "THREAT", "%s", info);', log)
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "ThreatCapCast", info, "");', log)
        self.assertIn("constexpr uint32 kCastVetoLogGapMs = 2000;", STRATEGY)

    def test_docs_and_changelog(self):
        self.assertIn("### `AiPlayerbot.ThreatCapCountsCastThreat`", SETTINGS)
        self.assertIn("(#aiplayerbotthreatcapcountscastthreat)", SETTINGS)
        section = SETTINGS[SETTINGS.index("### `AiPlayerbot.ThreatCapCountsCastThreat`"):]
        self.assertIn("Recommended on", section)
        self.assertIn("ThreatCapCast", section)
        self.assertIn("ThreatCapCast", LOGGING)
        self.assertIn("Recommended on", CONF[CONF.index("# The threat back-off counts the cast's own threat."):])
        self.assertIn("ThreatCapCountsCastThreat", CHANGELOG.split("\n- ", 2)[1])


if __name__ == "__main__":
    unittest.main()
