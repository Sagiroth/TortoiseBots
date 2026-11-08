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
        for word in ('"meet"', '"nearest"', '"healer_clock"', '"pull_join"'):
            self.assertIn("task.takeSrc = " + word + ";", fn)
        self.assertIn('task.takeSrc = thirdTank ? "receiver" : "pickup";', fn)
        # The meet's word sits with the meet's take, the nearest's with the nearest's.
        meet = fn[fn.index("task.meet = true;"):]
        self.assertTrue(meet.startswith('task.meet = true;\n                    task.takeSrc = "meet";'))
        nearest = fn[fn.index("task.source = nearestSon;"):]
        self.assertLess(nearest.index('task.takeSrc = "nearest";'), nearest.index("return task;"))

    def test_the_edge_take_adds_its_suffix(self):
        fn = body(ACTIONS, "bool BossTacticsSeekTankAction::Execute(Event& event)")
        take = fn[fn.index("if (task.take)"):fn.index("if (task.pull)")]
        self.assertIn("bool edgeTake = false;", take)
        self.assertIn("edgeTake = !chaseRefused;", take)
        self.assertLess(take.index("chaseRefused = edge != SeekTakeEdge::Allowed;"), take.index("edgeTake = !chaseRefused;"))
        self.assertIn("task.meet, SeekTakeSrcWord(task.takeSrc, edgeTake));", take)

    def test_the_field_is_documented(self):
        self.assertIn("meet=<0\\|1> src=<word>", ROWS)
        for word in ("`nearest`", "`healer_clock`", "`receiver`", "`meet`", "`pickup`", "`pull_join`", "`+edge`"):
            self.assertIn(word, ROWS)


if __name__ == "__main__":
    unittest.main()
