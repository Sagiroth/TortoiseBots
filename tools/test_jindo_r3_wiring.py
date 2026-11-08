"""Pins the glue of fix/jindo-r3 (t1106k): the threat successor that is not the holder (F2:
AiPlayerbot.DungeonRunThreatSuccessorNotHolder) and the two Jin'do log rows (F3: BossTacticsKillFirst reason=switch,
BossTacticsHex).

The rules are unit-tested in tools/test_threat_cap_policy.cpp (ThreatCapPickReference with notHolder, the Hex helpers)
and tools/test_boss_tactics_policy.cpp (CarrierSwitchSinceMs). These pins only prove the live code feeds them, behind the
key, and that the key ships off.
"""
import os
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as handle:
        return handle.read()


VALUES = read("ai/playerbot/strategy/values/ThreatValues.cpp")
POLICY = read("ai/playerbot/ThreatCapPolicy.h")
CONFIG_H = read("ai/playerbot/PlayerbotAIConfig.h")
CONFIG_CPP = read("ai/playerbot/PlayerbotAIConfig.cpp")
CONF = read("ai/playerbot/aiplayerbot.conf.dist.in")
DOCS = read("docs/play/boss-tactics.md")
DOCS_RUNS = read("docs/server/settings/dungeons-raids.md")
DOCS_ROWS = read("docs/play/refusal-rows.md")
CHANGELOG = read("CHANGELOG.md")


def member(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


class ThreatSuccessorNotHolder(unittest.TestCase):
    def test_the_key_ships_off(self):
        self.assertIn("bool dungeonRunThreatSuccessorNotHolder = false;", CONFIG_H)
        self.assertIn(
            'config.GetBoolDefault("AiPlayerbot.DungeonRunThreatSuccessorNotHolder", false)', CONFIG_CPP)
        self.assertIn("AiPlayerbot.DungeonRunThreatSuccessorNotHolder = 0", CONF)

    def test_holds_boss_reads_the_bosses_victim(self):
        # onBoss stays "this tank hits the boss"; holdsBoss is the other way round: the boss hits this tank.
        build = member(VALUES, "tanks.push_back(ThreatCapTank{", "});")
        self.assertIn("player->GetVictim() == target, target->GetVictim() == player", build)

    def test_the_pick_gets_the_key(self):
        self.assertIn(
            "ThreatCapPickReference(tanks, true, sPlayerbotAIConfig.dungeonRunThreatSuccessorNotHolder)", VALUES)

    def test_a_charmed_tank_never_joins_the_list(self):
        loop = member(VALUES, "for (std::size_t index = 0; index < roles.tanks.size(); ++index)", "tanks.push_back(")
        self.assertIn("!sServerFacade.IsAlive(player)", loop)
        self.assertIn("player->GetCharmerGuid()", loop)
        self.assertIn("continue;", loop)

    def test_the_comments_say_which_way_round(self):
        self.assertIn("bool holdsBoss = false; // the boss hits this tank", POLICY)
        self.assertIn('bool onBoss = false; // this tank hits the boss (its victim is the boss)', POLICY)

    def test_the_row_carries_the_holder(self):
        self.assertIn("holder=%s", VALUES)
        self.assertIn("ThreatCapHolderWord(pick.holder)", VALUES)

    def test_docs_and_changelog(self):
        self.assertIn("DungeonRunThreatSuccessorNotHolder", DOCS)
        self.assertIn("DungeonRunThreatSuccessorNotHolder", DOCS_RUNS)
        self.assertIn("holder=<main\\|off\\|none>", DOCS_ROWS)
        self.assertIn("DungeonRunThreatSuccessorNotHolder", CHANGELOG)


if __name__ == "__main__":
    unittest.main()
