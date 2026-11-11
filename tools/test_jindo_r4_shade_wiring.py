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
POLICY = read("ai/playerbot/BossTacticsPolicy.h")
CONFIG_H = read("ai/playerbot/PlayerbotAIConfig.h")
CONFIG_CPP = read("ai/playerbot/PlayerbotAIConfig.cpp")
CONF = read("ai/playerbot/aiplayerbot.conf.dist.in")
DOCS_RUNS = read("docs/server/settings/dungeons-raids.md")


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
        self.assertIn("ShadeCoverMissDue(memberKey, shadeKey, miss != ShadeCoverMiss::None, miss == ShadeCoverMiss::Dead,", body)
        due = member(ACTIONS, "static bool ShadeCoverMissDue(", "\n}\n")
        self.assertIn("std::lock_guard<std::mutex> lock(g_shadeCoverLogMutex);", due)
        self.assertIn("ShadeCoverLogBook g_shadeCoverLog;", ACTIONS)

    def test_the_pick_shares_the_victim_read(self):
        self.assertIn("c.victimHealer = ShadeOnGroupHealer(bot, unit);", ACTIONS)

    def test_documented(self):
        self.assertIn("BossTacticsShadeCover", DOCS)
        self.assertIn("BossTacticsShadeCover", DOCS_ROWS)
        self.assertIn("BossTacticsShadeCover", "".join(CHANGELOG.split("\n- ", 20)[1:20]))  # newest entries
        for word in ("dead", "charmed", "other_shade", "out_of_range", "threat_gate", "guard", "on_boss", "other_target",
                     "no_target"):
            self.assertIn(word, member(DOCS_ROWS, "`BossTacticsShadeCover`", "\n"))


class ShadeHealerCover(unittest.TestCase):
    def test_the_key_ships_off(self):
        self.assertIn("bool dungeonRunShadeHealerCover = false;", CONFIG_H)
        self.assertIn('config.GetBoolDefault("AiPlayerbot.DungeonRunShadeHealerCover", false)', CONFIG_CPP)
        self.assertIn("AiPlayerbot.DungeonRunShadeHealerCover = 0", CONF)
        self.assertIn("### `AiPlayerbot.DungeonRunShadeHealerCover`", DOCS_RUNS)
        self.assertIn("Recommended on.", member(DOCS_RUNS, "### `AiPlayerbot.DungeonRunShadeHealerCover`", "</details>"))
        self.assertIn("recommended on", member(CONF, "# Shade healer cover at Jin'do", "AiPlayerbot.DungeonRunShadeHealerCover = 0"))
        self.assertIn("DungeonRunShadeHealerCover", DOCS)
        self.assertIn("DungeonRunShadeHealerCover", "".join(CHANGELOG.split("\n- ", 20)[1:20]))  # newest entries

    def test_the_carrier_pick_reads_the_key(self):
        pick = member(ACTIONS, "Unit* PickBossKillFirst(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight, KillFirstPickMode mode,\n    float carrierReachYd)\n{", "\n}\n")
        self.assertIn(
            "PickCarrierOverride(fight.rows, candidates, carrierReachYd, sPlayerbotAIConfig.dungeonRunShadeHealerCover)",
            pick)
        self.assertIn(": PickKillFirst(fight.rows, candidates);", pick)

    def test_the_strike_trigger_feeds_the_cover(self):
        trigger = member(TRIGGERS, "bool BossTacticsCarrierStrikeTrigger::IsActive()", "\n}\n")
        self.assertIn("if (sPlayerbotAIConfig.dungeonRunShadeHealerCover)", trigger)
        self.assertIn("bot->GetDistance(shade) <= kShadeCoverReachYd &&", trigger)
        self.assertIn("ShadeCoverSameFloor(bot->GetPositionZ() - shade->GetPositionZ())", trigger)
        self.assertIn("ShadeCoverLatchHolds(latch, shade ? shade->GetObjectGuid().GetRawValue() : 0,", trigger)
        self.assertIn("SetShadeCoverLatch(bot, latch);", trigger)
        self.assertIn("sPlayerbotAIConfig.dungeonRunShadeHealerCover && role == CarrierStrikeRole::OffTank", trigger)
        self.assertIn("in.cursed, shade && ShadeOnGroupHealer(bot, shade), shadeVictim == bot,", trigger)
        self.assertIn("in.shadeCover = latched && inCoverReach && threatOk;", trigger)
        self.assertLess(trigger.index("in.shadeCover ="), trigger.index("CarrierStrikeAllowed(in,"))
        # The shade the cover reads is the strike's own pick.
        self.assertIn("Unit* const shade = FindBossCarrierStrikePick(ai, bot, fight);", trigger)
        self.assertLess(trigger.index("Unit* const shade = FindBossCarrierStrikePick"), trigger.index("in.shadeCover ="))
        # The curse over: the latch ends, on the trigger's early return for a bot with no carrier row.
        early = member(trigger, "if (!HasCarrierKillFirstRow(fight.rows))", "return false;")
        self.assertIn("SetShadeCoverLatch(bot, ShadeCoverLatch());", early)

    def test_the_latch_is_resolved_before_the_general_pick(self):
        pick = member(ACTIONS, "Unit* ai::FindBossCarrierStrikePick(", "\n}\n")
        self.assertIn("if (Unit* const latched = LatchedCoverShade(ai, bot, fight))", pick)
        self.assertLess(pick.index("LatchedCoverShade(ai, bot, fight)"),
                        pick.index("PickBossKillFirst(ai, bot, fight, KillFirstPickMode::Carrier"))
        self.assertIn("if (sPlayerbotAIConfig.dungeonRunShadeHealerCover)", pick)
        latched = member(ACTIONS, "static Unit* LatchedCoverShade(", "\n}\n")
        for needle in ("facts.carrierEntry = IsCarrierRowEntry(fight, unit->GetEntry());",
                       "facts.alive = sServerFacade.IsAlive(unit);",
                       "facts.attackable = PossibleAttackTargetsValue::IsValid(unit, bot);",
                       "facts.dropYd = bot->GetPositionZ() - unit->GetPositionZ();",
                       "facts.onSelf = unit->GetVictim() == bot;",
                       "ShadeCoverResolvePick(latch, HasCarrierKillFirstRow(fight.rows), facts, 0,"):
            self.assertIn(needle, latched)
        # The trigger and the action both read the pick through it.
        self.assertIn("FindBossCarrierStrikePick(ai, bot, fight)",
                      member(ACTIONS, "bool BossTacticsCarrierStrikeAction::Execute(", "\n}\n"))

    def test_the_offtank_cover_needs_half_the_top_tank_threat(self):
        trigger = member(TRIGGERS, "bool BossTacticsCarrierStrikeTrigger::IsActive()", "\n}\n")
        self.assertIn("bool threatOk = !in.threatSuccessor;", trigger)
        self.assertIn("row->kind == BossTacticKind::ThreatSuccessor && row->bossEntry == unit->GetEntry()", trigger)
        self.assertIn("float const own = sServerFacade.GetThreatManager(unit).getThreat(bot);", trigger)
        self.assertIn("for (Player* tank : tankRoles.tanks)", trigger)
        self.assertIn("threatOk = ShadeCoverThreatAllows(own, highest, kThreatCapOffTankAwayShare);", trigger)
        self.assertIn('#include "playerbot/ThreatCapPolicy.h"', TRIGGERS)

    def test_the_cover_line_leaves_out_only_the_victim(self):
        trigger = member(TRIGGERS, "bool BossTacticsCarrierStrikeTrigger::IsActive()", "\n}\n")
        self.assertIn("uint8 lowestCoverPct = lowestMemberPct;", trigger)
        self.assertIn("if (member == bot || member != shadeVictim)", trigger)
        self.assertIn("in.lowestCoverPct = lowestCoverPct;", trigger)
        self.assertIn("ShadeCoverLatch GetShadeCoverLatch(Player* bot);", ACTIONS_H)

    def test_the_pick_reads_the_height(self):
        self.assertEqual(ACTIONS.count("c.dropYd = bot->GetPositionZ() - unit->GetPositionZ();"), 2)
        trigger = member(TRIGGERS, "bool BossTacticsCarrierStrikeTrigger::IsActive()", "\n}\n")
        # The main tank is still its own role, and the strike key still gates the whole node.
        self.assertIn("role = tankRoles.BotIsMainTank() ? CarrierStrikeRole::MainTank : CarrierStrikeRole::OffTank;", trigger)
        self.assertIn("if (!sPlayerbotAIConfig.dungeonRunCarrierStrike)", trigger)

    def test_the_policy_keeps_the_holder_and_the_main_tank(self):
        allowed = member(POLICY, "inline CarrierStrikeVerdict CarrierStrikeAllowed(", "\n    }\n")
        self.assertIn("in.role == CarrierStrikeRole::OffTank && in.shadeCover && in.cursed && !in.holdsBoss", allowed)
        self.assertLess(allowed.index("return CarrierStrikeVerdict::MainTank;"), allowed.index("offTankCover"))
        self.assertIn("return CarrierStrikeVerdict::GuardTank;", allowed)

    def test_the_row_knows_the_cover(self):
        body = member(ACTIONS, "void ai::LogShadeCover(", "\n}\n")
        self.assertIn(
            "sPlayerbotAIConfig.dungeonRunShadeHealerCover && sPlayerbotAIConfig.dungeonRunCarrierStrike", body)
        self.assertIn("ShadeCoverThreatGate(role, successor, coverOn)", body)
        self.assertIn("facts.guard = ShadeCoverOffTankGuard(role, coverOn, mainTankPct,", body)
        self.assertIn("facts.dropYd = member->GetPositionZ() - shade->GetPositionZ();", body)
        self.assertIn("miss == ShadeCoverMiss::Dead", body)
        self.assertIn("g_shadeCoverLog.Note(member, shade, miss, dead, nowMs)", ACTIONS)


if __name__ == "__main__":
    unittest.main()
