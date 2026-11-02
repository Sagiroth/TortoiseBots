"""The pull's pack takes a creature group's members only when the group is "aggro together".

A plain formation walks with its leader but fights apart, so the glue (ReadPullPacks, addGroup)
must not put its other members in UnitFact::packGuids. The estimator (ReadPullJoiners) makes the
same check; this test pins the glue to it so the two cannot drift apart.

Run: python3 -m unittest tools/test_pull_pack_aggro_together_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class PullPackAggroTogetherWiringTest(unittest.TestCase):
    def setUp(self):
        self.text = (ROOT / "commands/Commander.cpp").read_text()

    def test_add_group_checks_the_aggro_together_flag_before_adding_members(self):
        start = self.text.index("auto addGroup = [](UnitFact& u, Creature* c)")
        block = self.text[start:start + 600]
        flag = block.index("group->HasGroupFlag(OPTION_AGGRO_TOGETHER)")
        loop = block.index("group->GetMembers()")
        self.assertLess(flag, loop, "the flag check must gate the member loop")
        self.assertIn("if (!group || !group->HasGroupFlag(OPTION_AGGRO_TOGETHER))", block)

    def test_estimator_uses_the_same_flag(self):
        start = self.text.index("void ReadPullJoiners(")
        block = self.text[start:start + 6000]
        self.assertIn("if (group->HasGroupFlag(OPTION_AGGRO_TOGETHER))", block)


if __name__ == "__main__":
    unittest.main()
