"""Wiring pins for the take row's source word (`src=` on `seek take`, Hakkar walk 56 F4, log part only).

The word is pure (SeekTakeSrcWord, tested in tools/test_seek_aura_policy.cpp). These pins prove every take path in
FindBossSeekTank names itself, the take edge adds `+edge`, and LogSeekTake writes the field. No flag: it is a log field.
Run: python3 -m unittest tools/test_seek_take_src_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
ACTIONS_H = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.h").read_text()
ROWS = (ROOT / "docs/play/refusal-rows.md").read_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


class SeekTakeSrcWiring(unittest.TestCase):
    def test_the_row_carries_src(self):
        log = body(ACTIONS, "void LogSeekTake(PlayerbotAI* ai, Player* bot, Unit* son, const char* reason, bool meet,")
        self.assertIn("const std::string& src)", log)
        self.assertIn('"seek take: %s -> %u victim=%s reason=%s meet=%d src=%s"', log)
        self.assertIn("src.c_str()", log)
        self.assertIn('const char* takeSrc = "other";', ACTIONS_H)

    def test_every_take_path_names_itself(self):
        fn = body(ACTIONS, "SeekTankTask ai::FindBossSeekTank(PlayerbotAI* ai, Player* bot)")
        # One word per `task.take = true;`.
        self.assertEqual(fn.count("task.take = true;"), fn.count("task.takeSrc = "))
        for word in ('"meet"', '"healer_clock"', '"pull_join"'):
            self.assertIn("task.takeSrc = " + word + ";", fn)
        # The nearest take names the reach when only the reach took it (walk 57 F4: `src=nearest_reach`).
        self.assertIn('task.takeSrc = viaReach ? "nearest_reach" : "nearest";', fn)
        self.assertIn('task.takeSrc = thirdTank ? "receiver" : "pickup";', fn)
        # The meet's word sits with the meet's take, the nearest's with the nearest's.
        meet = fn[fn.index("task.meet = true;"):]
        self.assertTrue(meet.startswith('task.meet = true;\n                    task.takeSrc = "meet";'))
        nearest = fn[fn.index("task.source = nearestSon;"):]
        self.assertLess(nearest.index('task.takeSrc = viaReach ? "nearest_reach" : "nearest";'), nearest.index("return task;"))

    def test_the_edge_take_adds_its_suffix(self):
        fn = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        take = fn[fn.index("if (task.take)"):fn.index("if (task.pull)")]
        self.assertIn("bool edgeTake = false;", take)
        self.assertIn("edgeTake = !chaseRefused;", take)
        self.assertLess(take.index("chaseRefused = edge != SeekTakeEdge::Allowed;"), take.index("edgeTake = !chaseRefused;"))
        self.assertIn("task.meet, SeekTakeSrcWord(task.takeSrc, edgeTake));", take)

    def test_the_field_is_documented(self):
        self.assertIn("meet=<0\\|1> src=<word>", ROWS)
        for word in ("`nearest`", "`nearest_reach`", "`healer_clock`", "`receiver`", "`meet`", "`pickup`", "`pull_join`", "`+edge`"):
            self.assertIn(word, ROWS)


class SeekTakeHeldWiring(unittest.TestCase):
    """Walk 58 F3: the cap guard's held take logs a `seek take held` row (it logged nothing before)."""

    def test_the_row_format_and_throttle(self):
        log = body(ACTIONS, "void LogSeekTakeHeld(PlayerbotAI* ai, Player* bot, Unit* son, const SeekCapGuardInfo& info,")
        self.assertIn('"seek take held son=%u taker=%s reason=cap_guard ref=%s share=%.0f src=%s victim=%s on_nontank_ms=%u "\n'
                      '             "victim_hp=%.0f"', log)
        self.assertIn('victim ? victim->GetName() : "none", onNonTankMs, victim ? victim->GetHealthPercent() : 100.0f);', log)
        self.assertIn('"BossTacticsSeekPull"', log)
        # Throttled per tank and Son, 2 s.
        self.assertIn("constexpr std::uint32_t kSeekTakeHeldLogGapMs = 2000;", ACTIONS)
        self.assertIn("std::make_pair(bot->GetObjectGuid().GetRawValue(), son->GetObjectGuid().GetRawValue())", log)
        self.assertIn("nowMs - last < kSeekTakeHeldLogGapMs", log)
        # No colon after "seek take": the take parsers that match "seek take:" never read it as a take.
        self.assertNotIn('"seek take held:', ACTIONS)

    def test_the_reach_guard_logs_the_held_son(self):
        fn = body(ACTIONS, "Unit* FindSeekNearestTake(PlayerbotAI* ai, Player* bot, const RaidTankRoles& roles,")
        self.assertIn("SeekTankLeaveCollapsesCap(ai, roles, state.boss, bot, &capInfo);", fn)
        held = fn[fn.index("if (pick < 0 && selfCollapsesCap)"):]
        self.assertIn("SeekCapGuardHeldPick(sPlayerbotAIConfig.dungeonRunSeekNearestTake, roles.botIndex, 2,", held)
        self.assertIn("Unit* const heldSon = units[static_cast<std::size_t>(held)];", held)
        self.assertIn("state.nonTankSince.find(heldSon->GetObjectGuid().GetRawValue());", held)
        self.assertIn("since != state.nonTankSince.end() ? state.nowMs - since->second.sinceMs : 0;", held)
        self.assertIn('LogSeekTakeHeld(ai, bot, heldSon, capInfo, "nearest_reach", heldOnNonTankMs);', held)

    def test_the_healer_clock_guard_logs_the_held_son(self):
        fn = body(ACTIONS, "bool SeekHealerClockCapOk(PlayerbotAI* ai, Player* bot, const RaidTankRoles& roles,")
        self.assertIn("SeekTankLeaveCollapsesCap(ai, roles, state.boss, bot, &capInfo)", fn)
        self.assertIn('if (!allowed)\n        LogSeekTakeHeld(ai, bot, son, capInfo, "healer_clock", onNonTankMs);', fn)
        self.assertIn("return allowed;", fn)

    def test_the_guard_names_the_reference(self):
        fn = body(ACTIONS, "bool SeekTankLeaveCollapsesCap(PlayerbotAI* ai, const RaidTankRoles& roles, Unit* boss,")
        self.assertIn("ThreatCapLeaveReadOf(tanks, selfPos, sPlayerbotAIConfig.dungeonRunThreatSuccessorNotHolder);", fn)
        self.assertIn("info->sharePct = read.sharePct;", fn)
        self.assertIn("return read.collapses;", fn)

    def test_the_row_is_documented(self):
        self.assertIn("seek take held son=%u taker=<name> reason=cap_guard ref=<name> share=%.0f src=", ROWS)
        self.assertIn("victim=<name> on_nontank_ms=%u victim_hp=%.0f", ROWS)


if __name__ == "__main__":
    unittest.main()
