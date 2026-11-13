"""The pack focus's glue: the run's skull used to come from the plan's kill_first list alone, so a plan whose list names
one entry (scarlet_library: 4291, its Scarlet Diviner) marked that entry and no other mob of the wing.

W1: the glue reads a mana user (a healer or a caster) on every engaged unit, the pack focus's first class
    (ReadEngagedWith, Facts::engagedUnits).
W2: the fight tick keeps PickKillFirst first and falls back to PickPackFocus, then to FocusGone/DropFocus, so the
    plan's own list is never outranked.
W3: a pull or move step's pack gets the fallback, and so does a boss step's adds: the boss itself, the units a
    kill_first/linked_death row names, an event and a rest fight return no pick.
W4: a mana user goes before the rest and only a unit of the fight, attackable, not evading, not held by the run's
    crowd control and within kKillFirstMaxYd of the lead bot is a candidate.
W5: a skull a player moved onto a living hostile of the fight is adopted as the focus (ahead of the plan's
    kill_first and the fallback), sent by guid without moving the player's mark.
W6: FocusGone also reads Facts::engagedUnits, so a pack focus whose unit left the fight is dropped.
W7: a run skull record is live only while its unit is a living hostile of the fight (SettleFocusIcons drops a record
    whose unit died, despawned or left the fight, and the previous mark once the new one landed); the tick reads a
    skull as the player's only when no live record matches it.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class PackFocusWiringTest(unittest.TestCase):
    def test_engaged_reads_a_mana_user(self):
        fn = body(COMMANDER, "void ReadEngagedWith(")
        self.assertIn("fact.caster = other->GetMaxPower(POWER_MANA) > 0;", fn)

    def test_tick_falls_back_after_the_plans_kill_first(self):
        fn = body(POLICY, "        if (const UnitFact* const h = PlayerSkullFocus(r, f))")
        # The plan's kill_first pick keeps its own branch; the fallback is the else-if after it.
        self.assertIn("else if (const UnitFact* k = PickKillFirst(r, f))", fn)
        self.assertIn("order = IssueKillFirstFocus(r, *k, FocusMovesSkull(", fn)
        fallback = fn[fn.index("else if (const UnitFact* p = PickPackFocus(r, f))"):]
        self.assertIn("IssueKillFirstFocus(r, *p, FocusMovesSkull(", fallback)
        # ... and DropFocus stays behind both (the fallback never replaces it).
        self.assertLess(fn.index("PickPackFocus"), fn.index("FocusGone(r, f)"))
        self.assertIn("DropFocus(r);", fn)

    def test_pull_move_and_boss_adds_get_the_fallback(self):
        fn = body(POLICY, "inline const UnitFact* PickPackFocus(")
        self.assertIn("bool const pullOrMove = s->kind == PlanStepKind::Pull || s->kind == PlanStepKind::Move ||",
                      fn)
        self.assertIn("bool const boss = s->kind == PlanStepKind::Boss;", fn)
        self.assertIn("if (!pullOrMove && !boss)", fn)
        self.assertIn("return nullptr;", fn)

    def test_a_boss_add_is_no_boss_and_no_tactic_row_unit(self):
        fn = body(POLICY, "inline const UnitFact* PickPackFocus(")
        self.assertIn("if (boss && (IsStepBoss(*s, u) || (r.plan && r.plan->KillFirstRank(u.entry) >= 0) ||", fn)
        self.assertIn("BossTacticOwnsEntry(r, *s, u.entry)))", fn)
        own = body(POLICY, "inline bool BossTacticOwnsEntry(")
        self.assertIn("(row.kind == ai::BossTacticKind::KillFirst || row.kind == ai::BossTacticKind::LinkedDeath)",
                      own)
        self.assertIn("(entry == row.targetEntry || entry == row.bossEntry)", own)

    def test_a_players_skull_is_adopted(self):
        fn = body(POLICY, "inline const UnitFact* PlayerSkullFocus(")
        self.assertIn("std::uint64_t const skull = f.raidIcons[kFocusIcon];", fn)
        self.assertIn(
            "if (!skull || skull == r.focusIconGuid || skull == r.prevFocusIconGuid || skull == r.focusIconClear)",
            fn)
        self.assertIn("if (u.guid == skull && u.hostile)", fn)
        self.assertIn("return &u;", fn)
        tick = body(POLICY, "        SettleFocusIcons(r, f);")
        # The player's mark is adopted ahead of the run's own pick, and its order moves no skull.
        self.assertLess(tick.index("PlayerSkullFocus"), tick.index("PickKillFirst(r, f)"))
        self.assertIn("order = IssueKillFirstFocus(r, *h, false);", tick)

    def test_a_runs_skull_record_dies_with_its_unit(self):
        fn = body(POLICY, "inline void SettleFocusIcons(")
        self.assertIn("bool const landed = r.runMs >= r.focusIconMs && r.runMs - r.focusIconMs >= kFocusRefusedIgnoreMs;", fn)
        self.assertIn("if (r.focusIconGuid && skull != r.focusIconGuid && landed)", fn)
        self.assertIn("if (r.prevFocusIconGuid && skull != r.prevFocusIconGuid)", fn)
        self.assertIn("if (r.focusIconGuid && !EngagedHostile(f, r.focusIconGuid))", fn)
        self.assertIn("if (r.prevFocusIconGuid && !EngagedHostile(f, r.prevFocusIconGuid))", fn)
        tick = body(POLICY, "        SettleFocusIcons(r, f);")
        self.assertLess(tick.index("SettleFocusIcons(r, f)"), tick.index("PlayerSkullFocus(r, f)"))
        issue = body(POLICY, "inline std::string IssueKillFirstFocus(")
        self.assertIn("r.focusIconMs = r.runMs;", issue)

    def test_priority_and_the_filters(self):
        fn = body(POLICY, "inline const UnitFact* PickPackFocus(")
        self.assertIn("int const cls = u.caster ? 0 : 1;", fn)
        self.assertIn("!u.hostile || !u.attackable || u.evading || !u.engaged || u.polymorphed || r.ccSent.count(u.guid)",
                      fn)
        self.assertIn("r.runMs < refused->second", fn)
        self.assertIn("u.distLead > kKillFirstMaxYd", fn)

    def test_focus_gone_reads_the_engaged_units(self):
        fn = body(POLICY, "inline bool FocusGone(")
        self.assertIn("for (const UnitFact& u : f.killFirst)", fn)
        self.assertIn("for (const UnitFact& u : f.engagedUnits)", fn)


if __name__ == "__main__":
    unittest.main()
