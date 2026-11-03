"""Wiring pins for the dashboard's boss list (owner request 2026-10-21: every boss
on the dashboard map, alive or dead, all the time).

The pure rules live in runtime/CommanderTelemetry.h (BossSpawnState,
StrongerBossState, GroupBossSpawns, BossKillLatch and the `bosses` fields of
BuildPayload, unit-tested in tools/test_commander_telemetry.cpp). These pins
prove the live code calls them: the spawn cache is built once at server start,
right after the plans are sealed and never inside a commander snapshot (Codex
review 2026-10-22), the commands layer fills the list for every standing binding
from it, the state is read on the lead's own map (its instance) and held by the
killed latch of that instance, and the emitter copies the list into the datagram.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CMD = (ROOT / "commands/Commander.cpp").read_text()
EMIT = (ROOT / "runtime/ObservabilityEmitter.cpp").read_text()
HOST = (ROOT / "host/BotHostAdapter.cpp").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class BossListWiring(unittest.TestCase):
    def test_map_data_fills_the_boss_list(self):
        fn = body(CMD, "static void FillCommanderMapData(")
        self.assertIn("FillCommanderBosses(lead, ref);", fn)

    def test_the_list_is_read_on_the_leads_own_dungeon_map(self):
        fn = body(CMD, "static void FillCommanderBosses(")
        self.assertIn("Map* map = lead->GetMap();", fn)
        self.assertRegex(fn, r"if \(!map \|\| !map->IsDungeon\(\)\)\s*return;")
        self.assertIn("ref.bossesReported = true;", fn)
        self.assertIn("ref.bossesMap = lead->GetMapId();", fn)
        # the state of each spawn: the creature on THIS map (instance), else the instance's respawn time
        self.assertIn("map->GetCreature(", fn)
        self.assertIn("map->GetPersistentState()", fn)
        self.assertIn("persistent->GetCreatureRespawnTime(dbGuid)", fn)
        # the decision is the tested pure helper, handed the creature's own state and the respawn time against now, per spawn row of
        # the entry, the strongest read kept
        self.assertIn("CommanderTelemetry::BossSpawnState(creature != nullptr, creature && creature->IsAlive(), respawnAt, static_cast<int64_t>(now))", fn)
        self.assertIn("state = CommanderTelemetry::StrongerBossState(state, ", fn)
        self.assertRegex(fn, r"for \(uint32 dbGuid : boss\.dbGuids\)")
        self.assertNotIn("CommanderTelemetry::BossState(", fn)
        self.assertNotIn("MergeBosses", fn)
        # the killed latch of the lead's instance decides what is reported; instances no binding stands on are forgotten
        self.assertIn("uint32 const instanceId = map->GetInstanceId();", fn)
        self.assertIn("kills.Expire(static_cast<int64_t>(now));", fn)
        self.assertIn("ref.bosses.back().state = kills.Apply(instanceId, boss.info.entry, state, static_cast<int64_t>(now));", fn)
        self.assertLess(fn.index("kills.Expire("), fn.index("kills.Apply("))

    def test_no_grid_or_world_scan_per_report(self):
        fn = body(CMD, "static void FillCommanderBosses(")
        for scan in ("GetCreatureListWithEntryInGrid", "DoCreatureData", "Cell::Visit", "VisitAll"):
            self.assertNotIn(scan, fn)

    def test_the_spawn_cache_is_built_once_after_the_plans_are_sealed(self):
        fn = body(CMD, "void BuildBossSpawnCache()")
        self.assertRegex(fn, r"if \(cache\.built \|\| !plans\.Sealed\(\)\)\s*return;")
        self.assertEqual(fn.count("sObjectMgr.DoCreatureData("), 1)
        self.assertIn("cache.built = true;", fn)
        # the same boss rule as the mob rank letter (RankKind): a plan boss / ai_dungeon_boss row, world-boss rank, instance bind
        self.assertIn("plans.IsBossEntry(", fn)
        self.assertIn("CREATURE_ELITE_WORLDBOSS", fn)
        self.assertIn("CREATURE_FLAG_EXTRA_INSTANCE_BIND", fn)
        # plan boss steps mark the run's bosses, and a boss step with no spawn row still gets a place
        self.assertIn("ai::PlanStepKind::Boss", fn)
        # pre-merged by (map, entry) once, so a report never searches its output per spawn
        self.assertIn("CommanderTelemetry::GroupBossSpawns(", fn)
        # measured once, and every map whose roster the payload cap leaves out is named
        self.assertIn("std::chrono::steady_clock::now()", fn)
        self.assertIn('"TortoiseBots: boss spawn cache built in %u ms for %u maps"', fn)
        self.assertIn("> CommanderTelemetry::kMaxBossesPerBinding", fn)
        self.assertIn("sLog.outString(", fn)

    def test_the_cache_is_built_at_startup_never_in_a_snapshot(self):
        start = body(HOST, "void BotHostAdapter::OnStartup()")
        self.assertIn("BotCommands::BuildBossSpawnCache();", start)
        # after the plans are sealed (sPlayerbotAIConfig.Initialize loads and seals them)
        self.assertLess(start.index("sPlayerbotAIConfig.Initialize();"), start.index("BotCommands::BuildBossSpawnCache();"))
        # only for the dungeon features that read it; everyone else skips the full-table scan
        self.assertIn("if (sPlayerbotAIConfig.dungeonTactics || sPlayerbotAIConfig.dungeonSelfClear)", start)
        self.assertLess(start.index("if (sPlayerbotAIConfig.dungeonTactics || sPlayerbotAIConfig.dungeonSelfClear)"),
                        start.index("BotCommands::BuildBossSpawnCache();"))
        self.assertIn("void BuildBossSpawnCache();", (ROOT / "commands/BotCommands.h").read_text())
        # the snapshot path only reads the cache: no build, no scan
        for name in ("static std::vector<CommanderTelemetry::BossEntrySpawns> const* DashBossSpawnsOn(",
                     "static void FillCommanderBosses(", "static void FillCommanderMapData(", "void CollectCommanderTelemetry("):
            fn = body(CMD, name)
            self.assertNotIn("BuildBossSpawnCache", fn, name)
            self.assertNotIn("DoCreatureData", fn, name)
        # Besides its definition, only `.bot run reloadplans` builds it again, after the plan tables were replaced
        # (tools/test_reload_plans_wiring.py): never a snapshot or a tick.
        self.assertEqual(CMD.count("BuildBossSpawnCache()"), 2, "its definition and RebuildBossSpawnCache only")
        self.assertIn("BuildBossSpawnCache();", body(CMD, "static bool RebuildBossSpawnCache(uint32& ms, uint32& maps)\n{"))
        self.assertEqual(CMD.count("RebuildBossSpawnCache(ms, maps)"), 1)
        self.assertIn("RebuildBossSpawnCache(ms, maps)", body(CMD, "static bool HandleReloadPlans("))
        fn = body(CMD, "static std::vector<CommanderTelemetry::BossEntrySpawns> const* DashBossSpawnsOn(")
        self.assertRegex(fn, r"if \(!cache\.built\)\s*return nullptr;")

    def test_the_emitter_copies_the_list(self):
        self.assertIn("info.bossesReported = ref.bossesReported;", EMIT)
        self.assertIn("info.bossesMap = ref.bossesMap;", EMIT)
        self.assertIn("info.bosses = ref.bosses;", EMIT)

    def test_full_school_immunity_reads_as_i(self):
        fn = body(CMD, "static void FillMobState(")
        self.assertIn("SPELL_AURA_SCHOOL_IMMUNITY", fn)
        self.assertIn("uint32 const immuneMask = CommanderTelemetry::FoldImmuneSchools(immuneMasks);", fn)
        self.assertIn("|| CommanderTelemetry::ImmuneToAllSchools(immuneMask),", fn)


if __name__ == "__main__":
    unittest.main()
