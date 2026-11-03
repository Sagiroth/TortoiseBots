"""Guild craft needs are whole-tree totals, wired in GuildValues.cpp.

CollectMissingReagents and FindSupplyCandidate both take their numbers from
runtime/GuildOrderReagentTree.h: the need list sums every branch, a leaf
candidate on the supply ladder asks for the order's total need for that item
(bags and mail short), the mailbox leg asks for the bag shortfall, the totals
ignore mail whose leg is on cooldown, no count drops below the branch's own
shortfall, and both views stop at the header's single depth constant.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
VALUES = (ROOT / "ai/playerbot/strategy/values/GuildValues.cpp").read_text()


def slice_between(text, start_marker, end_marker):
    start = text.index(start_marker)
    end = text.index(end_marker, start)
    return text[start:end]


class GuildOrderReagentTreeWiringTest(unittest.TestCase):
    def test_policy_header_included(self):
        self.assertIn('#include "runtime/GuildOrderReagentTree.h"', VALUES)

    def test_need_list_walks_whole_tree(self):
        body = slice_between(VALUES, "void CollectMissingReagents(", "\nbool FindSupplyCandidate(")
        self.assertIn("MissingReagentsFromTree(", body)
        self.assertNotIn("CollectMissingReagents(ai", body)

    def test_old_one_level_shared_map_gone(self):
        self.assertNotIn("std::unordered_map<uint32_t, uint32_t> shared;", VALUES)

    def test_ladder_uses_tree_totals(self):
        body = slice_between(VALUES, "\nbool FindSupplyCandidate(", "\n// Farmable verdict")
        self.assertIn("ReagentTotalsFromTree(", body)
        self.assertIn("TreeTotalFor(*totals", body)
        # The leaf return uses the total, not the per-branch count.
        self.assertIn(
            "out = { itemId, TreeTotalFor(*totals, depth, itemId, count, ReagentAggregate::ExtShort), false };",
            body,
        )

    def test_mailbox_stage_asks_for_the_bag_shortfall(self):
        body = slice_between(VALUES, "\nbool FindSupplyCandidate(", "\n// Farmable verdict")
        mail = slice_between(body, "if (inMail && inBags < required)", "uint32_t owned = inBags + inMail;")
        # The mail fills what the bags leave short; the external need stores a
        # count one mailbox worth short of the branch (the take moves the whole
        # stack regardless).
        self.assertIn("ReagentAggregate::BagShort", mail)
        self.assertNotIn("ReagentAggregate::ExtShort", mail)

    def test_tree_totals_do_not_spend_skipped_mail(self):
        body = slice_between(VALUES, "std::vector<ReagentTotals> ReagentTotalsFromTree(",
                             "\n// Which whole-tree count")
        self.assertIn("MailLegSkipped(", body)
        self.assertIn("SpentMail(", body)
        ladder = slice_between(VALUES, "\nbool FindSupplyCandidate(", "\n// Farmable verdict")
        self.assertIn("ReagentTotalsFromTree(ai, bot, snap, itemId, count, order)", ladder)

    def test_leaf_count_never_undercuts_the_branch(self):
        body = slice_between(VALUES, "uint32_t TreeTotalFor(", "\n// Every reagent")
        self.assertIn("WholeTreeCount(", body)

    def test_depth_cap_uses_the_shared_constant(self):
        body = slice_between(VALUES, "\nbool FindSupplyCandidate(", "\n// Farmable verdict")
        self.assertIn("kReagentTreeMaxDepth", body)
        self.assertNotIn("depth >= 8", body)


if __name__ == "__main__":
    unittest.main()
