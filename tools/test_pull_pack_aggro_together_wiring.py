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

    def test_the_pack_is_read_only_on_pull_ticks_and_in_the_window(self):
        facts = self.text[self.text.index("void ReadRunFacts(DungeonRunSlot const& slot, Player* lead,"):]
        call = facts.index("ReadPullPacks(map, f.units, f.patrollers, f.packResidue, f.hopUnits);")
        gate = facts.rindex("if (slot.run.joinerWindow ||", 0, call)
        block = facts[gate:call]
        self.assertIn("!f.anyInCombat && (TortoiseBots::DungeonRun::PartyGathered(f) || slot.run.gatherWaived)", block)

    def test_a_pulled_master_brings_its_linked_slaves_outside_the_read(self):
        start = self.text.index("void ReadPullPacks(")
        block = self.text[start:start + 9000]
        # The forward direction (FLAG_AGGRO_ON_AGGRO) is read off the map for every pick, not only for the units the
        # step's read lists.
        self.assertIn("addLinkedSlaves(u, creature->second);", block)
        slaves = block[block.index("auto addLinkedSlaves = "):block.index("for (auto const& pick : picks)")]
        self.assertIn("Cell::VisitGridObjects(master, searcher, kPullPackMasterYd);", slaves)
        self.assertIn("if (!row || !(row->linkingFlag & FLAG_AGGRO_ON_AGGRO) || !slaveOf(slave, row, master))", slaves)
        self.assertIn("u.packGuids.push_back(guid);", slaves)
        self.assertIn("addGroup(u, slave);", slaves)
        # The row's master spawn may carry any of the creature table's entries (a spawn variation): every one is
        # tried, not just creature_id[0].
        master = block[block.index("auto masterOf = "):block.index("auto slaveOf = ")]
        self.assertIn("data->GetCreatureIdCount()", master)
        self.assertNotIn("data->creature_id[0]", master)


if __name__ == "__main__":
    unittest.main()
