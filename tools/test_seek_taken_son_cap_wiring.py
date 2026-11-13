"""Wiring pins for the damage dealers' threat cap on the meet's just-taken Son (DungeonRunSeekTakenSonThreatCap, walk 60
lever 2).

The rules are pure and tested in tools/test_seek_aura_policy.cpp (SeekTakenSonDpsHeld, `taken_son_dps_held_at_85pct`)
and tools/test_seek_walkout_policy.cpp (the taken state: TestWalkoutTakenState). These pins prove the live code uses
them: the taken close notes the state before the meet is cleared, the reader takes one lock, the hold applies only to
the taken Son, and IsDpsHeld reads the key first.
Run: python3 -m unittest tools/test_seek_taken_son_cap_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
SETTINGS_DOC = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
ROWS_DOC = (ROOT / "docs/play/refusal-rows.md").read_text()
POLICY = (ROOT / "ai/playerbot/SeekWalkoutPolicy.h").read_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


def inline_body(text, signature):
    start = text.index(signature)
    return text[start:text.index("\n    }\n", start)]


class TakenSonThreatCapWiring(unittest.TestCase):
    def test_the_key_is_declared_loaded_and_documented(self):
        self.assertIn("bool dungeonRunSeekTakenSonThreatCap = false;", CONFIG_H)
        self.assertIn('dungeonRunSeekTakenSonThreatCap = config.GetBoolDefault("AiPlayerbot.DungeonRunSeekTakenSonThreatCap", false)',
                      CONFIG)
        self.assertIn("AiPlayerbot.DungeonRunSeekTakenSonThreatCap = 0", TEMPLATE)
        self.assertIn("### `AiPlayerbot.DungeonRunSeekTakenSonThreatCap`", SETTINGS_DOC)

    def test_the_taken_close_notes_the_state_before_the_meet_clear(self):
        fn = body(ACTIONS, "void UpdateSeekWalkout(PlayerbotAI* ai, Player* bot, const BossTacticRow& row, Unit* boss,")
        note = "WalkoutTakenNote(it->second.state, state.meetSon, takenBy->GetObjectGuid().GetRawValue(), nowMs);"
        self.assertEqual(1, ACTIONS.count(note))
        block = fn[fn.index("if (lost || takenBy)"):]
        self.assertIn("if (cleared && takenBy)\n                    " + note, block)
        # Inside the lock that cleared the shared meet, after the compare-and-clear, before the local meet clear.
        self.assertLess(block.index("std::lock_guard<std::mutex> lock(g_walkoutMutex);"), block.index(note))
        self.assertLess(block.index("CloseWalkoutMeetIf(it->second.state,"), block.index(note))
        self.assertLess(block.index(note), block.index("WalkoutMeetClear(state);"))

    def test_a_new_task_clears_the_state(self):
        start = inline_body(POLICY, "inline void WalkoutStart(SeekWalkoutState& s, std::uint64_t puller,")
        self.assertIn("WalkoutTakenClear(s);", start)

    def test_the_reader_takes_one_lock_for_the_snapshot(self):
        fn = body(ACTIONS, "SeekTakenSon SeekTakenSonRead(PlayerbotAI* ai, Player* bot)")
        snap = fn[:fn.index("GetCreature(ObjectGuid(out.son))")]
        self.assertEqual(1, snap.count("std::lock_guard<std::mutex> lock(g_walkoutMutex);"))
        self.assertIn("WalkoutTakenEnds(s.takenSon, true, age)", snap)
        self.assertIn("out.son = s.takenSon;", snap)
        self.assertIn("out.by = s.takenBy;", snap)
        # A dead or gone Son clears only the same take (compare-and-clear).
        tail = fn[fn.index("GetCreature(ObjectGuid(out.son))"):]
        self.assertIn("it->second.state.takenSon == out.son && it->second.state.takenMs == takenMs", tail)
        self.assertIn("WalkoutTakenClear(it->second.state);", tail)

    def test_the_hold_applies_only_to_the_taken_son(self):
        fn = body(ACTIONS, "bool TakenSonDpsHeld(PlayerbotAI* ai, Player* bot, Unit* unit)")
        self.assertTrue(fn.split("{", 1)[1].lstrip().startswith(
            "if (!sPlayerbotAIConfig.dungeonRunSeekTakenSonThreatCap)\n        return false;"))
        self.assertIn("if (PlayerbotAI::IsTank(bot) || !IsFreeDps(ai, bot))", fn)
        self.assertIn("unit->GetObjectGuid().GetRawValue() != taken.son", fn)
        self.assertIn("bot->GetObjectGuid().GetRawValue() == taken.by", fn)
        self.assertIn("const bool sonTakenByTank = victim->GetObjectGuid().GetRawValue() == taken.by;", fn)
        self.assertIn("ThreatPctOf(threat.getThreat(bot), threat.getThreat(victim), unit->IsInCombat(), false)", fn)
        self.assertIn("PlayerbotAI::IsHeal(bot)", fn)
        self.assertLess(fn.index("SeekTakenSonDpsHeld("), fn.index("LogSeekTakenSonCapped("))

    def test_the_cheap_filters_run_before_the_walkout_lock(self):
        # Codex/Opus round 1 (cost): the map filter and the victim-is-a-tank filter come before SeekTakenSonRead's lock.
        fn = body(ACTIONS, "bool TakenSonDpsHeld(PlayerbotAI* ai, Player* bot, Unit* unit)")
        read = fn.index("SeekTakenSonRead(ai, bot)")
        self.assertLess(fn.index("GetBossTactics().HasSeekAuraMap(bot->GetMapId())"), read)
        self.assertLess(fn.index("if (!victim || !PlayerbotAI::IsTank(victim))"), read)
        self.assertNotIn("g_walkoutMutex", fn)

    def test_an_unknown_siphon_clock_releases(self):
        # Codex round 1 HIGH: no fight read, or no known due time, is an unknown clock, and the pure rule releases it.
        fn = body(ACTIONS, "bool TakenSonDpsHeld(PlayerbotAI* ai, Player* bot, Unit* unit)")
        self.assertIn("const bool fightRead = ReadSeekFightCached(ai, bot, state);", fn)
        self.assertIn("const bool siphonKnown = fightRead && state.dueSignedMs != std::numeric_limits<int>::max();", fn)
        self.assertIn("taken.sinceMs, siphonKnown,", fn)

    def test_the_latch_is_per_bot_and_take(self):
        fn = body(ACTIONS, "bool TakenSonDpsHeld(PlayerbotAI* ai, Player* bot, Unit* unit)")
        self.assertIn("std::lock_guard<std::mutex> lock(g_seekTakenCapMutex);", fn)
        self.assertIn("SeekTakenCapLatchFor(g_seekTakenCapLatches[bot->GetObjectGuid().GetRawValue()], taken.son, taken.takenMs)",
                      fn)
        self.assertIn("siphonInMs, PlayerbotAI::IsHeal(bot), latch.held);", fn)
        self.assertIn("latch.held = held;", fn)
        self.assertIn("rowDue = SeekTakenCapRowDue(latch, held);", fn)
        # The row is written after the latch lock is released.
        self.assertLess(fn.index("rowDue = SeekTakenCapRowDue(latch, held);\n    }"), fn.index("if (rowDue)"))
        reader = body(ACTIONS, "SeekTakenSon SeekTakenSonRead(PlayerbotAI* ai, Player* bot)")
        self.assertIn("out.takenMs = s.takenMs;", reader)

    def test_is_dps_held_reads_the_key_first(self):
        fn = body(ACTIONS, "bool ai::IsDpsHeld(PlayerbotAI* ai, Player* bot, Unit* unit, DpsHoldTrigger* trigger)")
        call = "if (sPlayerbotAIConfig.dungeonRunSeekTakenSonThreatCap && TakenSonDpsHeld(ai, bot, unit))\n        return true;"
        self.assertIn(call, fn)
        # Before the aura-tactics gates, so those cannot return first.
        self.assertLess(fn.index(call), fn.index("if (!sPlayerbotAIConfig.dungeonRunAuraTactics"))
        self.assertLess(fn.index("if (!ai || !bot || !unit || !unit->IsAlive())"), fn.index(call))

    def test_the_row_text(self):
        fn = body(ACTIONS, "void LogSeekTakenSonCapped(PlayerbotAI* ai, Player* bot, Unit* son, std::uint8_t pct,")
        self.assertIn('"seek take capped son=%u bot=%s pct=%u since_take_ms=%u"', fn)
        self.assertIn("seek take capped son=<son> bot=<name> pct=<pct> since_take_ms=<ms>", ROWS_DOC)
        # The documented limit: area damage, ticking damage and pets are not held.
        self.assertIn("pets", ROWS_DOC[ROWS_DOC.index("| 18b |"):ROWS_DOC.index("| 18b |") + 800])

if __name__ == "__main__":
    unittest.main()
