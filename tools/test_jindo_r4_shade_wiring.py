"""Pins the glue of fix/jindo-r4-shade (walk 56 F3, Shades on uncursed healers): the BossTacticsShadeCover row.

The rules are unit-tested in tools/test_shade_cover_policy.cpp (ShadeCoverAsks, ShadeCoverMissReason, ShadeCoverLogDue).
These pins only prove the live code feeds them.
"""
import os
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as handle:
        return handle.read()


ACTIONS = read("ai/playerbot/strategy/actions/BossTacticsActions.cpp")
ACTIONS_H = read("ai/playerbot/strategy/actions/BossTacticsActions.h")
TRIGGERS = read("ai/playerbot/strategy/triggers/BossTacticsTriggers.cpp")
DOCS = read("docs/play/boss-tactics.md")
DOCS_ROWS = read("docs/play/refusal-rows.md")
CHANGELOG = read("CHANGELOG.md")


def member(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


class ShadeCoverRow(unittest.TestCase):
    def body(self):
        return member(ACTIONS, "void ai::LogShadeCover(", "\n}\n")

    def test_the_kill_first_trigger_runs_the_row(self):
        trigger = member(TRIGGERS, "bool BossTacticsKillFirstTrigger::IsActive()", "\n}\n")
        self.assertIn("LogShadeCover(ai, bot, fight);", trigger)
        self.assertLess(trigger.index("LogShadeCover(ai, bot, fight);"), trigger.index("FindBossKillFirst(ai, bot, fight)"))
        self.assertIn("void LogShadeCover(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight);", ACTIONS_H)

    def test_only_a_carrier_with_the_csv_runs_it(self):
        body = self.body()
        self.assertIn('sPlayerbotAIConfig.hasLog("bot_events.csv")', body)
        self.assertIn("CarrierKillFirstRow(*row)", body)
        self.assertIn("return; // only a carrier sees the Shades", body)

    def test_shades_on_a_healer_from_both_reads(self):
        body = self.body()
        self.assertIn("for (Unit* unit : fight.units)\n        consider(unit);", body)
        self.assertIn('"kill first grid units"', body)
        self.assertIn("if (ShadeOnGroupHealer(bot, unit))", body)
        self.assertIn("shades.size() >= kShadeSeenMaxPerPass", body)

    def test_the_policy_decides_who_and_why(self):
        body = self.body()
        self.assertIn("ShadeCoverAsks(role, cursed, holdsBoss)", body)
        self.assertIn("ShadeCoverMissReason(facts, kShadeCoverReachYd)", body)
        self.assertIn("facts.threatGate = ShadeCoverThreatGate(role, successor", body)
        self.assertIn("facts.charmed = !member->GetCharmerGuid().IsEmpty();", body)
        self.assertIn("facts.alive = member->IsAlive();", body)
        self.assertIn("PlayerbotAI::IsHeal(member)", body)
        self.assertIn("member == mainTank ? CarrierStrikeRole::MainTank : CarrierStrikeRole::OffTank", body)

    def test_logged_under_the_member_and_throttled(self):
        body = self.body()
        self.assertIn('sPlayerbotAIConfig.logEvent(memberAi, "BossTacticsShadeCover", info1, info2);', body)
        self.assertIn("ShadeCoverMissDue(memberKey, shadeKey, miss != ShadeCoverMiss::None, nowMs)", body)
        due = member(ACTIONS, "static bool ShadeCoverMissDue(", "\n}\n")
        self.assertIn("kShadeCoverMaxEntries", due)
        self.assertIn("ShadeCoverLogDue(g_shadeCoverLog[key], nowMs)", due)
        self.assertIn("g_shadeCoverLog.erase(key);", due)

    def test_the_pick_shares_the_victim_read(self):
        self.assertIn("c.victimHealer = ShadeOnGroupHealer(bot, unit);", ACTIONS)

    def test_documented(self):
        self.assertIn("BossTacticsShadeCover", DOCS)
        self.assertIn("BossTacticsShadeCover", DOCS_ROWS)
        self.assertIn("BossTacticsShadeCover", CHANGELOG.split("\n- ", 3)[1])
        for word in ("dead", "charmed", "other_shade", "out_of_range", "threat_gate", "on_boss", "other_target",
                     "no_target"):
            self.assertIn(word, member(DOCS_ROWS, "`BossTacticsShadeCover`", "\n"))


if __name__ == "__main__":
    unittest.main()
