"""Wiring pins for the ready check's module half (task R4m).

The pure rules live in runtime/ReadyCheckPolicy.h (BuffBits, ConsumableBits,
IsKeyCooldown, KeySpells, pinned by tools/test_ready_check_policy.cpp) and the
wire shape in runtime/CommanderTelemetry.h (`BotExtra` and the `rdy` writer,
pinned by tools/test_commander_telemetry.cpp). These pins prove the commands
layer feeds them: every bound bot's facts are read out of combat only, the aura
read runs every 10 s and the key-spell list every 60 s through a per-bot cache,
the cache is pruned with the other bound caches, and nothing scans the world.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CMD = (ROOT / "commands" / "Commander.cpp").read_text()
TELEMETRY = (ROOT / "runtime" / "CommanderTelemetry.h").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


CODE = strip_comments(CMD)


class ReadyCheckWiring(unittest.TestCase):
    def test_the_bot_loop_reads_the_facts_for_every_bound_bot(self):
        fn = body(CODE, "static void FillCommanderMapData(")
        self.assertIn("ReadReadyFacts(bot, extra, WorldTimer::getMSTime());", fn)
        loop = fn[fn.index("for (size_t i = 0; i < snapshot.partyCount; ++i)"):]
        self.assertLess(loop.index("ReadReadyFacts("), loop.index("ref.botExtras.push_back"))

    def test_the_reads_are_throttled(self):
        fn = body(CODE, "static void ReadReadyFacts(")
        self.assertIn("ReadyCheck::kReadyRefreshMs", fn)
        self.assertIn("ReadyCheck::kKeySpellsRefreshMs", fn)
        self.assertIn("s_readyCache()", fn)
        # the aura read sits behind the 10 s gate, the key-spell list behind the 60 s one
        self.assertLess(fn.index("ReadyCheck::kReadyRefreshMs"), fn.index("ReadReadyAuras("))
        self.assertLess(fn.index("ReadyCheck::kKeySpellsRefreshMs"), fn.index("ReadReadyKeySpells("))

    def test_combat_sends_the_cached_values_and_reads_nothing(self):
        fn = body(CODE, "static void ReadReadyFacts(")
        self.assertIn("if (bot->IsInCombat())", fn)
        self.assertIn("ApplyReadyCache", fn)
        # every read helper comes after the combat branch, which returns
        combat = fn.index("if (bot->IsInCombat())")
        read = fn.index("ReadReadyAuras(")
        self.assertLess(combat, read)
        self.assertLess(combat, fn.index("ReadReadyKeySpells("))
        self.assertLess(combat, fn.index("return;"))
        self.assertLess(fn.index("return;"), fn.index("kReadyRefreshMs"))

    def test_the_aura_read_uses_the_core_aura_facts(self):
        fn = body(CODE, "static void ReadReadyAuras(")
        for api in ("GetSpellAuraHolderMap()", "holder->IsDeleted()", "holder->IsPassive()",
                    "holder->GetSpellProto()", "proto->EffectApplyAuraName[i]", "proto->EffectMiscValue[i]",
                    "proto->SpellFamilyName", "sSpellMgr.GetSpellElixirMask(", "holder->IsPositive()",
                    "holder->GetCasterGuid().IsPlayer()"):
            self.assertIn(api, fn)
        self.assertIn("ReadyCheck::BuffBits(facts)", fn)
        self.assertIn("ReadyCheck::ConsumableBits(facts)", fn)

    def test_the_key_spell_read_skips_removed_and_disabled(self):
        fn = body(CODE, "static void ReadReadyKeySpells(")
        for api in ("bot->GetSpellMap()", "PLAYERSPELL_REMOVED", ".disabled", "sSpellMgr.GetFirstSpellInChain(",
                    "proto->RecoveryTime", "proto->CategoryRecoveryTime", "proto->IsPassiveSpell()"):
            self.assertIn(api, fn)
        self.assertIn("ReadyCheck::KeySpells(spells)", fn)

    def test_ready_and_left_come_from_the_cooldown_read(self):
        fn = body(CODE, "static void ReadReadyCooldowns(")
        self.assertIn("bot->HasSpellCooldown(spell)", fn)
        self.assertIn("bot->GetSpellCooldownDelay(spell)", fn)

    def test_the_cache_is_pruned_like_the_other_bound_caches(self):
        fn = body(CODE, "void CollectCommanderTelemetry(")
        self.assertIn("s_readyCache().clear();", fn)
        self.assertRegex(fn, r"for \(auto it = s_readyCache\(\)\.begin\(\); it != s_readyCache\(\)\.end\(\);\)")
        # pruned against the bots still bound, in the same place as the mob watch
        self.assertLess(fn.index("s_mobWatch().end();"), fn.index("s_readyCache().end();"))

    def test_no_grid_or_world_scan(self):
        for name in ("static void ReadReadyFacts(", "static void ReadReadyAuras(", "static void ReadReadyKeySpells(",
                     "static void ReadReadyCooldowns("):
            fn = body(CODE, name)
            for scan in ("GetCreatureListWithEntryInGrid", "DoCreatureData", "Cell::Visit", "VisitAll", "sObjectMgr"):
                self.assertNotIn(scan, fn)

    def test_the_writer_sends_the_ready_object_only_inside_a_bot_extra(self):
        # hasReady gates the object; it rides the same bot object as the other extra fields
        self.assertIn("bool hasReady = false;", TELEMETRY)
        self.assertIn('if (x.hasReady && trim < 4)', TELEMETRY)
        writer = body(TELEMETRY, "inline std::string BuildPayloadOf(")
        self.assertIn('\\"rdy\\"', writer)


class ReadyCheckPolicyTestWiring(unittest.TestCase):
    def test_the_named_tests_exist(self):
        for path in ("tools/test_ready_check_policy.cpp", "tools/test_ready_check_wiring.py"):
            self.assertTrue((ROOT / path).is_file(), path)


if __name__ == "__main__":
    unittest.main()
