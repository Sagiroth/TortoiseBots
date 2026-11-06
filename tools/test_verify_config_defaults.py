"""Unit tests for the config-defaults drift checker's parser.

tools/verify_config_defaults.py enforces the AGENTS.md rule that a key's code
fallback (`Get*Default`) equals the shipped template value. These tests cover
the literal evaluator, the balanced-paren expression extractor, template line
parsing and value comparison on synthetic input, plus one smoke test that the
real repository is currently clean.
"""
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from verify_config_defaults import (
    DELEGATED,
    active_duplicates,
    extract_expr,
    parse_default,
    parse_template,
    values_equal,
    check,
    ROOT,
)


TEXT = (
    'a = config.GetIntDefault("AiPlayerbot.GlobalCooldown", 1500);\n'
    'b = config.GetBoolDefault("AiPlayerbot.Enabled", false);\n'
    'c = config.GetFloatDefault("AiPlayerbot.SightDistance", 60.0f);\n'
    'd = config.GetIntDefault("AiPlayerbot.MaxRandomBotInWorldTime", 6 * 3600);\n'
    'e = config.GetStringDefault("AiPlayerbot.CommandSeparator", "\\\\");\n'
    'f = config.GetStringDefault("AiPlayerbot.LLMResponseStartPattern", R"(("text":\\s*"))");\n'
    'g = config.GetStringDefault("AiPlayerbot.LLMRpgPrompt", "In Tortoise."\n'
    '    " Standing nearby.");\n'
    'h = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.ObservabilityPort", 9195));\n'
    'i = config.GetIntDefault("AiPlayerbot.MaxRandomBotRandomizeTime",\n'
    '    config.GetIntDefault("AiPlayerbot.MaxRandomRandomizeTime", 24 * 3600));\n'
    'j = config.GetIntDefault(os.str().c_str(), 100);\n'
    'k = uint32(BotSelfBotLevel::GM_ONLY) ? config.GetBoolDefault("AiPlayerbot.SelfBotLevel", true) : false;\n'
)


def expr_for(key):
    import re
    from verify_config_defaults import CALL
    m = CALL.search(TEXT)
    while m and m.group(1) != key:
        m = CALL.search(TEXT, m.end())
    return extract_expr(TEXT, m.end())


class ParseDefaultTest(unittest.TestCase):
    def test_int(self):
        self.assertEqual(parse_default('1500'), 1500)

    def test_bool(self):
        self.assertIs(parse_default('false'), False)
        self.assertIs(parse_default('true'), True)

    def test_float(self):
        self.assertEqual(parse_default('60.0f'), 60.0)
        self.assertEqual(parse_default('0.50f'), 0.5)
        self.assertEqual(parse_default('60.f'), 60.0)
        self.assertEqual(parse_default('0.02f'), 0.02)

    def test_arithmetic(self):
        self.assertEqual(parse_default('6 * 3600'), 21600)
        self.assertEqual(parse_default('1 * 1800'), 1800)
        self.assertEqual(parse_default('24 * 3600'), 86400)

    def test_string_with_escapes(self):
        # C++ r'"\\"' is one backslash; r'"\\\\"' (as in CommandSeparator) is two
        self.assertEqual(parse_default(r'"\\"'), '\\')
        self.assertEqual(parse_default(r'"\\\\"'), '\\\\')
        self.assertEqual(parse_default(r'"\"a\"b\""'), '"a"b"')

    def test_raw_string_keeps_regex_verbatim(self):
        self.assertEqual(parse_default('R"(("text":\\s*"))"'), '("text":\\s*")')

    def test_adjacent_string_literals_concatenate(self):
        self.assertEqual(parse_default('"In Tortoise."\n    " Standing nearby."'),
                         'In Tortoise. Standing nearby.')

    def test_delegate(self):
        self.assertIs(parse_default('config.GetIntDefault("AiPlayerbot.X", 5)'), DELEGATED)

    def test_computed_identifiers_reject(self):
        self.assertIsNone(parse_default('TortoiseBots::kHireRaidSpreadDefault'))
        self.assertIsNone(parse_default('uint32(BotSelfBotLevel::GM_ONLY)'))

    def test_cast_unwraps(self):
        self.assertEqual(parse_default('static_cast<uint32>(9195)'), 9195)
        self.assertEqual(parse_default('uint32(300)'), 300)


class ExtractExprTest(unittest.TestCase):
    def test_simple(self):
        self.assertEqual(expr_for('AiPlayerbot.GlobalCooldown'), '1500')

    def test_stops_at_closing_paren(self):
        self.assertEqual(expr_for('AiPlayerbot.ObservabilityPort'), '9195')

    def test_multiline_nested_call(self):
        self.assertEqual(
            expr_for('AiPlayerbot.MaxRandomBotRandomizeTime'),
            'config.GetIntDefault("AiPlayerbot.MaxRandomRandomizeTime", 24 * 3600)')

    def test_dynamic_key_not_matched(self):
        # keys built at runtime (key.c_str(), os.str()) are not statically checkable
        from verify_config_defaults import CALL
        keys = [m.group(1) for m in CALL.finditer(TEXT)]
        self.assertEqual(len(keys), 11)
        self.assertTrue(all(k.startswith('AiPlayerbot.') for k in keys))


class ValuesEqualTest(unittest.TestCase):
    def test_bool_vs_template(self):
        self.assertTrue(values_equal(True, '1'))
        self.assertTrue(values_equal(False, '0'))
        self.assertFalse(values_equal(True, '0'))

    def test_numeric(self):
        self.assertTrue(values_equal(0.5, '0.50'))
        self.assertTrue(values_equal(60.0, '60'))
        self.assertFalse(values_equal(22.0, '25.0'))

    def test_string(self):
        self.assertTrue(values_equal('RNDBOT', 'RNDBOT'))
        self.assertTrue(values_equal('\\\\', '\\\\'))
        self.assertTrue(values_equal('a b', 'a  b'))  # whitespace collapsed
        self.assertFalse(values_equal('bots.log', ''))

    def test_template_outer_quotes_stripped(self):
        self.assertTrue(values_equal('RPLL_H_,DBMv4', '"RPLL_H_,DBMv4"'))


class ParseTemplateTest(unittest.TestCase):
    def test_active_and_commented_and_inline_comment(self):
        with tempfile.TemporaryDirectory() as tmp:
            conf = pathlib.Path(tmp) / 't.conf'
            conf.write_text(
                '[AiPlayerbotConf]\n'
                'AiPlayerbot.Enabled = 0\n'
                '# AiPlayerbot.LevelLadder = 1\n'
                '#AiPlayerbot.BotLogDebug = 0\n'
                'AiPlayerbot.RespawnModNeutral = 10.0    # each nearby player\n')
            entries = parse_template(conf)
        self.assertEqual(entries['AiPlayerbot.Enabled'], [(True, '0')])
        self.assertEqual(entries['AiPlayerbot.LevelLadder'], [(False, '1')])
        self.assertEqual(entries['AiPlayerbot.BotLogDebug'], [(False, '0')])
        self.assertEqual(entries['AiPlayerbot.RespawnModNeutral'], [(True, '10.0')])


class ConstantsMatchTemplateTest(unittest.TestCase):
    """The ALLOWLIST exempts keys whose code default is a C++ constant; these
    tests are the assertion those ALLOWLIST entries promise: the constant still
    equals the value the template shows."""

    def header_constant(self, header_rel, name):
        text = (ROOT / header_rel).read_text()
        i = text.index(name)
        i = text.index('=', i) + 1
        while text[i] in ' \t\n\r':
            i += 1
        start = i
        n = len(text)
        while i < n and text[i] != ';':  # scan to the initializer's semicolon,
            if text[i] == '"':           # skipping string contents (they hold ';')
                i += 1
                while text[i] != '"':
                    i += 2 if text[i] == '\\' else 1
            i += 1
        return parse_default(text[start:i])

    def assert_constant_matches_template(self, header_rel, name, key):
        value = self.header_constant(header_rel, name)
        entries = parse_template(ROOT / 'ai/playerbot/aiplayerbot.conf.dist.in').get(key, [])
        self.assertTrue(
            any(values_equal(value, v) for _, v in entries),
            f'{name} = {value!r} matches none of the template entries for {key}')

    def test_ammo_stacks_non_hunter(self):
        self.assert_constant_matches_template(
            'ai/playerbot/AmmoStackPolicy.h', 'kAmmoStacksNonHunterDefault',
            'AiPlayerbot.AmmoStacksNonHunter')

    def test_hire_raid_spread(self):
        self.assert_constant_matches_template(
            'runtime/HireRaidPolicy.h', 'kHireRaidSpreadDefault',
            'AiPlayerbot.HireRaidSpread')

    def test_hire_raid_presets(self):
        self.assert_constant_matches_template(
            'runtime/HireRaidPolicy.h', 'kHireRaidPresetsDefault',
            'AiPlayerbot.HireRaidPresets')


class ActiveDuplicateKeysTest(unittest.TestCase):
    def test_same_file_active_duplicate_is_flagged(self):
        with tempfile.TemporaryDirectory() as tmp:
            conf = pathlib.Path(tmp) / 't.conf'
            conf.write_text(
                'AiPlayerbot.DupKey = 0\n'
                '# AiPlayerbot.DupKey = 1\n'
                'AiPlayerbot.DupKey = 0\n'
                'AiPlayerbot.Once = 1\n')
            dups = active_duplicates(conf)
        self.assertEqual(list(dups), ['AiPlayerbot.DupKey'])
        self.assertEqual(len(dups['AiPlayerbot.DupKey']), 2)

    def test_shipped_templates_have_no_active_duplicates(self):
        found = {}
        for rel in ('ai/playerbot/aiplayerbot.conf.dist.in', 'conf/tortoise_bots.conf.dist'):
            dups = active_duplicates(ROOT / rel)
            if dups:
                found[rel] = dups
        self.assertEqual(found, {})


class RepoCleanTest(unittest.TestCase):
     def test_repository_currently_has_no_drift(self):
         self.assertEqual(check(), [])


if __name__ == '__main__':
    unittest.main()
