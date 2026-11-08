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


ACTIONS = read("ai/playerbot/strategy/actions/BossTacticsActions.cpp")
BOSS_POLICY = read("ai/playerbot/BossTacticsPolicy.h")
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


class KillFirstSwitchRow(unittest.TestCase):
    def test_the_helper_is_pure_and_used(self):
        self.assertIn("inline std::uint32_t CarrierSwitchSinceMs(std::uint32_t firstSeenMs, std::uint32_t nowMs)",
                      BOSS_POLICY)
        log = member(ACTIONS, "void LogCarrierSwitch(", "\n}\n")
        self.assertIn("CarrierSwitchSinceMs(known->second.firstMs, WorldTimer::getMSTime())", log)
        self.assertIn('"reason=switch"', log)
        self.assertIn("pick=%u:%u,from=%u,since_ms=%u", log)

    def test_one_row_per_bot_and_shade(self):
        log = member(ACTIONS, "void LogCarrierSwitch(", "\n}\n")
        self.assertIn("known->second.logged", log)
        self.assertIn("std::map<std::pair<uint64, uint64>, ShadeSeen> g_shadeSeen;", ACTIONS)

    def test_the_action_logs_after_a_successful_attack_on_a_carrier_row_shade(self):
        action = member(ACTIONS, "bool BossTacticsKillFirstAction::Execute(Event& event)", "\n}\n")
        attack = action.index("if (!Attack(bot, pick))")
        row = action.index("LogCarrierSwitch(ai, bot, pick, current);")
        self.assertGreater(row, attack)
        self.assertIn("if (IsCarrierRowEntry(fight, pick->GetEntry()))", action[attack:row])

    def test_the_pick_syncs_first_sight_and_idles_for_free(self):
        pick = member(ACTIONS, "Unit* PickBossKillFirst(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight, "
                      "KillFirstPickMode mode,\n    float carrierReachYd)\n{", "\n}\n")
        self.assertIn("SyncShadesSeen(bot, shadeGuids, shadeCount);", pick)
        sync = member(ACTIONS, "void SyncShadesSeen(", "\n}\n")
        # No Shade and an empty map: one atomic load, before the clock read, the lock or any container work.
        self.assertLess(sync.index("g_shadeSeenCount.load(std::memory_order_relaxed) == 0"), sync.index("lock_guard"))
        self.assertIn("it = g_shadeSeen.erase(it);", sync)
        self.assertIn("kShadeSeenMaxEntries", sync)

    def test_docs_and_changelog(self):
        self.assertIn("reason=switch", DOCS)
        self.assertIn("since_ms", DOCS)
        self.assertIn("reason=switch", DOCS_ROWS)


class HexSnapshotRow(unittest.TestCase):
    def test_the_snapshot_is_taken_on_the_world_thread_read_of_the_boss_threat(self):
        tank_threat = member(VALUES, "float ThreatValue::GetTankThreat(", "\n}\n")
        self.assertIn("LogHexSnapshot(ai, target, roles, group);", tank_threat)

    def test_the_row_fields(self):
        log = member(VALUES, "void LogHexSnapshot(", "\n}\n")
        self.assertIn('"holder=%s ot_threat=%.0f mt_threat=%.0f top_dps=%s:%.0f"', log)
        self.assertIn('"BossTacticsHex"', log)
        self.assertIn("ai->HasAura(kJindoHexSpellId, tank)", log)
        self.assertIn("HexSnapshotDue(last, nowMs)", log)
        self.assertIn("sPlayerbotAIConfig.hasLog(\"bot_events.csv\")", log)

    def test_no_flag_gates_the_row(self):
        log = member(VALUES, "void LogHexSnapshot(", "\n}\n")
        self.assertNotIn("sPlayerbotAIConfig.dungeonRun", log)

    def test_docs(self):
        self.assertIn("BossTacticsHex", DOCS)
        self.assertIn("BossTacticsHex", DOCS_ROWS)
        self.assertIn("top_dps", DOCS)


if __name__ == "__main__":
    unittest.main()
