"""Wiring pins for AiPlayerbot.DungeonRunFactJudgeCharmedLead (ZG walk 57 F1).

A lead bot charmed by Jin'do's Brain Wash (or any charm) is friendly to its charmer's side, so the run leader's fact
read, judged against him, dropped the Brain Wash Totem as "friendly" and the totem focus went silent (walk 57: 16 of 19
main-tank charm windows of 10 s or more had no TotemFocus row). With the key on, while the lead is charmed and an
uncharmed judge exists (RunJudge), every site below judges against that member instead. The decision is pure in
runtime/DungeonRunPolicy.h (RunFactJudgeReplacesLead) and tested in tools/test_dungeon_run_policy.cpp
(charmed_lead_totem_stays_a_fact). These pins prove each site goes through the helper:

- ReadKillFirst: the kill_first friendliness test and the owner's engagement (fact.ownerEngaged);
- ReadCcBreakFacts: the crowd-control hold friendliness test;
- HandleCommanderOrder: the order's judge (ResolveOrderTarget refused the run's totem focus as "friendly");
- CommanderFocus: the totem focus' selection owner (TotemFocusBot).

Run: python3 -m unittest tools/test_fact_judge_charmed_lead_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = (ROOT / "commands/Commander.cpp").read_text()
POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
POLICY_TEST = (ROOT / "tools/test_dungeon_run_policy.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONF_DIST = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
TACTICS = (ROOT / "docs/play/boss-tactics.md").read_text()

HELPER = "TortoiseBots::DungeonRun::RunFactJudgeReplacesLead("
HELPER_ARGS = re.compile(
    r"TortoiseBots::DungeonRun::RunFactJudgeReplacesLead\(\s*sPlayerbotAIConfig\.dungeonRunFactJudgeCharmedLead,\s*"
    r"(?P<lead>[A-Za-z_.>-]+)->IsCharmed\(\),\s*(?P<judge>[A-Za-z_]+) != nullptr\)")
FRIENDLY = re.compile(r"sServerFacade\.IsFriendlyTo\(\s*[^,]+,\s*([A-Za-z_.>-]+)\s*\)")


def body(start, end="\n}\n"):
    i = COMMANDS.index(start)
    return COMMANDS[i:COMMANDS.index(end, i + len(start))]


def helper_calls(text):
    return [(m.group("lead"), m.group("judge")) for m in HELPER_ARGS.finditer(text)]


class Helper(unittest.TestCase):
    def test_helper_is_pure_and_generic(self):
        i = POLICY.index("inline bool RunFactJudgeReplacesLead(bool keyOn, bool leadCharmed, bool judgeFound)")
        text = POLICY[i:POLICY.index("\n}\n", i)]
        self.assertIn("return keyOn && leadCharmed && judgeFound;", text)
        self.assertNotRegex(text, r"\d{3,}")  # no boss ids: any charm on the lead, every raid

    def test_named_case_runs(self):
        self.assertIn("static void charmed_lead_totem_stays_a_fact()", POLICY_TEST)
        self.assertIn("    charmed_lead_totem_stays_a_fact();\n", POLICY_TEST)
        self.assertIn("    TestRunFactJudgeKeepsTheLeadOtherwise();\n", POLICY_TEST)

    def test_every_helper_call_is_a_named_site(self):
        # Four sites: the kill_first read, the CC-hold read, the order judge and the totem focus owner. A new call
        # must be added to a named test below and to this count.
        self.assertEqual(COMMANDS.count(HELPER), 4)
        self.assertEqual(len(helper_calls(COMMANDS)), 4)


class Sites(unittest.TestCase):
    def test_read_kill_first(self):
        text = body("void ReadKillFirst(ai::DungeonPlan const& plan, Player* lead, std::vector<Player*> const& bots,")
        self.assertEqual(helper_calls(text), [("lead", "judge")])
        self.assertIn("Player* const judge = RunJudge(lead, bots);", text)
        self.assertIn("judge != nullptr) ? judge : lead;", text)
        self.assertIn("if (plan.KillFirstRank(unit->GetEntry()) < 0 || sServerFacade.IsFriendlyTo(unit, factLead))",
            text)
        self.assertIn("fact.ownerEngaged = summoner && !sServerFacade.IsFriendlyTo(summoner, factLead) &&", text)
        # Every friendliness test of the read goes through factLead: a new one against the raw lead fails here.
        self.assertEqual(set(FRIENDLY.findall(text)), {"factLead"})

    def test_read_cc_break_facts(self):
        text = body("void ReadCcBreakFacts(DungeonRunSlot const& slot, Player* lead, std::vector<Player*> const& bots,")
        self.assertEqual(helper_calls(text), [("lead", "judge")])
        self.assertIn("judge != nullptr) ? judge : lead;", text)
        self.assertIn("!unit->IsInCombat() || sServerFacade.IsFriendlyTo(unit, factLead) ||", text)
        self.assertEqual(set(FRIENDLY.findall(text)), {"factLead"})

    def test_order_judge(self):
        text = body("bool HandleCommanderOrder(ChatHandler* handler, char const* args)")
        self.assertEqual(helper_calls(text), [("order.lead", "charmJudge")])
        i = text.index("order.judge = order.lead;")
        j = text.index("Player* const charmJudge = RunJudge(order.lead, order.bots);")
        k = text.index("order.judge = charmJudge;")
        self.assertLess(i, j)
        self.assertLess(j, k)
        # The order's target check reads the judge, so a charmed lead no longer refuses the totem as friendly.
        resolve = body("static Unit* ResolveOrderTarget(ChatHandler* handler, std::string const& intent,")
        self.assertIn("facts.friendly = sServerFacade.IsFriendlyTo(unit, order.judge);", resolve)

    def test_totem_focus_owner(self):
        text = body("static bool CommanderFocus(ChatHandler* handler, CommanderOrder const& order, Unit* target,")
        self.assertEqual(helper_calls(text), [("order.lead", "charmJudge")])
        self.assertIn("Player* totemOwner = order.lead;", text)
        self.assertIn("totemOwner = charmJudge;", text)
        calls = re.findall(r"TotemFocusBot\(ai, bot, ([A-Za-z_.]+),", text)
        self.assertEqual(calls, ["totemOwner"])  # never the raw lead

    def test_run_judge_declared_before_use(self):
        self.assertLess(COMMANDS.index("Player* RunJudge(Player* lead, std::vector<Player*> const& bots); //"),
            COMMANDS.index("static bool CommanderFocus(ChatHandler* handler, CommanderOrder const& order"))


class Key(unittest.TestCase):
    def test_key_defaults_off(self):
        self.assertIn("bool dungeonRunFactJudgeCharmedLead = false;", CONFIG_H)
        self.assertIn('dungeonRunFactJudgeCharmedLead = config.GetBoolDefault('
            '"AiPlayerbot.DungeonRunFactJudgeCharmedLead", false);', CONFIG_CPP)
        self.assertIn("\nAiPlayerbot.DungeonRunFactJudgeCharmedLead = 0\n", CONF_DIST)

    def test_documented_recommended_on(self):
        self.assertIn("### `AiPlayerbot.DungeonRunFactJudgeCharmedLead`", SETTINGS)
        i = SETTINGS.index("### `AiPlayerbot.DungeonRunFactJudgeCharmedLead`")
        self.assertIn("Recommended on", SETTINGS[i:i + 1200])
        self.assertIn("AiPlayerbot.DungeonRunFactJudgeCharmedLead", TACTICS)
        i = CONF_DIST.index("AiPlayerbot.DungeonRunFactJudgeCharmedLead = 0")
        self.assertIn("recommended on", CONF_DIST[CONF_DIST.rindex("\n\n", 0, i):i])


if __name__ == "__main__":
    unittest.main()
