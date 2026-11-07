"""Wiring pins for the plain 80 % high-threat line and the shorter Son holds (Hakkar).

The rules are pure and tested in tools/test_threat_cap_policy.cpp (ThreatPctSaturated) and
tools/test_seek_aura_policy.cpp (the 3 s take, the 5 s burn, the 20 s hold cap). These pins prove the live code feeds
them: the high-threat trigger reads the named 80 % line on every target, no leftover cap trigger or stop action
survives in ai/, ThreatValue::Calculate still saturates, the hold read feeds the hold clock, and the docs state the
shipped timings.
Run: python3 -m unittest tools/test_threat_cap_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
THREAT_POLICY = (ROOT / "ai/playerbot/ThreatCapPolicy.h").read_text()
THREAT_STRATEGY = (ROOT / "ai/playerbot/strategy/generic/ThreatStrategy.cpp").read_text()
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/GenericTriggers.cpp").read_text()
SEEK_POLICY = (ROOT / "ai/playerbot/SeekAuraPolicy.h").read_text()
SEEK_FIGHT = (ROOT / "ai/playerbot/SeekFightStatePolicy.h").read_text()
BOSS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
DOC = (ROOT / "docs/play/boss-tactics.md").read_text()


def ai_tree_text():
    parts = []
    for path in sorted((ROOT / "ai").rglob("*")):
        if path.is_file():
            try:
                parts.append(path.read_text(encoding="utf-8"))
            except (OSError, UnicodeDecodeError):
                continue
    return "\n".join(parts)


AI_TREE = ai_tree_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


class HighLine(unittest.TestCase):
    def test_the_line_is_a_named_constant(self):
        self.assertIn("constexpr std::uint8_t kThreatHighLinePct = 80;", THREAT_POLICY)

    def test_the_high_threat_trigger_reads_the_line_on_every_target(self):
        trigger = body(TRIGGERS, "bool HighThreatTrigger::IsActive()")
        self.assertIn("relativeThreat >= kThreatHighLinePct", trigger)

    def test_no_cap_trigger_text_survives_in_ai(self):
        # The removed trigger's creator name, built without spelling it: it must appear nowhere under ai/.
        self.assertNotIn("successor" + " threat cap", AI_TREE)

    def test_no_stop_action_survives_in_ai(self):
        # The removed action's class name, built without spelling it: it must appear nowhere under ai/.
        self.assertNotIn("Stop" + "AutoAttack" + "Action", AI_TREE)


class ThreatValueSaturation(unittest.TestCase):
    def test_the_threat_value_returns_the_saturated_percentage(self):
        # Every threat user reads this uint8: the value itself has to saturate before the cast, or 300 % wraps to 44 %
        # and the cap lets the attack through where the margin matters most.
        values = (ROOT / "ai/playerbot/strategy/values/ThreatValues.cpp").read_text()
        calc = body(values, "uint8 ThreatValue::Calculate(Unit* target)")
        self.assertIn("return ThreatPctSaturated(botThreat, maxThreat);", calc)
        self.assertNotIn("botThreat * 100 / maxThreat", values)
        self.assertIn("inline std::uint8_t ThreatPctSaturated(float botThreat, float maxThreat)", THREAT_POLICY)


class SonHoldTimings(unittest.TestCase):
    def test_the_policy_constants(self):
        self.assertIn("constexpr std::uint32_t kSeekHealerTakeMs = 3000;", SEEK_POLICY)
        self.assertIn("constexpr std::uint32_t kSeekNonTankBurnMs = 5000;", SEEK_POLICY)
        self.assertIn("constexpr std::uint32_t kSeekHoldMaxMs = 20000;", SEEK_POLICY)
        decision = body(SEEK_POLICY, "inline bool SeekHoldDecision(")
        self.assertIn("if (heldMs >= kSeekHoldMaxMs)", decision)

    def test_the_hold_read_feeds_the_hold_clock(self):
        hold = body(BOSS, "bool SeekHoldsUnit(const SeekFight& state, Unit* unit)")
        self.assertIn("SeekHeldMs(state.holdSince, unit->GetObjectGuid().GetRawValue(), state.nowMs)", hold)
        read = body(BOSS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("TickSeekHoldClock(out, fightSons, record, nowMs)", read)
        self.assertIn("SeekHoldClockTick(record.holdSince, holdSeen, nowMs)", BOSS)
        # The alive/feed composition is the policy's (SeekSonTicks), so the release can be tested without the action.
        self.assertIn("SeekSonTicks(alive, SeekHoldClockTicks(", BOSS)
        self.assertNotIn("capped ||", BOSS)
        self.assertIn("inline bool SeekSonTicks(bool seenAlive, bool ticks, bool holds)", SEEK_POLICY)
        self.assertIn("SeekHoldClockTick", SEEK_FIGHT)
        self.assertIn("SeekHeldMs", SEEK_FIGHT)

    def test_the_docs_state_the_shipped_timings(self):
        self.assertIn("for 3 s is the first off-tank's take", DOC)
        self.assertIn("kept fighting for 20 s is released to the damage dealers", DOC)


if __name__ == "__main__":
    unittest.main()
