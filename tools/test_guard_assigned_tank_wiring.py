"""Wiring pins for AiPlayerbot.DungeonRunGuardLeavesAssignedTank (the raid guard leaves an assigned unit to its tank).

The rule is pure (ai/playerbot/BossTacticsPolicy.h: AssignedTankSlotOf, filled in Add; runtime/DungeonRunPolicy.h:
RaidFacts::guardLeavesAssigned with the two assigning kinds' keys and the slot's tank state, read in RaidGuardTick's
add pick) and tested in tools/test_boss_tactics_policy.cpp and tools/test_dungeon_run_policy.cpp. These pins prove the
live code reads the keys into the facts the guard tests, that the skip sits in the add-pick loop and nowhere else, that
a row only counts while the key that reads its kind is on and the slot's tank holds the unit or is inside the grace,
and that the key is templated and documented.

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
        # The table fills the map as rows are added, with the assigning kinds, and answers slot 0 before the seal.
        self.assertIn(
            "struct AssignedTankSlot\n    {\n        std::uint32_t slot = 0;\n        bool untilTank = false;\n"
            "        bool tankPost = false;\n    };", POLICY)
        add = body(POLICY, "        bool Add(const BossTacticRow& row)")
        self.assertIn("if (row.kind == BossTacticKind::TankPost)\n                slot = row.priority;", add)
        self.assertIn("row.kind == BossTacticKind::TauntAfterCast && row.mode == TacticMode::UntilTank", add)
        self.assertIn("AssignedTankSlot& have = assignedTankSlots_[row.map][TacticSubjectEntry(row)];", add)
        self.assertIn("if (have.slot == 0 || slot < have.slot)", add)
        self.assertIn(
            "if (row.kind == BossTacticKind::TankPost)\n                    have.tankPost = true;\n"
            "                else\n                    have.untilTank = true;", add)
        accessor = body(POLICY, "        AssignedTankSlot AssignedTankSlotOf(std::uint32_t map, std::uint32_t entry) const")
        self.assertIn("if (!sealed_.load(std::memory_order_acquire))", accessor)
        self.assertIn("assignedTankSlots_.find(map)", accessor)
        self.assertIn("return it == byMap->second.end() ? none : it->second;", accessor)
        self.assertIn(
            "std::unordered_map<std::uint32_t, std::unordered_map<std::uint32_t, AssignedTankSlot>> assignedTankSlots_;",
            POLICY)

    def test_the_raid_guard_skips_an_assigned_unit_in_its_add_pick(self):
        self.assertIn("bool guardLeavesAssigned = false;", RUN)
        self.assertIn("bool auraTactics = false;", RUN)
        self.assertIn("bool tankPost = false;", RUN)
        self.assertIn("std::vector<RaidTankFact> tanks;", RUN)
        self.assertIn(
            "struct RaidTankFact\n{\n    bool alive = false;\n    bool charmed = false;\n"
            "    std::uint64_t targetGuid = 0;\n};", RUN)
        self.assertIn("std::unordered_map<std::uint64_t, std::uint32_t> guardAssignedLooseMs;", RUN)
        self.assertIn("constexpr std::uint32_t kGuardAssignedGraceMs = 8000;", RUN)
        tick = body(RUN, "inline bool RaidGuardTick(Run& r, const Facts& f, std::uint32_t dt, std::string& order)")
        # The skip sits right after the boss/held/linked_death pass, inside the add pick (the boss branch above is
        # untouched: it names PlanBossEntry positively, not negatively).
        self.assertIn(
            "            if (PlanBossEntry(r, u.entry) || u.victimTank || LinkedDeathMemberEntry(r, f, u.entry))\n"
            "                continue; // the boss is the main tank's, never a pick-up; a mob an off-tank holds is not loose\n", tick)
        self.assertIn(
            "            if (f.raid.guardLeavesAssigned && r.liveTactics && r.plan)\n"
            "            {\n"
            "                ai::AssignedTankSlot const assigned = r.liveTactics->AssignedTankSlotOf(r.plan->map, u.entry);\n"
            "                if (assigned.slot >= 1 &&\n"
            "                    ((assigned.untilTank && f.raid.auraTactics) || (assigned.tankPost && f.raid.tankPost)))\n"
            "                {\n"
            "                    bool const tankHolds = assigned.slot < f.raid.tanks.size() &&\n"
            "                        f.raid.tanks[assigned.slot].alive && !f.raid.tanks[assigned.slot].charmed;\n"
            "                    bool const tankOnIt = tankHolds && f.raid.tanks[assigned.slot].targetGuid == u.guid;\n"
            "                    auto const had = r.guardAssignedLooseMs.find(u.guid);\n"
            "                    std::uint32_t const looseMs = had == r.guardAssignedLooseMs.end() ? 0 : had->second;\n"
            "                    if (tankHolds && (tankOnIt || looseMs < kGuardAssignedGraceMs))\n"
            "                    {\n", tick)
        self.assertIn(
            "                        if (tankOnIt)\n"
            "                            r.guardAssignedLooseMs.erase(u.guid);\n"
            "                        else\n"
            "                            r.guardAssignedLooseMs[u.guid] = looseMs + dt;\n"
            "                        continue;\n", tick)
        # Only that one read: the boss pick, the healer guard and the party guard keep their rules.
        self.assertEqual(tick.count("AssignedTankSlotOf"), 1)
        self.assertEqual(tick.count("guardLeavesAssigned"), 1)
        # The skip needs a live, uncharmed tank on the unit or inside the grace, never the slot alone.
        self.assertNotIn("if (tankHolds)\n                    {", tick)
        self.assertNotIn("kGuardAssignedGraceMs =", tick)  # the constant is the policy header's, not a local

    def test_the_commander_reads_the_keys_into_the_raid_facts(self):
        self.assertIn(
            "f.raid.guardLeavesAssigned = sPlayerbotAIConfig.dungeonRunGuardLeavesAssignedTank;", COMMANDER)
        self.assertIn("f.raid.auraTactics = sPlayerbotAIConfig.dungeonRunAuraTactics;", COMMANDER)
        self.assertIn("f.raid.tankPost = sPlayerbotAIConfig.dungeonRunTankPost;", COMMANDER)
        # The slot order the rows' priority names, with the state the grace reads.
        self.assertIn("for (Player* const tank : ai::GetRaidTankRoles(leadAi).tanks)", COMMANDER)
        self.assertIn("tankFact.alive = tank->IsAlive();", COMMANDER)
        self.assertIn("tankFact.charmed = tank->IsCharmed();", COMMANDER)
        self.assertIn("tankFact.targetGuid = tank->GetSelectionGuid().GetRawValue();", COMMANDER)
        self.assertIn("f.raid.tanks.push_back(tankFact);", COMMANDER)

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
