"""Unit tests for the config-usage checker.

tools/verify_config_usage.py fails when a key is read into PlayerbotAIConfig
and the member is consumed nowhere else. These tests cover the read detection
(static_cast, min/max clamping, enum constructors, LoadList containers,
direct calls, comment stripping), the read masking inside
PlayerbotAIConfig.cpp, the display-only classification and the allow-list on
a synthetic tree, plus one smoke test that the real repository is currently
clean.
"""
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import verify_config_usage
from verify_config_usage import check, scan_reads
CONF_CPP = """\
#include "playerbot/PlayerbotAIConfig.h"
void PlayerbotAIConfig::Initialize()
{
    deadMember = config.GetIntDefault("AiPlayerbot.DeadKey", 0);
    usedMember = config.GetIntDefault("AiPlayerbot.UsedKey", 0);
    configInternal = config.GetIntDefault("AiPlayerbot.ConfigInternalKey", 0);
    globalFlag = configInternal != 0;
    castMember = static_cast<uint32>(config.GetIntDefault("AiPlayerbot.CastKey", 3));
    clampMember = static_cast<uint32>(std::max(0, config.GetIntDefault("AiPlayerbot.ClampKey", 3)));
    levelMember = BotSelfBotLevel(config.GetIntDefault("AiPlayerbot.LevelKey", 1));
    LoadList<std::list<uint32> >(config.GetStringDefault("AiPlayerbot.ListKey", ""), listMember);
    BarGoLink::SetOutputState(config.GetBoolDefault("AiPlayerbot.DirectKey", true));
    // mentionedInCommentOnly = config.GetIntDefault("AiPlayerbot.CommentKey", 0);
}
void PlayerbotAIConfig::LoadUpdates()
{
    displayOnlyMember = config.GetBoolDefault("AiPlayerbot.DisplayOnlyKey", true);
}
"""

CONF_H = """\
class PlayerbotAIConfig
{
public:
    uint32 deadMember;
    uint32 usedMember;
    bool configInternal;
    bool globalFlag;
    bool displayOnlyMember;
    uint32 castMember;
    uint32 clampMember;
    int levelMember;
    std::list<uint32> listMember;
};
"""

RUN_CPP = """\
void RunBot::Tick(Player* bot)
{
    if (sPlayerbotAIConfig.usedMember)
        bot->Advance();
    // sPlayerbotAIConfig.deadMember is only named here in a comment.
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
        (root / 'runtime/RunBot.cpp').write_text(
            RUN_CPP + '\n'
            'void RunBot::More()\n'
            '{\n'
            '    use(sPlayerbotAIConfig.castMember);\n'
            '    use(sPlayerbotAIConfig.clampMember);\n'
            '    use(sPlayerbotAIConfig.levelMember);\n'
            '    use(sPlayerbotAIConfig.listMember);\n'
            '}\n')
        (root / 'runtime/ObservabilityEmitter.cpp').write_text(
            'void ObservabilityEmitter::Initialize() {}\n'
            'void Emitter::Snapshot()\n'
            '{\n'
            '    ss << "skeleton"\n'
            '       << ",\\"frame\\":" << sPlayerbotAIConfig.displayOnlyMember\n'
            '       << ",\\"tail\\\":" << sPlayerbotAIConfig.othertail;\n'
            '}}\n')
        self.root = root

    def tearDown(self):
        self._tmp.cleanup()

    def test_read_pairs_member_with_key(self):
        members, _ = scan_reads(self.root)
        self.assertEqual(members['deadMember'], {'AiPlayerbot.DeadKey'})

    def test_cast_clamp_enum_and_list_reads_are_attributed(self):
        members, _ = scan_reads(self.root)
        self.assertEqual(members['castMember'], {'AiPlayerbot.CastKey'})
        self.assertEqual(members['clampMember'], {'AiPlayerbot.ClampKey'})
        self.assertEqual(members['levelMember'], {'AiPlayerbot.LevelKey'})
        self.assertEqual(members['listMember'], {'AiPlayerbot.ListKey'})

    def test_direct_call_needs_no_member(self):
        members, (literal, _dynamic) = scan_reads(self.root)
        self.assertNotIn('SetOutputState', members)
        self.assertNotIn('DirectKey', {k for keys in members.values()
                                       for k in keys})

    def test_commented_read_is_not_a_read(self):
        members, _ = scan_reads(self.root)
        self.assertNotIn('mentionedInCommentOnly', members)

    def test_comment_mention_is_not_consumption(self):
        findings = check(self.root)
        self.assertTrue(any(f.startswith('deadMember: ') for f in findings))

    def test_dead_key_reported(self):
        findings = check(self.root)
        self.assertTrue(any(f.startswith('deadMember: ') for f in findings))

    def test_engine_consumption_passes(self):
        findings = [f for f in check(self.root) if f.startswith('usedMember')]
        self.assertEqual(findings, [])

    def test_wrapped_reads_consumed_elsewhere_pass(self):
        for member in ('castMember', 'clampMember', 'levelMember', 'listMember'):
            findings = [f for f in check(self.root)
                        if f.startswith(member + ':')]
            self.assertEqual(findings, [])

    def test_consumption_inside_the_config_file_counts(self):
        findings = [f for f in check(self.root) if f.startswith('configInternal')]
        self.assertEqual(findings, [])

    def test_snapshot_emit_is_display_only(self):
        findings = check(self.root)
        self.assertTrue(any(f.startswith('displayOnlyMember: ') and
                            'dashboard' in f for f in findings))

    def test_allowlist_silences_a_read(self):
        verify_config_usage.ALLOWLIST['deadMember'] = 'intentional: not consumed yet'
        try:
            findings = [f for f in check(self.root) if f.startswith('deadMember')]
            self.assertEqual(findings, [])
        finally:
            verify_config_usage.ALLOWLIST.clear()
class LiveRepoSmokeTest(unittest.TestCase):
    def test_the_real_repository_has_no_dead_reads(self):
        self.assertEqual(check(verify_config_usage.ROOT), [])


if __name__ == '__main__':
    unittest.main()
