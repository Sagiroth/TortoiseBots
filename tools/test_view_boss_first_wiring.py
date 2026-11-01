"""Wiring pin for the boss pass in the commander view snapshot: a boss any bot
fights keeps one of the 24 mob rows, however many adds took rows before it.

The order is glue code over game units, so it is pinned here: the boss pass runs
before the exact-victim pass in commands/Commander.cpp's CollectViewSnapshot,
every call in it asks for bosses only, a non-boss already seen this frame is
dropped before any unit lookup, and the rank letter has one source (ViewRankOf)
for both the view snapshot and the Dungeons tab. A guid that does not resolve
(stale list, another instance copy) is skipped for that occurrence only and is
never cached as a non-boss.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CMD = (ROOT / "commands/Commander.cpp").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


def canon(text):
    text = re.sub(r"//[^\n]*", "", text)
    return re.sub(r"\s+", "", text)


class BossRowWiring(unittest.TestCase):
    def test_boss_pass_runs_before_the_victim_pass(self):
        fn = body(CMD, "void CollectViewSnapshot(")
        after_clear = fn[fn.index("taken.Clear();"):]
        first_loop = after_clear.index("for (Player* bot : bots)")
        victim_loop = after_clear.index("for (Player* bot : bots)", first_loop + 1)
        boss_pass = after_clear[:victim_loop]
        calls = re.findall(r"AddViewHostile\([^\n]*\);", boss_pass)
        self.assertEqual(len(calls), 4, calls)
        for call in calls:
            self.assertTrue(call.endswith(", false, true);"), call)
        victim = after_clear.index(
            "AddViewHostile(snapshot, taken, nonBosses, bot, victim->GetObjectGuid())")
        self.assertLess(victim_loop, victim)

    def test_non_boss_reject_set_is_checked_before_any_lookup(self):
        fn = body(CMD, "void AddViewHostile(")
        boss_guard = fn[fn.index("if (bossOnly)"):fn.index("SeenHostile(bot, guid)")]
        self.assertIn(
            "std::find(nonBosses.begin(), nonBosses.end(), guid) != nonBosses.end()",
            boss_guard)
        self.assertIn("ViewRankOf(", boss_guard)
        self.assertIn("nonBosses.push_back(guid);", boss_guard)

    def test_reject_set_caches_only_a_resolved_non_boss(self):
        fn = body(CMD, "void AddViewHostile(")
        boss_guard = fn[fn.index("if (bossOnly)"):
                        fn.index("Unit* unit = SeenHostile(bot, guid);")]
        flat = canon(boss_guard)
        lookup = re.search(
            r"Map\*(\w+)=bot->GetMap\(\);Unit\*(\w+)=\1\?(\1->GetUnit\(guid\)):nullptr;",
            flat)
        self.assertIsNotNone(lookup, boss_guard)
        unit_var = lookup.group(2)
        after = flat[lookup.end():]
        # A null lookup must not reach the cache: only this occurrence is skipped.
        null_guard = f"if(!{unit_var})return;"
        self.assertTrue(after.startswith(null_guard), after)
        self.assertEqual(
            after[len(null_guard):],
            f"if(ViewRankOf({unit_var})!='b'){{nonBosses.push_back(guid);return;}}}}",
            boss_guard)

    def test_non_boss_reject_set_is_frame_local(self):
        fn = body(CMD, "void CollectViewSnapshot(")
        self.assertIn("static std::vector<ObjectGuid> nonBosses;", fn)
        after_clear = fn[fn.index("taken.Clear();"):]
        self.assertLess(after_clear.index("nonBosses.clear();"),
                        after_clear.index("AddViewHostile("))

    def test_rank_has_one_source(self):
        self.assertEqual(CMD.count("CommanderTelemetry::RankKind("), 1)


if __name__ == "__main__":
    unittest.main()
