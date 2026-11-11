"""Wiring pins for the healer top-up reserve (DungeonRunHealReserve).

The pure rules live in ai/playerbot/HealReservePolicy.h and are tested in
tools/test_heal_reserve_policy.cpp. These pins hold the call sites: both top-up
gates ask HealTopUpAllowedFor, the trigger's old MediumMana line is gone, the
reader that reads the key is the gate (never the row read), the boss victim comes
from the heal row's own boss and not the seek key, the pick skips a refused
top-up, and the row counts one refusal per healer per tick.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY = (ROOT / "ai/playerbot/HealReservePolicy.h").read_text()
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/HealthTriggers.cpp").read_text()
DRUID = (ROOT / "ai/playerbot/strategy/druid/DruidActions.cpp").read_text()
PARTY_HEAL = (ROOT / "ai/playerbot/strategy/values/PartyMemberToHeal.cpp").read_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


class HealReserveWiring(unittest.TestCase):
    def test_the_top_up_trigger_asks_the_rule_and_not_medium_mana(self):
        fn = body(TRIGGERS, "bool PartyMemberAlmostFullHealthTrigger::IsActive()")
        self.assertIn("HealTopUpAllowedFor(ai, bot, target)", fn)
        self.assertNotIn("mediumMana", fn)
        self.assertNotIn('AI_VALUE2(uint8, "mana"', fn)

    def test_the_rejuvenation_spread_asks_the_rule(self):
        pick = body(DRUID, "Unit* ai::FindRejuvenationSpreadTarget(PlayerbotAI* ai, Player* bot)")
        self.assertIn("HealTopUpAllowedFor(ai, bot, member)", pick)
        useful = body(DRUID, "bool CastRejuvenationSpreadAction::isUseful()")
        self.assertIn("HealTopUpAllowedFor(ai, bot, target)", useful)
        self.assertNotIn("mediumMana", useful)

    def test_the_gate_reads_the_key_before_the_row_read(self):
        fn = body(ACTIONS, "bool ai::HealTopUpAllowedFor(PlayerbotAI* ai, Player* bot, Unit* target)")
        self.assertLess(fn.index("dungeonRunHealReserve"), fn.index("BossHealReserve("))
        # Key off: today's rule and nothing else is read.
        self.assertIn("manaPct >= mediumManaPct", fn)
        self.assertIn("HealReserveSkipsTopUp(", fn)

    def test_the_row_read_does_not_read_the_key(self):
        fn = body(ACTIONS, "HealReserveRead ai::BossHealReserve(PlayerbotAI* ai, Player* bot, Unit*& boss)")
        self.assertNotIn("dungeonRunHealReserve", fn)
        self.assertNotIn("dungeonRunSeekAura", fn)
        self.assertIn("HasHealReserveMap(", fn)
        self.assertIn("BossTacticKind::HealReserve", fn)
        # The boss is the winning row's own, resolved from the fight scan.
        self.assertIn("row->bossEntry", fn)
        self.assertIn("fight.units", fn)
        self.assertIn("HealReservePick(", fn)
        # D5: no rows means no seek-clock read at all.
        self.assertLess(fn.index("rows.empty()"), fn.index("ReadSeekFightCached("))
        self.assertIn("ReadSeekFightCached(", fn)

    def test_the_gate_resolves_the_boss_victim_from_the_heal_row(self):
        fn = body(ACTIONS, "bool ai::HealTopUpAllowedFor(PlayerbotAI* ai, Player* bot, Unit* target)")
        self.assertIn("BossHealReserve(ai, bot, boss)", fn)
        self.assertIn("HealReserveTargetExempt(target, boss)", fn)
        # The exemption never reads the seek key's own fight state again.
        self.assertNotIn("state.boss", fn)
        self.assertNotIn("GetVictim()", fn)
        exempt = body(ACTIONS, "bool ai::HealReserveTargetExempt(Unit* target, Unit* boss)")
        self.assertIn("PlayerbotAI::IsTank(", exempt)
        self.assertIn("boss->GetVictim() == target", exempt)

    def test_the_pick_skips_a_refused_top_up(self):
        pick = body(PARTY_HEAL, "Unit* PartyMemberToHeal::Calculate()")
        self.assertIn("HealReserveRefusesTopUp(ai, bot, target, PredictedHealHealth(candidate))", pick)
        self.assertLess(pick.index("NeedsPartyHealing(candidate, bands)"),
                        pick.index("HealReserveRefusesTopUp(ai, bot, target, PredictedHealHealth(candidate))"))
        self.assertLess(pick.index("HealReserveRefusesTopUp(ai, bot, target, PredictedHealHealth(candidate))"),
                        pick.index("candidates.push_back(candidate)"))
        fn = body(ACTIONS, "bool ai::HealReserveRefusesTopUp(PlayerbotAI* ai, Player* bot, Unit* target, "
                           "float effectiveHealthPct)")
        # Key off and no row never refuse: today's pick.
        self.assertIn("dungeonRunHealReserve", fn)
        self.assertIn("HealReserveSkipsTopUp(", fn)
        self.assertIn("HealReserveTargetExempt(", fn)
        self.assertIn("return refused;", fn)

    def test_the_reserve_tier_reads_the_effective_health_not_raw(self):
        # The pick passes triage's own PredictedHealHealth; the gate builds the same number for its target.
        pick = body(PARTY_HEAL, "Unit* PartyMemberToHeal::Calculate()")
        self.assertIn("PredictedHealHealth(candidate)", pick)
        fn = body(ACTIONS, "bool ai::HealTopUpAllowedFor(PlayerbotAI* ai, Player* bot, Unit* target)")
        self.assertIn("EffectiveHealHealthPct(ai, target)", fn)
        self.assertLess(fn.index("EffectiveHealHealthPct(ai, target)"), fn.index("HealReserveSkipsTopUp("))
        self.assertIn("effectiveHealthPct", fn.split("HealReserveSkipsTopUp(", 1)[1])
        # Raw health never classifies the reserve decision at either call site.
        self.assertNotIn("GetHealthPercent", pick.split("HealReserveRefusesTopUp(", 1)[1].split("\n", 1)[0])
        wrapper = body(ACTIONS, "bool ai::HealReserveRefusesTopUp(PlayerbotAI* ai, Player* bot, Unit* target, "
                                "float effectiveHealthPct)")
        self.assertNotIn("GetHealthPercent", wrapper)
        self.assertNotIn("GetHealthPercent", fn)

    def test_the_refusal_count_is_healer_only_per_tick_and_in_band(self):
        count = body(ACTIONS, "static void CountHealReserveRefusal(PlayerbotAI* ai, Player* bot)")
        self.assertIn("refusedTick == tick", count)
        self.assertIn("++window.refused", count)
        self.assertIn('hasLog("bot_events.csv")', count)
        gate = body(ACTIONS, "bool ai::HealTopUpAllowedFor(PlayerbotAI* ai, Player* bot, Unit* target)")
        # Only an in-band candidate of a healer counts, once per tick through the counter.
        self.assertIn("HealReserveSkipsTopUp(", gate)
        self.assertIn("ai->IsHeal(bot)", gate)
        self.assertLess(gate.index("HealReserveSkipsTopUp("), gate.index("CountHealReserveRefusal("))
        # Rows are written for healers only; the old per-evaluation increment is gone.
        self.assertIn('hasLog("bot_events.csv") && ai->IsHeal(bot)', gate)
        self.assertNotIn("++window.refused", gate)

    def test_the_policy_holds_the_pure_rules(self):
        for name in ("HealReserveReleased(", "HealReservePick(", "HealReserveRefuses(", "HealReserveSkipsTopUp(",
                     "HealTopUpAllowedAtHealth(", "HealReserveTopUpTier("):
            self.assertIn(name, POLICY)
        self.assertIn("reservePct", POLICY)
        self.assertIn("released", POLICY)
        self.assertIn("bossEntry", POLICY)
        self.assertNotIn("14834", POLICY)


if __name__ == "__main__":
    unittest.main()
