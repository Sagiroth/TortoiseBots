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

    def test_the_white_stop_trigger_stops_in_the_trigger_pass(self):
        trigger = body(TRIGGERS, "bool SuccessorWhiteCapTrigger::IsActive()")
        self.assertIn("if (!sPlayerbotAIConfig.dungeonRunThreatSuccessorStrict) return false;", trigger)
        self.assertIn('const bool refZero = AI_VALUE2(float, "tank threat", "current target") == 0.0f;', trigger)
        self.assertIn("if (SuccessorWhiteStopNow(ai->SuccessorWhiteCap(), true, PlayerbotAI::IsTank(bot),", trigger)
        stop = trigger[trigger.index("if (SuccessorWhiteStopNow("):]
        self.assertIn("bot->AttackStop();", stop)
        self.assertIn("bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);", stop)
        # The stop is never a queued action that a higher action could beat: the trigger is never active, no stop
        # action exists, and the node queues nothing.
        self.assertTrue(trigger.rstrip().endswith("return false;"))
        self.assertNotIn("return true", trigger)
        self.assertNotIn('"stop auto attack"', AI_TREE)
        self.assertIn('creators["successor white cap"]', (ROOT / "ai/playerbot/strategy/triggers/TriggerContext.h").read_text())
        self.assertIn("SuccessorWhiteOnTarget(bot->HasUnitState(UNIT_STAT_MELEE_ATTACKING), bot->GetVictim() == target,", trigger)
        self.assertIn("repeating && autoRepeat->m_targets.getUnitTarget() == target);", trigger)
        self.assertIn("return latch.Update(successorTarget, isTank, pct, refZero) && whiteRunning;", THREAT_POLICY)

    def test_the_trigger_is_checked_on_minimal_ticks(self):
        # successor_white_cap_runs_in_minimal_cycles (failed with the empty node, first relevance -1): a model of
        # Engine::ProcessTriggers(minimal=true) built from the live sources. The engine skips a node whose first handler's
        # relevance is under 100 on a minimal tick; the node's first handler must therefore be at 100 or more.
        import re
        engine = (ROOT / "ai/playerbot/strategy/Engine.cpp").read_text()
        process = body(engine, "void Engine::ProcessTriggers(bool minimal)")
        gate = re.search(r"if \(minimal && node->getFirstRelevance\(\) < (\d+)\)\n\s+continue;", process)
        self.assertIsNotNone(gate)
        threshold = int(gate.group(1))
        self.assertLess(process.index("getFirstRelevance()"), process.index("trigger->Check()"))
        trigger_cpp = (ROOT / "ai/playerbot/strategy/Trigger.cpp").read_text()
        self.assertIn("return handlers[0] ? handlers[0]->getRelevance() : -1;", trigger_cpp)
        strategy_h = (ROOT / "ai/playerbot/strategy/Strategy.h").read_text()
        priorities = {name: int(value) for name, value in re.findall(r"(ACTION_[A-Z_]+) = (\d+)", strategy_h)}
        triggers = body(STRATEGY, "void ThreatStrategy::InitCombatTriggers(std::list<TriggerNode*> &triggers)")
        node = re.search(r'new TriggerNode\("successor white cap",\s*NextAction::array\(0, new NextAction\("([^"]+)", '
                         r'(ACTION_[A-Z_]+)(?: \+ (\d+))?\), NULL\)\)', triggers)
        self.assertIsNotNone(node, "the node needs a first handler, or its relevance is -1")
        relevance = priorities[node.group(2)] + int(node.group(3) or 0)

        def checked(minimal, first_relevance):
            return not (minimal and first_relevance < threshold)

        self.assertFalse(checked(True, -1))           # the old empty node: skipped on a minimal tick
        self.assertTrue(checked(True, relevance))     # now checked on a minimal tick
        self.assertTrue(checked(False, relevance))
        # The handler can never take an action slot: the trigger never fires, and the action is never useful.
        self.assertEqual(node.group(1), "successor white cap noop")
        actions_h = (ROOT / "ai/playerbot/strategy/actions/GenericActions.h").read_text()
        noop = actions_h[actions_h.index("class SuccessorWhiteCapNoopAction"):]
        noop = noop[:noop.index("};")]
        self.assertIn("virtual bool isUseful() override { return false; }", noop)
        self.assertIn('creators["successor white cap noop"]', (ROOT / "ai/playerbot/strategy/actions/ActionContext.h").read_text())

    def test_the_threat_cap_row_carries_the_white_field_under_the_key(self):
        values = (ROOT / "ai/playerbot/strategy/values/ThreatValues.cpp").read_text()
        log = body(values, "void LogThreatCap(PlayerbotAI* ai, Player* bot, const ThreatCapPick& pick, Unit* offTankTarget)")
        self.assertIn("if (sPlayerbotAIConfig.dungeonRunThreatSuccessorStrict)", log)
        self.assertIn("holder=%s white=%s ref0=%d", log)
        self.assertIn('ai->SuccessorWhiteCap().stopped ? "held" : "free"', log)
        self.assertIn("pick.threat == 0.0f ? 1 : 0", log)
        self.assertIn("if (last != 0 && nowMs - last < kThreatCapLogGapMs)", log)

    def test_the_multiplier_holds_white_damage_behind_the_key(self):
        multiplier = body(STRATEGY, "float ThreatMultiplier::GetValue(Action* action)")
        self.assertIn("if (sPlayerbotAIConfig.dungeonRunThreatSuccessorStrict && AutoAttackAction(action))", multiplier)
        self.assertIn("ai->SuccessorWhiteCap().Update(false, false, 0);", multiplier)
        self.assertIn('AI_VALUE2(float, "tank threat", "current target") == 0.0f))', multiplier)
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

    def test_the_pick_marks_cursed_tanks_behind_the_key(self):
        # The list is built by the shared helper (walk 57 F4 review M3), which GetTankThreat calls before the pick.
        caller = body(self.VALUES, "float ThreatValue::GetTankThreat(PlayerbotAI* ai, Unit* target)")
        self.assertIn("ai::ThreatCapTanksOf(ai, target, roles, ai->GetBot(), nullptr);", caller)
        self.assertLess(caller.index("ThreatCapTanksOf("), caller.index("ThreatCapPickReference("))
        tank = body(self.VALUES, "std::vector<ThreatCapTank> ThreatCapTanksOf(PlayerbotAI* ai, Unit* target,")
        self.assertIn("const bool keepsAway = sPlayerbotAIConfig.dungeonRunThreatSuccessorKeepsAway;", tank)
        self.assertIn("tank.cursed = keepsAway && ai->HasAura(kJindoDelusionsSpellId, player);", tank)
        self.assertIn("if (keepsAway)\n        ThreatCapCursedOnBoss(tanks);", tank)
        self.assertLess(tank.index("ThreatCapCursedOnBoss(tanks);"), tank.index("return tanks;"))
        self.assertIn("constexpr std::uint32_t kJindoDelusionsSpellId = 24306;", THREAT_POLICY)
        self.assertIn("return threat > 0.0f && threat >= highest * kThreatCapOffTankAwayShare;", THREAT_POLICY)

    def test_a_charmed_tank_is_still_skipped(self):
        # The charm half was dropped (round-1 review): a charmed tank's old threat would lift the cap on Hakkar.
        tank = body(self.VALUES, "std::vector<ThreatCapTank> ThreatCapTanksOf(PlayerbotAI* ai, Unit* target,")
        self.assertIn("!ai->IsSafe(player) || player == skip", tank)
        self.assertIn("if (!charmer.IsEmpty() && !charmer.IsPlayer())\n            continue;", tank)
        self.assertNotIn("CharmThreat" + "Memo", AI_TREE)
        self.assertNotIn("CharmMemo" + "Threat", AI_TREE)

    def test_docs_and_changelog(self):
        self.assertIn("### `AiPlayerbot.DungeonRunThreatSuccessorKeepsAway`", SETTINGS)
        self.assertIn("DungeonRunThreatSuccessorKeepsAway", DOC)
        self.assertIn("DungeonRunThreatSuccessorKeepsAway", CHANGELOG)


if __name__ == "__main__":
    unittest.main()
