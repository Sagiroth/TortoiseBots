"""Raid entry by the commander (AiPlayerbot.RaidEntryByCommander), wired as the brief says.

runtime/RemoteCommanderEnter.h holds the pure rule (tested in
tools/test_remote_commander_enter.cpp): the waiver needs the key, a raid's entrance, no
area-trigger script, a real commander of the bot's raid who meets the condition. This
checks the glue in commands/Commander.cpp: the raid check and the script-id check gate
conditionWaived (a five-player dungeon and a scripted entrance never reach the commander's
condition), the waiver is assigned in one place, that place is the only one, and a waived
entry still goes in by the core's own Player::TeleportTo, which keeps the map's admission
checks. Also the key's code default equals the shipped template.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()
CONF_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONF_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()


def squash(text):
    return re.sub(r"\s+", " ", text).strip()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class RaidEntryByCommanderWiringTest(unittest.TestCase):
    def gather(self):
        return body(COMMANDER, "RemoteCommander::EntryFacts GatherEntryFacts(", "\n}\n")

    def test_raid_and_script_gate_the_waiver(self):
        gather = self.gather()
        self.assertIn("facts.raidMap = targetMap->IsDungeon() && targetMap->IsRaid();", squash(gather))
        self.assertIn("bool const unscriptedTrigger = sScriptMgr.GetAreaTriggerScriptId(triggerId) == 0;",
                      squash(gather))
        start = gather.index("if (facts.conditionFailed")
        gate = squash(gather[start:gather.index("{", start)])
        # A dungeon's condition is never waived, and neither is one whose trigger binds a script: the
        # core script and the fallback teleport would disagree about it.
        self.assertIn("facts.raidMap", gate)
        self.assertIn("unscriptedTrigger", gate)
        self.assertIn("sPlayerbotAIConfig.raidEntryByCommander", gate)
        # Both checks are read before the gate, so they cannot be bypassed by a later write.
        self.assertLess(gather.index("facts.raidMap = targetMap->IsDungeon() && targetMap->IsRaid();"),
                        start)
        self.assertLess(gather.index("sScriptMgr.GetAreaTriggerScriptId(triggerId)"), start)

    def test_waiver_passes_the_gates_to_the_policy(self):
        gather = squash(self.gather())
        self.assertIn("facts.conditionWaived = RemoteCommander::WaiveCondition("
                      "sPlayerbotAIConfig.raidEntryByCommander, facts.raidMap, unscriptedTrigger, "
                      "commanderIsRealPlayer, commanderOfRaid, commanderMeets);", gather)
        # One assignment only, so no other path can waive a condition.
        self.assertEqual(COMMANDER.count("conditionWaived ="), 1)

    def test_commander_of_a_bound_raid_counts(self):
        # `.bot commander bind ... raid` takes the commander out of the group, so membership alone never
        # holds for a bound raid: the binding that commands the bot is the second way in.
        gather = squash(self.gather())
        self.assertIn("bool const inRaid = commanderIsRealPlayer && group && group->isRaidGroup();", gather)
        self.assertIn("RemoteCommander::BindingCommandsBot(*binding, bot->GetObjectGuid().GetRawValue(), "
                      "group->GetId())", gather)
        # Only a raid group can have a commander of the raid (the bot's own group is the one the binding names).
        start = gather.index("bool const commanderOfRaid")
        self.assertIn("inRaid &&", squash(gather[start:gather.index(";", start)]))

    def test_waived_entry_uses_the_core_teleport(self):
        send = body(COMMANDER, "bool SendEnterTrigger(", "\n}\n")
        self.assertIn("if (facts.conditionWaived && bot->TeleportTo(tele->destination))", squash(send))
        # The trigger packet still goes first; the teleport is only the fallback for the waiver.
        self.assertLess(send.index("HandleAreaTriggerOpcode(packet)"), send.index("facts.conditionWaived"))

    def test_key_default_matches_template(self):
        self.assertIn("raidEntryByCommander = true;", CONF_H)
        self.assertIn('raidEntryByCommander = config.GetBoolDefault("AiPlayerbot.RaidEntryByCommander", true);',
                      CONF_CPP)
        self.assertIn("#AiPlayerbot.RaidEntryByCommander = 1", TEMPLATE)


if __name__ == "__main__":
    unittest.main()
