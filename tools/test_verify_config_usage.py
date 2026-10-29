"""Unit tests for the config-usage checker.

tools/verify_config_usage.py fails when a key is read into PlayerbotAIConfig
and the member is consumed nowhere else. These tests cover the read detection,
the read masking inside PlayerbotAIConfig.cpp, the display-only classification
and the allow-list on a synthetic tree, plus one smoke test that the real
repository is currently clean.
"""
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import verify_config_usage
from verify_config_usage import check, READ


CONF_CPP = """\
#include "playerbot/PlayerbotAIConfig.h"
void PlayerbotAIConfig::Initialize()
{
    deadValue = config.GetIntDefault("AiPlayerbot.DeadKey", 0);
    usedValue = config.GetIntDefault("AiPlayerbot.UsedKey", 0);
    configInternal = config.GetIntDefault("AiPlayerbot.ConfigInternalKey", 0);
    globalFlag = configInternal != 0;
}
void PlayerbotAIConfig::LoadUpdates()
{
    displayOnlyValue = config.GetBoolDefault("AiPlayerbot.DisplayOnlyKey", true);
}
"""

CONF_H = """\
class PlayerbotAIConfig
{
public:
    uint32 deadValue;
    uint32 usedValue;
    bool configInternal;
    bool globalFlag;
    bool displayOnlyValue;
};
"""

RUN_CPP = """\
void RunBot::Tick(Player* bot)
{
    if (sPlayerbotAIConfig.usedValue)
        bot->Advance();
}
"""


class SyntheticTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        root = pathlib.Path(self._tmp.name)
        (root / 'ai/playerbot').mkdir(parents=True)
        (root / 'runtime').mkdir()
        (root / 'ai/playerbot/PlayerbotAIConfig.cpp').write_text(CONF_CPP)
        (root / 'ai/playerbot/PlayerbotAIConfig.h').write_text(CONF_H)
        (root / 'runtime/RunBot.cpp').write_text(RUN_CPP)
        (root / 'runtime/ObservabilityEmitter.cpp').write_text(
            'void Emitter::Snapshot()\n'
            '{\n'
            '    ss << "skeleton"\n'
            '       << ",\\"frame\\":" << sPlayerbotAIConfig.displayOnlyValue\n'
            '       << ",\\"tail\\\":" << sPlayerbotAIConfig.othertail;\n'
            '}}\n')
        self.root = root

    def tearDown(self):
        self._tmp.cleanup()

    def test_read_pairs_member_with_key(self):
        text = CONF_CPP
        m = READ.search(text)
        self.assertEqual(m.group(1), 'deadValue')
        self.assertEqual(m.group(2), 'AiPlayerbot.DeadKey')

    def test_read_line_is_masked(self):
        masked = CONF_CPP
        for m in READ.finditer(masked):
            masked = masked.replace(m.group(0), ' ' * len(m.group(0)))
        self.assertNotIn('deadValue = config', masked)
        self.assertNotIn('usedValue = config', masked)

    def test_dead_key_reported(self):
        findings = check(self.root)
        self.assertTrue(any(f.startswith('deadValue: ') for f in findings))

    def test_engine_consumption_passes(self):
        findings = [f for f in check(self.root) if f.startswith('usedValue')]
        self.assertEqual(findings, [])

    def test_consumption_inside_the_config_file_counts(self):
        findings = [f for f in check(self.root) if f.startswith('configInternal')]
        self.assertEqual(findings, [])

    def test_snapshot_emit_is_display_only(self):
        findings = check(self.root)
        self.assertTrue(any(f.startswith('displayOnlyValue: ') and
                            'dashboard' in f for f in findings))

    def test_allowlist_silences_a_read(self):
        verify_config_usage.ALLOWLIST['deadValue'] = 'intentional: not consumed yet'
        try:
            findings = [f for f in check(self.root) if f.startswith('deadValue')]
            self.assertEqual(findings, [])
        finally:
            verify_config_usage.ALLOWLIST.clear()


class LiveRepoSmokeTest(unittest.TestCase):
    def test_the_real_repository_has_no_dead_reads(self):
        self.assertEqual(check(verify_config_usage.ROOT), [])


if __name__ == '__main__':
    unittest.main()
