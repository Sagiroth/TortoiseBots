"""Task A5: the run leader's raid rules (runtime/DungeonRunPolicy.h + the glue).

The policy scales with the raid: the gather, the wipe level and the rest
floor come from runtime/RaidLayoutPolicy.h. The glue (commands/Commander.cpp,
ReadRunFacts) reads the raid's tanks, healers, gather spread and rest floor
only for a raid binding with AiPlayerbot.DungeonRunRaid = 1.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
COMMANDS = (ROOT / "commands/Commander.cpp").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body_from(text, start):
    i = text.index(start)
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i:j]
    return text[i:]


SRC = strip_comments(COMMANDS)


class RaidRunWiringTest(unittest.TestCase):
    def test_read_run_facts_reads_the_raid(self):
        glue = body_from(SRC, "void ReadRunFacts(")
        self.assertIn("f.raid.on = true;", glue)
        self.assertIn("RaidLayout::RestFloor(", glue)
        self.assertIn("member.core = isTank || isHeal;", glue)
        tick = strip_comments(COMMANDS)
        self.assertIn("ReadRunFacts(slot, lead, bots, facts, binding->raid);", tick)
        self.assertIn("ReadRunFacts(slot, holdLead, holdBots, holdFacts, binding->raid);", tick)

    def test_party_rules_use_the_raid_helpers(self):
        for old in ("f.dead < kWipeDead", "f.dead >= kWipeDead", "partyDead >= kWipeDead",
                    "partyDead < kWipeDead", "f.maxMemberYd > kGatherYd"):
            self.assertNotIn(old, strip_comments(POLICY))


if __name__ == "__main__":
    unittest.main()
