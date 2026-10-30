"""The bot's loot entitlement follows the core's loot-view rule (TB #1307).

The rule is pure (ai/playerbot/policy/loot/LootViewRulePolicy.h, tested in
tools/test_loot_view_rule_policy.cpp). This pins the glue, which only builds
against the core:
- LootObject::MayLootCorpse ends in ai::HasVisiblePersonalItemFor, not in the
  core's Loot::hasItemFor, which ignores the round-robin reservation; a hidden
  copy must not make the bot walk to the corpse and open an empty window.
- ai::HasVisiblePersonalItemFor (ai/playerbot/policy/loot/CorpseLootPolicy.h)
  asks the rule for the quest and the conditional list, with the holder key
  taken from roundRobinPlayer and looked up in the same list as the viewer's;
  the FFA list is always shown by the core, so it never asks the rule.
- LootAccess::GetLootContentFor feeds "should loot object" and must skip the
  same hidden copies, or the bot re-adds a reserved corpse.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STACK = (ROOT / "ai/playerbot/LootObjectStack.cpp").read_text()
VALUES = (ROOT / "ai/playerbot/strategy/values/LootValues.cpp").read_text()
HELPER = (ROOT / "ai/playerbot/policy/loot/CorpseLootPolicy.h").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class LootViewRuleWiringTest(unittest.TestCase):
    def test_the_corpse_gate_uses_the_visible_personal_items(self):
        fn = body(STACK, "static bool MayLootCorpse(", "\n}\n")
        lines = [line.strip() for line in fn.splitlines() if line.strip()]
        self.assertTrue(lines[-1].startswith("return ai::HasVisiblePersonalItemFor(loot, bot);"), lines[-1])
        self.assertNotIn("hasItemFor(", fn)

    def test_the_helper_asks_the_rule_for_quest_and_conditional(self):
        fn = body(HELPER, "inline bool HasVisiblePersonalItemFor(", "\n    }\n")
        self.assertIn("PersonalCopyVisible(it.freeforall, turn, mine, has(q, holder))", fn)
        self.assertIn("PersonalCopyVisible(it.freeforall, turn, mine, has(c, holder))", fn)
        # The holder is the round-robin player, never the viewer's key.
        self.assertIn("uint32 const holder = ObjectGuid(loot.roundRobinPlayer).GetCounter();", fn)
        self.assertNotIn("PersonalCopyVisible(it.freeforall, turn, mine, has(q, me))", fn)
        self.assertNotIn("PersonalCopyVisible(it.freeforall, turn, mine, has(c, me))", fn)
        # The quest copy is read from the quest store, like the quest map it sits in.
        self.assertIn("loot.m_questItems[qi.index]", fn)

    def test_the_helper_leaves_free_for_all_copies_shown(self):
        fn = body(HELPER, "inline bool HasVisiblePersonalItemFor(", "\n    }\n")
        ffa = body(fn, "if (has(f, me))", "if (has(c, me))")
        self.assertIn("loot.items[fi.index].is_looted", ffa)
        self.assertNotIn("PersonalCopyVisible", ffa)

    def test_the_loot_content_skips_the_same_hidden_copies(self):
        fn = body(VALUES, "std::vector<LootItem*> LootAccess::GetLootContentFor(", "\n}\n")
        self.assertRegex(fn, r"if \(\s*m != 1\s*&&\s*!ai::PersonalCopyVisible\(")
        self.assertIn("ObjectGuid(loot->roundRobinPlayer).GetCounter()", fn)
        self.assertIn("maps[m]->find(turnHolder)", fn)

    def test_the_refusal_line_reports_the_reservation(self):
        fn = body(STACK, "void LootObject::Refresh(", "\n}\n")
        call = fn.index("ai::CorpseRefusalReason(")
        args = fn[call:fn.index('"debug loot");', call)]
        self.assertIn("bool const reserved = loot.hasItemFor(bot) && !ai::HasVisiblePersonalItemFor(loot, bot);", fn)
        self.assertIn("otherTurn, reserved", args)


if __name__ == "__main__":
    unittest.main()
