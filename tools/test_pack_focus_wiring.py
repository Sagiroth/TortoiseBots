"""The pack focus's glue (issue #1584): the run's skull used to come from the plan's kill_first list alone, so a plan
whose list names one entry (scarlet_library: 4291, its Scarlet Diviner) marked that entry and no other mob of the wing.

W1: the glue reads a mana user (a healer or a caster) on every engaged unit, the pack focus's first class
    (ReadEngagedWith, Facts::engagedUnits).
W2: the fight tick keeps PickKillFirst first and falls back to PickPackFocus, then to FocusGone/DropFocus, so the
    plan's own list is never outranked.
W3: the pack focus is a pull step's own: a boss step, an event and a rest fight return no pick (their adds belong to
    the boss tactics' rows; a linked_death group must die together).
W4: a mana user goes before the rest and only a unit of the fight, attackable, not evading, not held by the run's
    crowd control and within kKillFirstMaxYd of the lead bot is a candidate.
W5: FocusGone also reads Facts::engagedUnits, so a pack focus whose unit left the fight is dropped.
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
        fn = body(POLICY, "        if (const UnitFact* k = PickKillFirst(r, f))")
        # The plan's kill_first pick keeps its own branch; the fallback is the else-if after it.
        self.assertIn("order = IssueKillFirstFocus(r, *k, FocusMovesSkull(", fn)
        fallback = fn[fn.index("else if (const UnitFact* p = PickPackFocus(r, f))"):]
        self.assertIn("IssueKillFirstFocus(r, *p, FocusMovesSkull(", fallback)
        # ... and DropFocus stays behind both (the fallback never replaces it).
        self.assertLess(fn.index("PickPackFocus"), fn.index("FocusGone(r, f)"))
        self.assertIn("DropFocus(r);", fn)

    def test_a_pull_step_owns_the_fallback(self):
        fn = body(POLICY, "inline const UnitFact* PickPackFocus(")
        self.assertIn("if (!s || s->kind != PlanStepKind::Pull || InEvent(r))", fn)
        self.assertIn("return nullptr;", fn)

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
