"""Wiring pins for the plain 80 % high-threat line and the shorter Son holds (Hakkar).

The rules are pure and tested in tools/test_threat_cap_policy.cpp (ThreatPctSaturated) and
tools/test_seek_aura_policy.cpp (the 3 s take, the 5 s burn, the 20 s hold cap). These pins prove the live code feeds
them: the high-threat trigger reads the named 80 % line on every target, the 65 % cap trigger removed by 405801e86
stays gone (the white-damage stop of AiPlayerbot.DungeonRunThreatSuccessorStrict replaces it behind its key),
ThreatValue::Calculate still saturates, the hold read feeds the hold clock, and the docs state the
shipped timings.
Run: python3 -m unittest tools/test_threat_cap_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
THREAT_POLICY = (ROOT / "ai/playerbot/ThreatCapPolicy.h").read_text()
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/GenericTriggers.cpp").read_text()
SEEK_POLICY = (ROOT / "ai/playerbot/SeekAuraPolicy.h").read_text()
SEEK_FIGHT = (ROOT / "ai/playerbot/SeekFightStatePolicy.h").read_text()
BOSS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
DOC = (ROOT / "docs/play/boss-tactics.md").read_text()
STRATEGY = (ROOT / "ai/playerbot/strategy/generic/ThreatStrategy.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONF = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
CHANGELOG = (ROOT / "CHANGELOG.md").read_text()


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

    def test_no_65_percent_cap_survives_in_ai(self):
        # The removed 65 % trigger and its line, built without spelling them: they must appear nowhere under ai/.
        self.assertNotIn("successor" + " threat cap", AI_TREE)
        self.assertNotIn("kThreatSuccessor" + "HighLinePct", AI_TREE)
        self.assertNotIn("SuccessorCap" + "Hysteresis", AI_TREE)


class ThreatSuccessorStrict(unittest.TestCase):
    """AiPlayerbot.DungeonRunThreatSuccessorStrict: no projection escape and the 92/85 % white stop on successor bosses."""

    def test_the_key_ships_off(self):
        self.assertIn("bool dungeonRunThreatSuccessorStrict = false;", CONFIG_H)
        self.assertIn('config.GetBoolDefault("AiPlayerbot.DungeonRunThreatSuccessorStrict", false)', CONFIG_CPP)
        self.assertIn("AiPlayerbot.DungeonRunThreatSuccessorStrict = 0", CONF)

    def test_the_high_threat_trigger_calls_the_escape_helper(self):
        trigger = body(TRIGGERS, "bool HighThreatTrigger::IsActive()")
        self.assertIn("const bool strict = sPlayerbotAIConfig.dungeonRunThreatSuccessorStrict;", trigger)
        self.assertIn("const bool successorTarget = strict && ThreatSuccessorTarget(target);", trigger)
        self.assertIn(
            "if (HighThreatProjectionEscapes(successorTarget, strict, currentThreat, deltaThreat, tankThreat))", trigger)
        self.assertNotIn("newThreat < tankThreat", trigger)

    def test_the_white_stop_trigger_is_keyed_and_registered(self):
        trigger = body(TRIGGERS, "bool SuccessorWhiteCapTrigger::IsActive()")
        self.assertIn("if (!sPlayerbotAIConfig.dungeonRunThreatSuccessorStrict) return false;", trigger)
        self.assertIn("ai->SuccessorWhiteCap().Update(true, PlayerbotAI::IsTank(bot), AI_VALUE2(uint8, \"threat\", \"current target\"))", trigger)
        self.assertIn('creators["successor white cap"]', (ROOT / "ai/playerbot/strategy/triggers/TriggerContext.h").read_text())
        self.assertIn('creators["stop auto attack"]', (ROOT / "ai/playerbot/strategy/actions/ActionContext.h").read_text())
        triggers = body(STRATEGY, "void ThreatStrategy::InitCombatTriggers(std::list<TriggerNode*> &triggers)")
        self.assertIn('"successor white cap"', triggers)
        self.assertIn('new NextAction("stop auto attack", ACTION_HIGH)', triggers)
        stop = body((ROOT / "ai/playerbot/strategy/actions/GenericActions.cpp").read_text(),
                    "bool StopAutoAttackAction::Execute(Event& event)")
        self.assertIn("bot->AttackStop();", stop)
        self.assertIn("bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);", stop)

    def test_the_multiplier_holds_white_damage_behind_the_key(self):
        multiplier = body(STRATEGY, "float ThreatMultiplier::GetValue(Action* action)")
        self.assertIn("if (sPlayerbotAIConfig.dungeonRunThreatSuccessorStrict && AutoAttackAction(action))", multiplier)
        self.assertIn("ai->SuccessorWhiteCap().Update(false, false, 0);", multiplier)
        # The latch runs before the old ACTION_THREAT_NONE exemption, or a wand/melee restart would slip through.
        self.assertLess(multiplier.index("AutoAttackAction(action)"),
                        multiplier.index("action->GetThreatType() == ActionThreatType::ACTION_THREAT_NONE"))
        self.assertIn("kSuccessorWhiteStopPct = 92;", THREAT_POLICY)
        self.assertIn("kSuccessorWhiteResumePct = 85;", THREAT_POLICY)

    def test_docs_and_changelog(self):
        self.assertIn("### `AiPlayerbot.DungeonRunThreatSuccessorStrict`", SETTINGS)
        self.assertIn("DungeonRunThreatSuccessorStrict", DOC)
        self.assertIn("DoT ticks", DOC)
        self.assertIn("92 % of walk 56", DOC)
        self.assertIn("DungeonRunThreatSuccessorStrict", CHANGELOG)


class ThreatValueSaturation(unittest.TestCase):
    def test_the_threat_value_returns_the_saturated_percentage(self):
        # Every threat user reads this uint8: the value itself has to saturate before the cast, or 300 % wraps to 44 %
        # and the cap lets the attack through where the margin matters most.
        values = (ROOT / "ai/playerbot/strategy/values/ThreatValues.cpp").read_text()
        calc = body(values, "uint8 ThreatValue::Calculate(Unit* target)")
        self.assertIn("return ThreatPctOf(botThreat, maxThreat, target->IsInCombat(), fleeing);", calc)
        self.assertIn("return ThreatPctSaturated(botThreat, maxThreat);", THREAT_POLICY)
        self.assertNotIn("botThreat * 100 / maxThreat", values)
        self.assertIn("inline std::uint8_t ThreatPctSaturated(float botThreat, float maxThreat)", THREAT_POLICY)


class SonHoldTimings(unittest.TestCase):
    def test_the_policy_constants(self):
        self.assertIn("constexpr std::uint32_t kSeekHealerTakeMs = 3000;", SEEK_POLICY)
        self.assertIn("constexpr std::uint32_t kSeekNonTankBurnMs = 5000;", SEEK_POLICY)
        self.assertIn("constexpr std::uint32_t kSeekHoldMaxMs = 20000;", SEEK_POLICY)
        decision = body(SEEK_POLICY, "inline SeekHoldWhy SeekHoldWhyOf(")
        self.assertIn("if (heldMs >= kSeekHoldMaxMs)", decision)
        self.assertIn("if (nonTankHeldMs >= kSeekNonTankBurnMs)", decision)
        self.assertIn("SeekHoldWhyHolds(SeekHoldWhyOf(", body(SEEK_POLICY, "inline bool SeekHoldDecision("))

    def test_the_hold_read_feeds_the_hold_clock(self):
        hold = body(BOSS, "bool SeekHoldsUnit(const SeekFight& state, Unit* unit)")
        self.assertIn("SeekHeldMs(state.holdSince, unit->GetObjectGuid().GetRawValue(), state.nowMs)", hold)
        read = body(BOSS, "bool ReadSeekFight(PlayerbotAI* ai, Player* bot, SeekFight& out)")
        self.assertIn("TickSeekHoldClock(bot, out, fightSons, record, nowMs, seekRows)", read)
        self.assertIn("SeekHoldClockTick(record.holdSince, holdSeen, nowMs,", BOSS)
        # The alive/feed composition is the policy's (SeekSonTicks), so the release can be tested without the action.
        self.assertIn("SeekSonTicks(alive, SeekHoldClockTicks(", BOSS)
        self.assertNotIn("capped ||", BOSS)
        self.assertIn("inline bool SeekSonTicks(bool seenAlive, bool ticks, bool holds)", SEEK_POLICY)
        self.assertIn("SeekHoldClockTick", SEEK_FIGHT)
        self.assertIn("SeekHeldMs", SEEK_FIGHT)

    def test_the_docs_state_the_shipped_timings(self):
        self.assertIn("for 3 s is the first off-tank's take", DOC)
        self.assertIn("kept fighting for 20 s is released to the damage dealers", DOC)


class CureIgnoresThreat(unittest.TestCase):
    """AiPlayerbot.DungeonRunCureIgnoresThreat: party cures join the heals in the threat back-off's exemption."""

    def test_the_key_ships_off(self):
        self.assertIn("bool dungeonRunCureIgnoresThreat = false;", CONFIG_H)
        self.assertIn('config.GetBoolDefault("AiPlayerbot.DungeonRunCureIgnoresThreat", false)', CONFIG_CPP)
        self.assertIn("AiPlayerbot.DungeonRunCureIgnoresThreat = 0", CONF)

    def test_the_multiplier_exempts_party_cures_behind_the_key(self):
        multiplier = body(STRATEGY, "float ThreatMultiplier::GetValue(Action* action)")
        self.assertIn("ThreatCapExempt(dynamic_cast<CastHealingSpellAction*>(action) != nullptr,", multiplier)
        self.assertIn("dynamic_cast<CurePartyMemberAction*>(action) != nullptr,", multiplier)
        self.assertIn("sPlayerbotAIConfig.dungeonRunCureIgnoresThreat))", multiplier)
        # The exemption runs before the high-threat veto it skips.
        self.assertLess(multiplier.index("ThreatCapExempt("), multiplier.index('"trigger active", "high threat"))\n    {'))
        self.assertIn("return heal || (partyCure && cureIgnoresThreat);", THREAT_POLICY)

    def test_docs_and_changelog(self):
        self.assertIn("### `AiPlayerbot.DungeonRunCureIgnoresThreat`", SETTINGS)
        self.assertIn("DungeonRunCureIgnoresThreat", DOC)
        self.assertIn("DungeonRunCureIgnoresThreat", CHANGELOG)


class ThreatSuccessorKeepsAway(unittest.TestCase):
    """AiPlayerbot.DungeonRunThreatSuccessorKeepsAway: cursed and charmed tanks stay in the successor pick."""

    VALUES = (ROOT / "ai/playerbot/strategy/values/ThreatValues.cpp").read_text()

    def test_the_key_ships_off(self):
        self.assertIn("bool dungeonRunThreatSuccessorKeepsAway = false;", CONFIG_H)
        self.assertIn('config.GetBoolDefault("AiPlayerbot.DungeonRunThreatSuccessorKeepsAway", false)', CONFIG_CPP)
        self.assertIn("AiPlayerbot.DungeonRunThreatSuccessorKeepsAway = 0", CONF)

    def test_the_pick_reads_delusions_and_the_charm_memo_behind_the_key(self):
        tank = body(self.VALUES, "float ThreatValue::GetTankThreat(PlayerbotAI* ai, Unit* target)")
        self.assertIn("const bool keepsAway = sPlayerbotAIConfig.dungeonRunThreatSuccessorKeepsAway;", tank)
        self.assertIn("if (charmed && !keepsAway)\n            continue;", tank)
        self.assertIn("threat = CharmMemoThreat(target, player, charmed, threat);", tank)
        self.assertIn("tanks.push_back(ThreatCapCharmedTank(threat, index >= 1));", tank)
        self.assertIn("keepsAway && ai->HasAura(kJindoDelusionsSpellId, player), keepsAway)", tank)
        self.assertIn("constexpr std::uint32_t kJindoDelusionsSpellId = 24306;", THREAT_POLICY)
        memo = body(self.VALUES, "float CharmMemoThreat(Unit* boss, Player* tank, bool charmed, float live)")
        self.assertIn("entry.memo.Read(charmed, live, nowMs)", memo)
        self.assertIn("boss->GetInstanceId()", memo)
        self.assertIn("g_charmMemo.erase(it)", memo)

    def test_docs_and_changelog(self):
        self.assertIn("### `AiPlayerbot.DungeonRunThreatSuccessorKeepsAway`", SETTINGS)
        self.assertIn("DungeonRunThreatSuccessorKeepsAway", DOC)
        self.assertIn("DungeonRunThreatSuccessorKeepsAway", CHANGELOG)


if __name__ == "__main__":
    unittest.main()
