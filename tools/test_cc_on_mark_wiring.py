"""Every run crowd-control family has a bot row that fires on the run's mark.

runtime/DungeonRunPolicy.h orders `cc <mark> <guid> <bot>` for a family and
expects the marked bot's own combat AI to cast it (RunCcOf, PickCcOrder). The
mark is only cast when the class strategy carries the family's row and it reads
the assigned mark (CcTargetValue's "cc target", or the rogue's "sap cc mark"
before the pull). A family with a mark but no row is a silent no-op: the run
sends the order, CheckCc waits for an aura that never comes and notes "did not
land". This pins one row per family, in a combat (or the rogue's non-combat)
strategy of its class.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRATEGIES = ROOT / "ai/playerbot/strategy"


class CcOnMarkWiringTest(unittest.TestCase):
    def test_every_family_has_its_on_cc_row(self):
        rows = [
            # family, file, the trigger row, the action it queues
            ("Polymorph", "mage/MageStrategy.cpp", '"polymorph",', 'new NextAction("polymorph"'),
            ("Shackle", "priest/PriestStrategy.cpp", '"shackle undead",', 'new NextAction("shackle undead"'),
            ("Banish", "warlock/WarlockStrategy.cpp", '"banish",', 'new NextAction("banish on cc"'),
            ("Sap", "rogue/RogueStrategy.cpp", '"sap cc mark",', 'new NextAction("sap"'),
            ("Hibernate", "druid/DruidStrategy.cpp", '"hibernate",', 'new NextAction("hibernate on cc"'),
            ("FreezingTrap", "hunter/HunterStrategy.cpp", '"freezing trap",', 'new NextAction("freezing trap on cc"'),
            ("Seduction", "warlock/WarlockStrategy.cpp", '"seduction",', 'new NextAction("seduction on cc"'),
            ("Repentance", "paladin/RetributionPaladinStrategy.cpp", '"repentance on cc",',
             'new NextAction("repentance on cc"'),
            ("ScareBeast", "hunter/HunterStrategy.cpp", '"scare beast",', 'new NextAction("scare beast on cc"'),
            ("Fear", "warlock/WarlockStrategy.cpp", '"fear",', 'new NextAction("fear on cc"'),
        ]
        for family, path, trigger, action in rows:
            text = (STRATEGIES / path).read_text()
            with self.subTest(family=family):
                self.assertIn(trigger, text)
                self.assertIn(action, text)

    def test_repentance_on_cc_is_registered(self):
        context = (STRATEGIES / "paladin/PaladinAiObjectContext.cpp").read_text()
        self.assertEqual(context.count('creators["repentance on cc"]'), 2)
        self.assertIn('return new RepentanceCcTrigger(ai);', context)
        self.assertIn('return new CastRepentanceOnCcAction(ai);', context)
        triggers = (STRATEGIES / "paladin/PaladinTriggers.h").read_text()
        self.assertIn('CC_TRIGGER(RepentanceCcTrigger, "repentance");', triggers)
        actions = (STRATEGIES / "paladin/PaladinActions.h").read_text()
        self.assertIn("class CastRepentanceOnCcAction : public CastSpellAction", actions)
        self.assertIn('GetTargetName() override { return "cc target"; }', actions)
        self.assertIn("IsCrowdControlAction() const override { return true; }", actions)


if __name__ == "__main__":
    unittest.main()
