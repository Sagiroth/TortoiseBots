"""Wiring pins for the `threat_successor` auto-attack veto and the shorter Son holds (Hakkar, walk 48).

The rules are pure and tested in tools/test_threat_cap_policy.cpp (the 65 % line, SuccessorCapStopsAutoAttack) and
tools/test_seek_aura_policy.cpp (the 3 s take, the 5 s burn, the 20 s hold cap). These pins prove the live code feeds
them: the high-threat trigger reads the named line, the threat multiplier no longer exempts auto-attack / wand / Auto
Shot, the stop action interrupts the auto-repeat, the hold read feeds the hold clock, and the docs state the shipped
timings.
Run: python3 -m unittest tools/test_threat_cap_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
THREAT_POLICY = (ROOT / "ai/playerbot/ThreatCapPolicy.h").read_text()
THREAT_STRATEGY = (ROOT / "ai/playerbot/strategy/generic/ThreatStrategy.cpp").read_text()
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/GenericTriggers.cpp").read_text()
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/GenericActions.cpp").read_text()
ACTION_CONTEXT = (ROOT / "ai/playerbot/strategy/actions/ActionContext.h").read_text()
TRIGGER_CONTEXT = (ROOT / "ai/playerbot/strategy/triggers/TriggerContext.h").read_text()
SEEK_POLICY = (ROOT / "ai/playerbot/SeekAuraPolicy.h").read_text()
SEEK_FIGHT = (ROOT / "ai/playerbot/SeekFightStatePolicy.h").read_text()
BOSS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
DOC = (ROOT / "docs/play/boss-tactics.md").read_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


class SuccessorHighLine(unittest.TestCase):
    def test_the_lines_are_named_constants(self):
        self.assertIn("constexpr std::uint8_t kThreatHighLinePct = 80;", THREAT_POLICY)
        self.assertIn("constexpr std::uint8_t kThreatSuccessorHighLinePct = 65;", THREAT_POLICY)
        self.assertIn("return successorTarget ? kThreatSuccessorHighLinePct : kThreatHighLinePct;", THREAT_POLICY)

    def test_the_high_threat_trigger_reads_the_successor_line(self):
        trigger = body(TRIGGERS, "bool HighThreatTrigger::IsActive()")
        self.assertIn("relativeThreat >= ThreatHighLinePct(ThreatSuccessorTarget(target))", trigger)

    def test_the_successor_cap_trigger_gates_on_the_row_and_the_group(self):
        trigger = body(TRIGGERS, "bool SuccessorThreatCapTrigger::IsActive()")
        self.assertIn('AI_VALUE(bool, "group")', trigger)
        self.assertIn('AI_VALUE(bool, "neglect threat")', trigger)
        # The data row decides before the percentage is read: a trash target never pays for the tank scan the threat
        # value costs, and the bot's latch is cleared so a later boss starts from the high line.
        self.assertIn("if (!ThreatSuccessorTarget(target))", trigger)
        self.assertLess(trigger.index("ThreatSuccessorTarget(target)"), trigger.index('AI_VALUE2(uint8, "threat"'))
        self.assertIn("ai->SuccessorThreatCap().Update(true, PlayerbotAI::IsTank(bot),", trigger)
        self.assertIn('creators["successor threat cap"]', TRIGGER_CONTEXT)

    def test_the_cap_state_lives_on_the_bot(self):
        ai_header = (ROOT / "ai/playerbot/PlayerbotAI.h").read_text()
        self.assertIn("#include \"playerbot/ThreatCapPolicy.h\"", ai_header)
        self.assertIn("ai::SuccessorCapHysteresis successorThreatCap_;", ai_header)
        self.assertIn("ai::SuccessorCapHysteresis& SuccessorThreatCap() { return successorThreatCap_; }", ai_header)
        self.assertIn("constexpr std::uint8_t kThreatSuccessorResumePct = 58;", THREAT_POLICY)
        self.assertIn("bool Update(bool successorTarget, bool tank, std::uint8_t threatPct)", THREAT_POLICY)


class AutoAttackVeto(unittest.TestCase):
    def test_the_multiplier_no_longer_exempts_the_three_damage_actions(self):
        self.assertIn("dynamic_cast<MeleeAction*>(action)", THREAT_STRATEGY)
        self.assertIn("dynamic_cast<CastShootAction*>(action)", THREAT_STRATEGY)
        self.assertIn('action->getName() == "auto shot"', THREAT_STRATEGY)
        veto = body(THREAT_STRATEGY, "float ThreatMultiplier::GetValue(Action* action)")
        self.assertIn("if (AutoAttackAction(action))", veto)
        # The row decides before the percentage: a trash target never reads the threat value for the cap.
        self.assertIn("if (!ThreatSuccessorTarget(target))", veto)
        self.assertLess(veto.index("ThreatSuccessorTarget(target)"), veto.index('AI_VALUE2(uint8, "threat"'))
        self.assertIn("ai->SuccessorThreatCap().Update(true, PlayerbotAI::IsTank(bot),", veto)

    def test_the_stop_action_stops_the_swing_and_the_auto_repeat(self):
        execute = body(ACTIONS, "bool StopAutoAttackAction::Execute(Event& event)")
        self.assertIn("bot->AttackStop();", execute)
        self.assertIn("bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);", execute)
        useful = body(ACTIONS, "bool StopAutoAttackAction::isUseful()")
        self.assertIn("bot->HasUnitState(UNIT_STAT_MELEE_ATTACKING)", useful)
        self.assertIn("bot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL)", useful)
        self.assertIn('creators["stop auto attack"]', ACTION_CONTEXT)

    def test_the_threat_strategy_carries_the_stop_row(self):
        add = body(THREAT_STRATEGY, "void ThreatStrategy::InitCombatTriggers(std::list<TriggerNode*> &triggers)")
        self.assertIn('"successor threat cap"', add)
        self.assertIn('new NextAction("stop auto attack", ACTION_HIGH)', add)


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
        self.assertIn("SeekHeldMs(out.holdSince, guid, nowMs) >= kSeekHoldMaxMs", BOSS)
        self.assertIn("SeekHoldClockTick", SEEK_FIGHT)
        self.assertIn("SeekHeldMs", SEEK_FIGHT)

    def test_the_docs_state_the_shipped_timings(self):
        self.assertIn("for 3 s is the first off-tank's take", DOC)
        self.assertIn("kept fighting for 20 s is released to the damage dealers", DOC)
        self.assertIn("falls under 58 % of that reference", DOC)
        self.assertIn("65 % of that reference instead of 80 %", DOC)


if __name__ == "__main__":
    unittest.main()
