"""Wiring of the raid config keys AiPlayerbot.DungeonRunRaid and AiPlayerbot.DungeonRunRaidBossRules."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
HEADER = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class RaidConfigWiringTest(unittest.TestCase):
    def test_keys_default_off(self):
        keys = body(CONFIG, "void PlayerbotAIConfig::LoadDungeonRunKeys()", "\n}\n")
        self.assertIn('dungeonRunRaid = config.GetBoolDefault("AiPlayerbot.DungeonRunRaid", false);', keys)
        self.assertIn("bool dungeonRunRaid", HEADER)
        self.assertIn("AiPlayerbot.DungeonRunRaid = 0", TEMPLATE)
        self.assertIn("AiPlayerbot.DungeonRunRaid", SETTINGS)

    def test_boss_rules_key_default_off_and_fed_to_the_raid_facts(self):
        # AiPlayerbot.DungeonRunRaidBossRules: a raid's boss fight ceiling of 1200 s, deaths that do not pause it while
        # it loses hp, and the off-tanks' pick-ups (DungeonRun::RaidBossRulesFight). Off by default: dev's raid boss
        # fight is unchanged unless it is set.
        keys = body(CONFIG, "void PlayerbotAIConfig::LoadDungeonRunKeys()", "\n}\n")
        self.assertIn(
            'dungeonRunRaidBossRules = config.GetBoolDefault("AiPlayerbot.DungeonRunRaidBossRules", false);', keys)
        self.assertIn("bool dungeonRunRaidBossRules = false;", HEADER)
        self.assertIn("AiPlayerbot.DungeonRunRaidBossRules = 0", TEMPLATE)
        self.assertIn("### `AiPlayerbot.DungeonRunRaidBossRules`", SETTINGS)
        self.assertIn("[`AiPlayerbot.DungeonRunRaidBossRules`](#aiplayerbotdungeonrunraidbossrules) | `0`", SETTINGS)
        code = strip_comments(COMMANDER)
        raid = body(code, "    if (raid && sPlayerbotAIConfig.dungeonRunRaid)", "\n    }\n")
        self.assertIn("f.raid.on = true;", raid)
        self.assertIn("f.raid.bossRules = sPlayerbotAIConfig.dungeonRunRaidBossRules;", raid)


    def test_guard_read_fills_the_raid_attackers_with_the_lead_dead(self):
        # DungeonRun::RaidGuardTick's dead-lead branch needs Facts::partyAttackers: in a raid's boss fight the glue's
        # guard read goes on past a dead (or non-tank) lead bot, around a living anchor (DungeonRun::GuardReadMode).
        code = strip_comments(COMMANDER)
        read = body(code, "void ReadGuardFacts(Player* lead,", "\n}\n")
        self.assertIn("ai::PlanStep const* hold, bool raidBoss)", read)
        mode = read.index("GuardRead const mode = GuardReadMode(lead->IsAlive(), f.leadIsTank, raidBoss);")
        fill = read.index("f.partyAttackers.push_back(fact);")
        self.assertLess(mode, fill)
        before = read[:mode]
        # No return on a dead or non-tank lead bot before the mode decides.
        self.assertNotIn("!lead->IsAlive()", before)
        self.assertNotIn("if (!f.leadIsTank)", read)
        self.assertIn("if (mode == GuardRead::None)\n        return;", read)
        # The attackers are read around the anchor, within the raid boss range when it is one.
        self.assertIn("float const range = raidBoss ? kRaidBossGuardYd : kGuardYd;", read)
        self.assertIn("bot->GetDistance(anchor) > range", read)
        self.assertIn("fact.distLead = anchor->GetDistance(attacker);", read)
        self.assertIn("for (std::string const& name : f.raid.bossOffTanksAlive)", read)
        self.assertIn("fact.victimName = victim->GetName();", read)
        self.assertIn("f.leadHeldEntries.push_back(attacker->GetEntry());", read)
        # Both call sites say whether it is a raid's boss fight.
        self.assertEqual(code.count("TortoiseBots::DungeonRun::RaidBossRulesFight(slot.run, f)"), 2)

    def test_boss_off_tanks_are_raid_wide(self):
        code = strip_comments(COMMANDER)
        self.assertIn("yd <= TortoiseBots::DungeonRun::kRaidBossGuardYd &&", code)
        self.assertIn("f.raid.bossOffTanksAlive.push_back(member.name);", code)
        # The trash fights' off-tanks keep kGuardYd.
        self.assertIn("bot->GetMap() == lead->GetMap() && yd <= kGuardYd &&", code)


if __name__ == "__main__":
    unittest.main()
