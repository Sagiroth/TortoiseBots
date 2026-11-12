"""A grouped healer priest no longer sleeps 3 s after each wand shot.

CastShootAction::Execute used to SetDuration(weaponDelay) unconditionally
(1500 ms swing + 1500 ms GCD = 3000 ms): the bot's AI did not tick for 3 s,
so the "wand blocks cast" stop lagged a hit by up to 3 s and the heal went
out late (trials #1556). Now a grouped healer sleeps one react delay
(ShootSleepMs, WandPolicy.h) and the healer part of the stop runs every
tick ("healer wand not allowed"), so the heal starts within one wand swing.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRATEGY = ROOT / "ai/playerbot/strategy"
SPELL_ACTIONS = (STRATEGY / "actions/GenericSpellActions.cpp").read_text()
TRIGGERS = (STRATEGY / "triggers/GenericTriggers.cpp").read_text()
TRIGGER_HDR = (STRATEGY / "triggers/GenericTriggers.h").read_text()
PRIEST_TRIG = (STRATEGY / "priest/PriestTriggers.cpp").read_text()
PRIEST_TRIG_HDR = (STRATEGY / "priest/PriestTriggers.h").read_text()
PRIEST_CTX = (STRATEGY / "priest/PriestAiObjectContext.cpp").read_text()
PRIEST_STRAT = (STRATEGY / "priest/PriestStrategy.cpp").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class HealerWandSleepWiringTest(unittest.TestCase):
    def test_shoot_sleeps_react_delay_for_grouped_healer(self):
        fn = body(SPELL_ACTIONS, "bool CastShootAction::Execute(Event& event)")
        self.assertIn(
            'ShootSleepMs(wand, IsGroupedHealer(ai), weaponDelay, sPlayerbotAIConfig.reactDelay)',
            fn)
        self.assertNotIn("SetDuration(weaponDelay)", fn)

    def test_self_cancel_guard_stays(self):
        fn = body(SPELL_ACTIONS, "bool CastShootAction::Execute(Event& event)")
        self.assertIn("GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL)", fn)
        self.assertIn("m_spellInfo->Id == GetSpellID()", fn)

    def test_wand_means_shoot_spell_name(self):
        fn = body(SPELL_ACTIONS, "bool CastShootAction::Execute(Event& event)")
        self.assertIn('GetSpellName() == "shoot"', fn)

    def test_grouped_healer_shared_predicate(self):
        fn = body(TRIGGERS, "bool ai::IsGroupedHealer(PlayerbotAI* ai)")
        self.assertIn(
            "return ai->GetBot()->GetGroup() && ai->ContainsStrategy(STRATEGY_TYPE_HEAL)",
            fn)
        gate = body(TRIGGERS, "bool ai::HealerWandAllowed(PlayerbotAI* ai, Unit* target)")
        self.assertIn("gate.groupedHealer = IsGroupedHealer(ai);", gate)
        self.assertIn("bool IsGroupedHealer(PlayerbotAI* ai);", TRIGGER_HDR)

    def test_healer_stop_trigger_runs_every_tick(self):
        self.assertRegex(PRIEST_TRIG_HDR, r'Trigger\(ai, "healer wand not allowed", 1\)')
        fn = body(PRIEST_TRIG, "bool PriestHealerWandNotAllowedTrigger::IsActive()")
        self.assertIn("WAND_SHOOT_CATEGORY", fn)
        self.assertIn("IsGroupedHealer(ai)", fn)
        self.assertIn('!HealerWandAllowed(ai, AI_VALUE(Unit*, "current target"))', fn)
        self.assertNotIn("ProbeSpell", fn)

    def test_healer_stop_trigger_registered(self):
        self.assertIn(
            'creators["healer wand not allowed"] = [](PlayerbotAI* ai) { return new PriestHealerWandNotAllowedTrigger(ai); };',
            PRIEST_CTX)

    def test_healer_stop_row_after_wand_blocks_cast(self):
        fn = body(PRIEST_STRAT, "void PriestStrategy::InitCombatTriggers(")
        self.assertRegex(
            fn,
            r'"healer wand not allowed",\s*NextAction::array\(0, new NextAction\("stop wand", ACTION_CRITICAL_HEAL \+ 4\)')

    def test_wand_blocks_cast_row_stays(self):
        fn = body(PRIEST_STRAT, "void PriestStrategy::InitCombatTriggers(")
        self.assertRegex(
            fn,
            r'"wand blocks cast",\s*NextAction::array\(0, new NextAction\("stop wand", ACTION_CRITICAL_HEAL \+ 4\)')

    def test_stop_still_waits_out_swing(self):
        fn = body(SPELL_ACTIONS, "bool StopWandAction::Execute(Event& event)")
        self.assertIn("WandStopWaitMs(", fn)

    def test_probe_trigger_keeps_two_second_interval(self):
        self.assertIn(
            'WandBlocksCastTrigger(PlayerbotAI* ai, std::string name) : Trigger(ai, name, 2)',
            TRIGGER_HDR)

    def test_mage_and_warlock_untouched(self):
        for path in list((STRATEGY / "mage").glob("*.cpp")) + list((STRATEGY / "warlock").glob("*.cpp")):
            self.assertNotIn("healer wand not allowed", path.read_text(), path.name)


if __name__ == "__main__":
    unittest.main()
