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
GRACE = (ROOT / "ai/playerbot/GuardGracePolicy.h").read_text()
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
            "struct AssignedTankSlot\n    {\n        std::uint32_t untilTankSlot = 0;\n"
            "        std::uint32_t tankPostSlot = 0;", POLICY)
        add = body(POLICY, "        bool Add(const BossTacticRow& row)")
        self.assertIn(
            "std::uint32_t& have = assignedTankSlots_[row.map][TacticSubjectEntry(row)].tankPostSlot;\n"
            "                if (have == 0 || row.priority < have)\n                    have = row.priority;", add)
        self.assertIn(
            "std::uint32_t& have = assignedTankSlots_[row.map][TacticSubjectEntry(row)].untilTankSlot;\n"
            "                if (have == 0 || row.priority < have)\n                    have = row.priority;", add)
        self.assertIn("row.kind == BossTacticKind::TauntAfterCast &&\n                row.mode == TacticMode::UntilTank",
            add)
        slot_for = body(POLICY, "        std::uint32_t SlotFor(bool auraTactics, bool tankPost) const")
        self.assertIn("if (auraTactics && untilTankSlot > 0)", slot_for)
        self.assertIn("if (tankPost && tankPostSlot > 0 && (slot == 0 || tankPostSlot < slot))", slot_for)
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
        self.assertIn("struct RaidTankFact\n{\n    bool alive = false;\n    bool charmed = false;\n};", RUN)
        self.assertNotIn("targetGuid", body(RUN, "struct RaidTankFact\n{"))  # the tank's selection is no hold
        self.assertIn("std::unordered_map<std::uint64_t, std::uint64_t> guardAssignedLooseSinceMs;", RUN)
        self.assertIn("std::uint64_t guardAssignedClockMs = 0;", RUN)
        self.assertIn("constexpr std::uint32_t kGuardAssignedGraceMs = 8000;", GRACE)
        self.assertIn("using ai::kGuardAssignedGraceMs;", RUN)
        tick = body(RUN, "inline bool RaidGuardTick(Run& r, const Facts& f, std::uint32_t dt, std::string& order)")
        # The clocks are CheckGuard's, once a fight tick and before any order path can return: the healer guard's and
        # the lead join's orders must not pause them, and the tick itself never runs them. With the key off they are
        # cleared instead, so a key turned on mid-run starts clean.
        helper = body(RUN, "inline void UpdateGuardAssignedLoose(Run& r, const Facts& f, std::uint32_t dt)")
        self.assertIn(
            "    r.guardAssignedClockMs += dt;\n"
            "    for (auto it = r.guardAssignedLooseSinceMs.begin(); it != r.guardAssignedLooseSinceMs.end();)\n"
            "    {\n"
            "        bool const loose = std::any_of(f.partyAttackers.begin(), f.partyAttackers.end(),\n"
            "            [&it](const UnitFact& u) { return u.guid == it->first && !u.victimTank; });\n"
            "        it = loose ? std::next(it) : r.guardAssignedLooseSinceMs.erase(it);\n"
            "    }\n", helper)
        # Key off keeps today's arithmetic exactly (cleared extra map, emplace every loose attacker).
        self.assertIn(
            "    if (!f.raid.guardClockHittable)\n"
            "    {\n"
            "        r.guardAssignedUnhittableSinceMs.clear();\n"
            "        for (const UnitFact& u : f.partyAttackers)\n"
            "            if (!u.victimTank)\n"
            "                r.guardAssignedLooseSinceMs.emplace(u.guid, r.guardAssignedClockMs);\n"
            "        return;\n"
            "    }\n", helper)
        # Key on runs the hittable pure clock per attacker (pinned in full in the key-on test).
        self.assertIn("ai::GuardAssignedLooseClock(prior, unh, u.attackable && u.selectable,", helper)
        self.assertNotIn("GuardAssignedLooseClock(prior, unh, true,", helper)
        self.assertNotIn("r.guardAssignedClockMs += dt;", tick)  # the clock advances in CheckGuard only
        self.assertNotIn("guardAssignedLooseSinceMs.emplace", tick)
        check = body(RUN, "inline bool CheckGuard(Run& r, const Facts& f, std::uint32_t dt, std::string& order)")
        self.assertIn(
            "            if (f.raid.guardLeavesAssigned)\n"
            "                UpdateGuardAssignedLoose(r, f, dt);\n"
            "            else if (r.guardAssignedClockMs != 0 || !r.guardAssignedLooseSinceMs.empty())\n"
            "            {\n"
            "                r.guardAssignedClockMs = 0;\n"
            "                r.guardAssignedLooseSinceMs.clear();\n"
            "                r.guardAssignedUnhittableSinceMs.clear();\n"
            "            }\n", check)
        self.assertEqual(check.count("UpdateGuardAssignedLoose(r, f, dt);"), 1)
        self.assertEqual(check.count("guardAssignedClockMs += dt;"), 0)
        self.assertLess(check.index("UpdateGuardAssignedLoose(r, f, dt);"), check.index("RaidHealerGuard(r, f, dt, order)"))
        self.assertLess(check.index("UpdateGuardAssignedLoose(r, f, dt);"), check.index("RaidBossLeadJoin(r, f, dt, order)"))
        # With no off-tank alive the branch falls through to the party guard as before: the clock ran above it.
        self.assertIn(
            "            if (!f.raid.bossOffTanksAlive.empty())\n"
            "            {\n"
            "                if (RaidBossLeadJoin(r, f, dt, order))\n"
            "                    return true;\n"
            "                return RaidGuardTick(r, f, dt, order);\n"
            "            }\n"
            "            if (LeadHoldsPlanBoss(r, f))\n", check)
        # The skip sits right after the boss/held/linked_death pass, inside the add pick (the boss branch above is
        # untouched: it names PlanBossEntry positively, not negatively).
        self.assertIn(
            "            if (PlanBossEntry(r, u.entry) || u.victimTank || LinkedDeathMemberEntry(r, f, u.entry))\n"
            "                continue; // the boss is the main tank's, never a pick-up; a mob an off-tank holds is not loose\n", tick)
        self.assertIn(
            "            if (f.raid.guardLeavesAssigned && r.liveTactics && r.plan)\n"
            "            {\n"
            "                ai::AssignedTankSlot const assigned = r.liveTactics->AssignedTankSlotOf(r.plan->map, u.entry);\n"
            "                std::uint32_t const slot = assigned.SlotFor(f.raid.auraTactics, f.raid.tankPost);\n"
            "                if (slot >= 1)\n"
            "                {\n"
            "                    bool const tankHolds = slot < f.raid.tanks.size() &&\n"
            "                        f.raid.tanks[slot].alive && !f.raid.tanks[slot].charmed;\n"
            "                    bool const fromPost = f.raid.tankPost && assigned.tankPostSlot == slot;\n"
            "                    std::uint32_t const grace = ai::GuardAssignedGraceFor(f.raid.guardClockHittable, fromPost);\n"
            "                    auto const had = r.guardAssignedLooseSinceMs.find(u.guid);\n"
            "                    bool const started = had != r.guardAssignedLooseSinceMs.end();\n"
            "                    std::uint64_t const looseSince = started ? had->second : 0;\n"
            "                    if (ai::GuardLeavesToAssignedTank(f.raid.guardClockHittable, tankHolds, started,\n"
            "                        r.guardAssignedClockMs, looseSince, grace))\n"
            "                        continue;\n", tick)
        # Only that one read: the boss pick, the healer guard and the party guard keep their rules.
        self.assertEqual(tick.count("AssignedTankSlotOf"), 1)
        self.assertEqual(tick.count("guardLeavesAssigned"), 1)
        # The skip needs a live, uncharmed tank and the unit inside the grace, never the slot alone and never the
        # tank's selected target: a tank that cannot take the unit must not keep it on a healer.
        self.assertNotIn("if (tankHolds)\n                    {", tick)
        self.assertNotIn("tankOnIt", tick)
        self.assertNotIn("targetGuid", tick)
        self.assertNotIn("kGuardAssignedGraceMs =", tick)  # the constant is the policy header's, not a local

    def test_the_commander_reads_the_keys_into_the_raid_facts(self):
        self.assertIn(
            "f.raid.guardLeavesAssigned = sPlayerbotAIConfig.dungeonRunGuardLeavesAssignedTank;", COMMANDER)
        self.assertIn("f.raid.auraTactics = sPlayerbotAIConfig.dungeonRunAuraTactics;", COMMANDER)
        self.assertIn("f.raid.tankPost = sPlayerbotAIConfig.dungeonRunTankPost;", COMMANDER)
        # The slot order the rows' priority names, with the state the grace reads (the order drops dead tanks).
        self.assertIn("for (Player* const tank : ai::GetRaidTankRoles(leadAi).tanks)", COMMANDER)
        self.assertIn("tankFact.alive = tank->IsAlive();", COMMANDER)
        self.assertIn("tankFact.charmed = tank->IsCharmed();", COMMANDER)
        self.assertNotIn("tankFact.targetGuid", COMMANDER)
        self.assertIn("f.raid.tanks.push_back(tankFact);", COMMANDER)

    def test_the_hittable_clock_starts_on_first_hittable_tick(self):
        # The pure clock: hittable starts (or keeps) the loose start; unhittable keeps or clears by the grace.
        self.assertIn("constexpr std::uint32_t kGuardPostGraceMs = 15000;", GRACE)
        clock = body(GRACE, "    inline GuardLooseClock GuardAssignedLooseClock(")
        self.assertIn("if (hittable)\n        {", clock)
        self.assertIn("out.looseSinceMs = prior != 0 ? prior : nowMs;", clock)
        self.assertIn("out.unhittableSinceMs = unhittableSince != 0 ? unhittableSince : nowMs;", clock)
        self.assertIn("out.looseSinceMs = nowMs - out.unhittableSinceMs >= kGuardAssignedGraceMs ? 0 : prior;",
            clock)
        self.assertIn("return keyOn && fromTankPost ? kGuardPostGraceMs : kGuardAssignedGraceMs;",
            GRACE)
        leaves = body(GRACE, "    inline bool GuardLeavesToAssignedTank(")
        self.assertIn("if (!tankHolds)\n            return false;", leaves)
        self.assertIn("if (!clockStarted)\n            return keyOn || clockMs < graceMs;", leaves)
        self.assertIn("return clockMs - looseSince < graceMs;", leaves)
        self.assertIn("static_assert(kTankPostReserveGraceMs == kGuardPostGraceMs,", POLICY)
        # The facts and clocks: selectable on the unit, the key on the raid facts, the extra map on the run.
        self.assertIn("bool selectable = true;", RUN)
        self.assertIn("bool guardClockHittable = false;", RUN)
        self.assertIn(
            "std::unordered_map<std::uint64_t, std::uint64_t> guardAssignedUnhittableSinceMs;", RUN)
        # Every clear site clears both maps: the key-off branch, the two resets, and the key-off early return.
        self.assertEqual(RUN.count("r.guardAssignedUnhittableSinceMs.clear();"), 4)
        # The update reads the pure clock with the unit's hittable test, never a literal.
        helper = body(RUN, "inline void UpdateGuardAssignedLoose(Run& r, const Facts& f, std::uint32_t dt)")
        self.assertIn("ai::GuardAssignedLooseClock(prior, unh, u.attackable && u.selectable,", helper)
        self.assertNotIn("GuardAssignedLooseClock(prior, unh, true,", helper)
        # The skip reads the helpers: the tank_post slot picks the 15 s grace.
        tick = body(RUN, "inline bool RaidGuardTick(Run& r, const Facts& f, std::uint32_t dt, std::string& order)")
        self.assertIn("bool const fromPost = f.raid.tankPost && assigned.tankPostSlot == slot;", tick)
        self.assertIn(
            "std::uint32_t const grace = ai::GuardAssignedGraceFor(f.raid.guardClockHittable, fromPost);",
            tick)
        self.assertIn("ai::GuardLeavesToAssignedTank(f.raid.guardClockHittable, tankHolds, started,", tick)
        # The glue fills both: selectable at both attacker sites, the key into the raid facts.
        self.assertEqual(COMMANDER.count(
            "fact.selectable = !attacker->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);"), 2)
        self.assertIn("f.raid.guardClockHittable = sPlayerbotAIConfig.dungeonRunGuardClockHittable;",
            COMMANDER)
        # The regenerated header lists the new fields.
        GEN = (ROOT / "runtime/DecisionCaptureFields.gen.h").read_text()
        self.assertIn("X(selectable)", GEN)
        self.assertIn("X(guardClockHittable)", GEN)
        self.assertIn("X(guardAssignedUnhittableSinceMs)", GEN)

    def test_the_hittable_key_is_read_templated_and_documented(self):
        self.assertIn('dungeonRunGuardClockHittable = config.GetBoolDefault('
            '"AiPlayerbot.DungeonRunGuardClockHittable", false);', CONFIG_CPP)
        self.assertIn("bool dungeonRunGuardClockHittable = false;", CONFIG_H)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.DungeonRunGuardClockHittable = 0$")
        self.assertIn("AiPlayerbot.DungeonRunGuardClockHittable", DOCS)
        self.assertIn("DungeonRunGuardClockHittable", CHANGELOG)
        self.assertNotIn("recommended on",
            DOCS[DOCS.index("### `AiPlayerbot.DungeonRunGuardClockHittable`"):][:400])

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
