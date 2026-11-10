"""Wiring pins for the engaged boss registry (AiPlayerbot.BossTacticsKeepsEngagedBoss).

The rules live in ai/playerbot/BossEngagedPolicy.h (pure, tested by
tools/test_boss_engaged_policy.cpp). These pins prove the live code calls them:

 - the fight scan notes every boss its attacker list shows and appends the
   registry's bosses to the fight exactly as the walk-out kept boss;
 - the stand lookup reads the registry too, after the attacker list;
 - only a unit in combat whose entry has boss-tactic or boss-stand rows is noted;
 - a bucket whose map instance is gone (or whose rows all expired) is swept at most
   once a minute from the note path, under the registry lock;
 - the key is read, documented and off by default.

Run: python3 -m unittest tools/test_engaged_boss_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
AI = ROOT / "ai/playerbot"
ACTIONS = (AI / "strategy/actions/BossTacticsActions.cpp").read_text()
POLICY = (AI / "BossEngagedPolicy.h").read_text()
CONFIG = (AI / "PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (AI / "aiplayerbot.conf.dist.in").read_text()
SETTINGS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class EngagedBossWiring(unittest.TestCase):
    def test_fight_scan_adds_registered_boss(self):
        fn = body(ACTIONS, "BossTacticsFight ai::ScanBossTactics(")
        # Every living attacker is noted, and the registry read appends its units to the fight.
        self.assertLess(fn.index("NoteEngagedBoss(bot, unit, mapId);"), fn.index("fight.units.push_back(unit);"))
        registry = fn.index("RegisteredEngagedBosses(bot, seen)")
        self.assertIn("for (Creature* const registered : RegisteredEngagedBosses(bot, seen))", fn)
        self.assertLess(fn.index("fight.units.push_back(unit);"), registry)
        # Exactly the walk-out kept-boss append: the fight's unit list and the entry's unit list.
        self.assertIn("fight.units.push_back(registered);", fn)
        self.assertIn("entryUnits[known - seen.begin()].push_back(registered);", fn)
        self.assertLess(registry, fn.index("entryUnits[known - seen.begin()].push_back(registered);"))
        self.assertLess(fn.index("entryUnits[known - seen.begin()].push_back(registered);"),
                        fn.index("addRows(*rows, bossHpPct);"))

    def test_find_boss_stand_reads_registry(self):
        fn = body(ACTIONS, "BossStandTarget ai::FindBossStand(")
        self.assertIn("NoteEngagedBoss(bot, unit, mapId);", fn)
        self.assertIn("readEntries.push_back(unit->GetEntry());", fn)
        self.assertIn("RegisteredEngagedBosses(bot, readEntries)", fn)
        # The registry read only runs when the attacker list opened no window, and it feeds the same read.
        self.assertLess(fn.index("if (readStandUnit(unit))"), fn.index("if (!openedWindow)"))
        self.assertLess(fn.index("if (!openedWindow)"), fn.index("readStandUnit(registered)"))
        self.assertIn("const auto readStandUnit = [&](Unit* unit)", fn)

    def test_note_only_for_units_with_rows(self):
        fn = body(ACTIONS, "void NoteEngagedBoss(Player* bot, Unit* unit, std::uint32_t mapId)")
        self.assertIn("if (!bot || !unit || !sPlayerbotAIConfig.bossTacticsKeepsEngagedBoss || !unit->IsInCombat())", fn)
        self.assertIn("EngagedBossEligible(GetBossStands().Rows(mapId, entry) != nullptr,", fn)
        self.assertIn("GetBossTactics().HasKindFor(mapId, entry, BossTacticKind::TankPost)))", fn)
        # The note and its expiry rules live in the policy's instance bucket; the clock is read under the registry lock.
        # A repeat note of the same (bot, entry) inside the note window skips the lock: one thread-local mark, no state.
        self.assertIn("mark.bot == botGuid && mark.entry == entry && markMs - mark.ms < kEngagedBossNoteMs", fn)
        self.assertLess(fn.index("markMs - mark.ms < kEngagedBossNoteMs"), fn.index("std::lock_guard"))
        self.assertIn("thread_local EngagedNoteMark t_engagedNoteMarks[kEngagedNoteMarks];", ACTIONS)
        self.assertIn("g_engagedBosses.Note(mapId, instanceId, entry, guid, nowMs);", fn)
        self.assertLess(fn.index("std::lock_guard"), fn.index("const std::uint32_t nowMs = WorldTimer::getMSTime();"))
        self.assertIn("EngagedBossNoteDue(have, nowMs, have ? note->second.lastSeenMs : 0)", POLICY)
        self.assertIn("static_cast<std::uint32_t>(nowMs - old->second.lastSeenMs) > kExpireMs", POLICY)
        self.assertIn("kExpireMs = 3 * kEngagedBossFreshMs", POLICY)
        registered = body(ACTIONS, "std::vector<Creature*> RegisteredEngagedBosses(")
        self.assertIn("g_engagedBossCount.load(std::memory_order_relaxed) == 0", registered)
        # Nothing registered: the read returns before the lock.
        self.assertLess(registered.index("g_engagedBossCount.load"), registered.index("std::lock_guard"))
        # The read collects the caller's bucket (and prunes it) under one lock, its clock read there too.
        self.assertIn("g_engagedBosses.Read(mapId, instanceId, WorldTimer::getMSTime(), seenEntries)", registered)
        self.assertLess(registered.index("std::lock_guard"), registered.index("WorldTimer::getMSTime()"))
        self.assertIn("EngagedBossUsable(view, nowMs)", registered)
        self.assertIn("bot->GetMap()->GetCreature(ObjectGuid(row.guid))", registered)
        for field in ("view.alive = sServerFacade.IsAlive(creature);", "view.inCombat = creature->IsInCombat();",
                      "view.evading = creature->IsInEvadeMode();", "view.botInCombat = bot->IsInCombat();",
                      "view.botToBossYd = bot->GetDistance(creature);"):
            self.assertIn(field, registered)

    def test_abandoned_buckets_are_swept_from_the_note_path(self):
        # A bucket no bot of its instance touches any more is only found by the registry's one full walk: it runs from
        # the note path, under the same lock, at most once a minute, gated on the map instance still existing.
        fn = body(ACTIONS, "void NoteEngagedBoss(Player* bot, Unit* unit, std::uint32_t mapId)")
        self.assertIn("g_engagedBosses.SweepAbandoned(nowMs,", fn)
        self.assertIn("return sMapMgr.FindMap(sweepMapId, sweepInstanceId) != nullptr;", fn)
        self.assertIn("kEngagedBossSweepMs = 60000", ACTIONS)
        self.assertIn("nowMs - g_engagedBossSweepMs >= kEngagedBossSweepMs", fn)
        self.assertLess(fn.index("std::lock_guard"), fn.index("const std::uint32_t nowMs = WorldTimer::getMSTime();"))
        self.assertLess(fn.index("std::lock_guard"), fn.index("SweepAbandoned"))
        self.assertIn("g_engagedBossCount.store(g_engagedBosses.size, std::memory_order_relaxed);", fn.split("SweepAbandoned")[1])
        self.assertIn("void SweepAbandoned(std::uint32_t nowMs, InstanceAlive instanceAlive)", POLICY)

    def test_the_key_is_documented_and_off(self):
        self.assertIn('bossTacticsKeepsEngagedBoss = config.GetBoolDefault("AiPlayerbot.BossTacticsKeepsEngagedBoss", false);',
                      CONFIG)
        self.assertIn("AiPlayerbot.BossTacticsKeepsEngagedBoss = 0", TEMPLATE)
        self.assertIn("### `AiPlayerbot.BossTacticsKeepsEngagedBoss`", SETTINGS)
        self.assertIn("### `AiPlayerbot.BossTacticsKeepsEngagedBoss`", SETTINGS)
        # Which fights it is for: Onyxia's Lair first, the other raids untested, and only the rows it serves.
        self.assertIn("Recommended on for Onyxia's Lair; not yet tested in other raids.", TEMPLATE)
        self.assertIn("recommended on for Onyxia's Lair; not yet tested in other raids", SETTINGS)
        self.assertIn("a boss with a\n# tank_post row", TEMPLATE)
        # The policy holds the three constants the glue reads, and no boss name.
        for constant in ("kEngagedBossFreshMs", "kEngagedBossNoteMs", "kEngagedBossReachYd"):
            self.assertIn(constant, POLICY)
        self.assertNotIn("Onyxia", POLICY)
        self.assertNotIn("10184", POLICY)


if __name__ == "__main__":
    unittest.main()
