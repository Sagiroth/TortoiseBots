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


    def test_healer_guard_key_default_off_and_fed_to_the_raid_facts(self):
        # AiPlayerbot.DungeonRunRaidHealerGuard: in a raid boss fight whose main tank holds the boss an add on a healer
        # goes to the nearest ranged dps first (DungeonRun::RaidHealerGuard); off by default, the off-tanks take it.
        # Only read inside a raid boss fight (DungeonRunRaidBossRules).
        keys = body(CONFIG, "void PlayerbotAIConfig::LoadDungeonRunKeys()", "\n}\n")
        self.assertIn(
            'dungeonRunRaidHealerGuard = config.GetBoolDefault("AiPlayerbot.DungeonRunRaidHealerGuard", false);', keys)
        self.assertIn("bool dungeonRunRaidHealerGuard = false;", HEADER)
        self.assertIn("AiPlayerbot.DungeonRunRaidHealerGuard = 0", TEMPLATE)
        self.assertIn("### `AiPlayerbot.DungeonRunRaidHealerGuard`", SETTINGS)
        self.assertIn("[`AiPlayerbot.DungeonRunRaidHealerGuard`](#aiplayerbotdungeonrunraidhealerguard) | `0`", SETTINGS)
        code = strip_comments(COMMANDER)
        raid = body(code, "    if (raid && sPlayerbotAIConfig.dungeonRunRaid)", "\n    }\n")
        self.assertIn("f.raid.healerGuard = sPlayerbotAIConfig.dungeonRunRaidHealerGuard;", raid)
        # The ranged dps the guard sends (never one busy with a commander task, as the off-tank lists are not) and the
        # add's own place, both read by the glue.
        self.assertIn(
            "member.rangedDps = f.raid.on && !isTank && !isHeal && memberAi && memberAi->IsRanged(bot) &&\n"
            "            !CommanderTasks().count(bot->GetObjectGuid().GetRawValue());",
            code)
        # Its current target: an assignment whose bot never targets the add is over (DungeonRun::RaidHealerGuard).
        self.assertIn("member.targetGuid = bot->GetSelectionGuid().GetRawValue();", code)
        # IssueRunOrder's answer, fed back to the guard: only a taken order is an assignment (HealerGuardOrderTaken).
        self.assertIn("TortoiseBots::DungeonRun::HealerGuardOrderTaken(it->second.run, order, accepted);", code)
        read = body(code, "void ReadGuardFacts(Player* lead,", "\n}\n")
        self.assertIn("fact.x = attacker->GetPositionX();", read)
        self.assertIn("fact.y = attacker->GetPositionY();", read)

    def test_guard_read_fills_the_raid_attackers_with_the_lead_dead(self):
        # DungeonRun::RaidGuardTick's dead-lead branch needs Facts::partyAttackers: in a raid's boss fight the glue's
        # guard read goes on past a dead (or non-tank) lead bot, around a living anchor (DungeonRun::GuardReadMode).
        # A raid's trash fight whose off-tanks pick up (DungeonRun::RaidTrashOffTanks) is the other case: the mobs a
        # dead main tank left on the healers are still the off-tanks' to take (DungeonRun::GuardTarget's dead-lead
        # branch), which only works if the glue reads the raid's attackers then too. A party run, a dead lead bot
        # with no off-tank, and every other fight keep reading nothing.
        code = strip_comments(COMMANDER)
        read = body(code, "void ReadGuardFacts(Player* lead,", "\n}\n")
        self.assertIn("ai::PlanStep const* hold, bool raidBoss, bool raidTrashOffTanks, bool raidWalk = false)", read)
        mode = read.index(
            "GuardRead const mode = GuardReadMode(lead->IsAlive(), f.leadIsTank, raidBoss, raidTrashOffTanks);")
        fill = read.index("f.partyAttackers.push_back(fact);")
        self.assertLess(mode, fill)
        before = read[:mode]
        # No return on a dead or non-tank lead bot before the mode decides.
        self.assertNotIn("!lead->IsAlive()", before)
        self.assertNotIn("if (!f.leadIsTank)", read)
        self.assertIn("if (mode == GuardRead::None)\n        return;", read)
        # The attackers are read around the anchor, within the raid boss range when it is one.
        self.assertIn("float const range = raidBoss ? kRaidBossGuardYd : kGuardYd;", read)
        self.assertIn("botYd = bot == anchor ? 0.0f : bot->GetDistance(anchor);", read)
        self.assertIn("float const mobYd = anchor->GetDistance(attacker);", read)
        self.assertIn("fact.victimName = victim->GetName();", read)
        self.assertIn("f.leadHeldEntries.push_back(attacker->GetEntry());", read)
        self.assertIn("if (bot != lead || mode == GuardRead::RaidAnchor)", read)
        # Both call sites say whether it is a raid's boss fight, and whether it is a raid's trash fight whose
        # off-tanks pick up (the same rule GuardTarget's dead-lead branch reads: DungeonRun::RaidTrashOffTanks).
        self.assertEqual(code.count("TortoiseBots::DungeonRun::RaidBossRulesFight(slot.run, f)"), 2)
        self.assertEqual(code.count("TortoiseBots::DungeonRun::RaidTrashOffTanks(slot.run, f)"), 2)
        self.assertIn("bool const raidTrashOffTanks = TortoiseBots::DungeonRun::RaidTrashOffTanks(slot.run, f);", code)
        self.assertIn("TortoiseBots::DungeonRun::RaidTrashOffTanks(slot.run, f), raidWalk);", code)
        self.assertIn("ReadGuardFacts(lead, bots, f, hold, raidBoss, raidTrashOffTanks);", code)
        self.assertIn("Player* const anchor = RaidGuardAnchor(lead, bots, f);", read)

    def test_boss_off_tanks_are_raid_wide(self):
        code = strip_comments(COMMANDER)
        self.assertIn("(yd <= TortoiseBots::DungeonRun::kRaidBossGuardYd || !lead->IsAlive()) &&", code)
        self.assertIn("f.raid.bossOffTanksAlive.push_back(member.name);", code)
        # The trash fights' off-tanks keep kGuardYd, and are not measured from a dead lead bot either: the fight may
        # have moved off its body (or the lead bot released), and the pick-ups must not switch themselves off there.
        self.assertIn("(yd <= kGuardYd || !lead->IsAlive()) &&", code)
        self.assertIn("f.raid.offTanksAlive.push_back(member.name);", code)
        # With the lead bot dead the anchor the attackers are read around is one of the boss fight's off-tanks, then
        # one of the trash fight's, then any living bot on its map.
        anchor = body(code, "Player* RaidGuardAnchor(Player* lead,", "\n}\n")
        # ... but first, with the lead bot dead, the member the fight is around (an off-tank in combat, else a member
        # with attackers: DungeonRun::DeadLeadGuardAnchor), never the first off-tank in the list: an idle one far off
        # hid the loose mobs. The first-in-list loops stay as the fallback when nobody fights.
        self.assertIn("DeadLeadGuardAnchor(f)", anchor)
        self.assertLess(anchor.index("DeadLeadGuardAnchor(f)"), anchor.index("f.raid.bossOffTanksAlive"))
        # ... and the off-tanks enrolled with the lead bot dead are only those in the fight, dropped once the
        # members are read and before the guard read asks whether any is alive.
        self.assertIn("if (f.raid.on && !lead->IsAlive())\n        TortoiseBots::DungeonRun::DropIdleDeadLeadOffTanks(f);", code)
        self.assertLess(code.index("DropIdleDeadLeadOffTanks(f);"), code.index("ReadGuardFacts(lead, bots, f,\n"))
        self.assertLess(anchor.index("f.raid.bossOffTanksAlive"), anchor.index("f.raid.offTanksAlive"))
        self.assertLess(anchor.index("f.raid.offTanksAlive"), anchor.rindex("for (Player* bot : bots)"))


if __name__ == "__main__":
    unittest.main()
