"""Wiring pins for AiPlayerbot.DungeonRunGuardLeavesAssignedTank (the raid guard leaves an assigned unit to its tank).

The rule is pure (ai/playerbot/BossTacticsPolicy.h: AssignedTankSlot, filled in Add; runtime/DungeonRunPolicy.h:
RaidFacts::guardLeavesAssigned read in RaidGuardTick's add pick) and tested in tools/test_boss_tactics_policy.cpp and
tools/test_dungeon_run_policy.cpp. These pins prove the live code reads the key into the fact the guard tests, that the
skip sits in the add-pick loop and nowhere else, and that the key is templated and documented.

Run: python3 -m unittest tools/test_guard_assigned_tank_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY = (ROOT / "ai/playerbot/BossTacticsPolicy.h").read_text()
RUN = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
DOCS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
BOSS_DOCS = (ROOT / "docs/play/boss-tactics.md").read_text()
CHANGELOG = (ROOT / "CHANGELOG.md").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class GuardAssignedTankWiring(unittest.TestCase):
    def test_the_table_answers_the_units_tank_slot(self):
        # The table fills the map as rows are added and answers -1 before the seal.
        add = body(POLICY, "        bool Add(const BossTacticRow& row)")
        self.assertIn("if (row.kind == BossTacticKind::TankPost)\n                slot = row.priority;", add)
        self.assertIn("row.kind == BossTacticKind::TauntAfterCast && row.mode == TacticMode::UntilTank", add)
        self.assertIn("assignedTankSlots_[row.map][TacticSubjectEntry(row)]", add)
        self.assertIn("if (have == 0 || slot < have)", add)
        accessor = body(POLICY, "        int AssignedTankSlot(std::uint32_t map, std::uint32_t entry) const")
        self.assertIn("if (!sealed_.load(std::memory_order_acquire))", accessor)
        self.assertIn("assignedTankSlots_.find(map)", accessor)
        self.assertIn("return it == byMap->second.end() ? -1 : static_cast<int>(it->second);", accessor)
        self.assertIn(
            "std::unordered_map<std::uint32_t, std::unordered_map<std::uint32_t, std::uint32_t>> assignedTankSlots_;",
            POLICY)

    def test_the_raid_guard_skips_an_assigned_unit_in_its_add_pick(self):
        self.assertIn("bool guardLeavesAssigned = false;", RUN)
        tick = body(RUN, "inline bool RaidGuardTick(Run& r, const Facts& f, std::uint32_t dt, std::string& order)")
        # The skip sits right after the boss/held/linked_death pass, inside the add pick (the boss branch above is
        # untouched: it names PlanBossEntry positively, not negatively).
        self.assertIn(
            "            if (PlanBossEntry(r, u.entry) || u.victimTank || LinkedDeathMemberEntry(r, f, u.entry))\n"
            "                continue; // the boss is the main tank's, never a pick-up; a mob an off-tank holds is not loose\n"
            "            // A unit a tank row assigns to a tank slot (until_tank, tank_post) stays with that tank: its window\n"
            "            // hands it to the next usable tank itself, and its post takes it.\n"
            "            if (f.raid.guardLeavesAssigned && r.liveTactics && r.plan &&\n"
            "                r.liveTactics->AssignedTankSlot(r.plan->map, u.entry) >= 1)\n"
            "                continue;", tick)
        # Only that one read: the boss pick, the healer guard and the party guard keep their rules.
        self.assertEqual(tick.count("AssignedTankSlot"), 1)
        self.assertEqual(tick.count("guardLeavesAssigned"), 1)

    def test_the_commander_reads_the_key_into_the_raid_facts(self):
        self.assertIn(
            "f.raid.guardLeavesAssigned = sPlayerbotAIConfig.dungeonRunGuardLeavesAssignedTank;", COMMANDER)

    def test_config_key_is_read_templated_and_documented(self):
        self.assertIn(
            'dungeonRunGuardLeavesAssignedTank =\n        config.GetBoolDefault('
            '"AiPlayerbot.DungeonRunGuardLeavesAssignedTank", false);', CONFIG_CPP)
        self.assertIn("bool dungeonRunGuardLeavesAssignedTank = false;", CONFIG_H)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.DungeonRunGuardLeavesAssignedTank = 0$")
        self.assertIn("AiPlayerbot.DungeonRunGuardLeavesAssignedTank", DOCS)
        self.assertIn("recommended on", DOCS[DOCS.index("### `AiPlayerbot.DungeonRunGuardLeavesAssignedTank`"):][:400])
        self.assertIn("AiPlayerbot.DungeonRunGuardLeavesAssignedTank", BOSS_DOCS)
        self.assertIn("DungeonRunGuardLeavesAssignedTank", CHANGELOG)


if __name__ == "__main__":
    unittest.main()
