"""Debug loot says why a corpse is not the bot's to loot (TB #1307).

The reason is pure (ai/playerbot/policy/loot/CorpseRefusalReasonPolicy.h,
tested in tools/test_corpse_refusal_reason_policy.cpp). This pins the glue:
- LootObject::Refresh reports it on the "not this bot's to loot" line, inside
  the existing if (debug) after MayLootCorpse, so the gate is unchanged;
- the presence argument is IsAllowedLooter(guid, false) -- the looter list
  recorded at the kill, without the personal shortcut;
- the roll and personal-item arguments come from the corpse's own loot, and
  the reservation flag is hasItemFor without a visible copy.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STACK = (ROOT / "ai/playerbot/LootObjectStack.cpp").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class CorpseRefusalReasonWiringTest(unittest.TestCase):
    def setUp(self):
        self.refresh = body(STACK, "void LootObject::Refresh(", "\n}\n")
        self.gate = self.refresh.index("if (MayLootCorpse(bot, creature))")
        self.call = self.refresh.index("ai::CorpseRefusalReason(")

    def test_the_reason_is_reported_from_the_refusal_line(self):
        window = self.refresh[max(0, self.call - 240):self.refresh.index('"debug loot");', self.call)]
        self.assertIn('"Creature lootable but not this bot\'s to loot: " +\n                    ai::CorpseRefusalReason(', window)
        self.assertNotIn('"Creature lootable but not this bot\'s to loot."', self.refresh)

    def test_the_presence_argument_skips_the_personal_shortcut(self):
        args = self.refresh[self.call:self.refresh.index('"debug loot");', self.call)]
        self.assertIn("ai::CorpseRefusalReason(loot.IsAllowedLooter(bot->GetObjectGuid(), false),", args)
        self.assertNotIn("IsAllowedLooter(bot->GetObjectGuid())", args)

    def test_the_corpse_facts_are_passed(self):
        args = self.refresh[self.call:self.refresh.index('"debug loot");', self.call)]
        self.assertIn("!loot.m_questItems.empty()", args)
        self.assertIn("loot.hasItemFor(bot)", args)

    def test_the_reservation_flag_compares_visible_copies(self):
        self.assertIn("bool const reserved = loot.hasItemFor(bot) && !ai::HasVisiblePersonalItemFor(loot, bot);",
                      self.refresh)
        args = self.refresh[self.call:self.refresh.index('"debug loot");', self.call)]
        self.assertIn("otherTurn, reserved", args)

    def test_the_report_stays_behind_the_loot_gate(self):
        self.assertLess(self.gate, self.call)
        between = self.refresh[self.gate:self.call]
        # The gate's own branch returns before the debug report, so reaching the
        # report still means MayLootCorpse refused the corpse.
        self.assertIn("this->guid = guid;", between)
        self.assertIn("return;", between)
        debug_at = between.rindex("if (debug)")
        tail = between[debug_at:]
        self.assertTrue(tail.startswith("if (debug)"), tail)
        self.assertIn("{", tail)
        self.assertNotIn("}", tail)
        self.assertIn("ai->TellDebug(", tail)


if __name__ == "__main__":
    unittest.main()
