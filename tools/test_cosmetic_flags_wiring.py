"""The pool-bot helmet-cloak setting glue.

runtime/BotManager.cpp applies RandomBotShowHelmet/ShowCloak to pool bots on
login: 0 sets a hide flag, 1 clears it, and only masterless pool bots are
touched (record.random, not hired, not claimed), so hired companions and a
player's own characters keep the display flags of their characters. The setting
lives in the character flags, so a value change reaches existing bots on their
next login and survives restart.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MANAGER = (ROOT / "runtime/BotManager.cpp").read_text()


def block(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


COSMETIC = block(MANAGER, "// Helmet and cloak display on pool bots",
                 "// Normalize Goblin and High Elf")


class CosmeticFlagsWiringTest(unittest.TestCase):
    def test_flags_ride_on_regular_pool_bots_only(self):
        # The fence must read all three lifecycle gates inline: no helper
        # call may quietly widen who gets the character-level flags.
        self.assertIn("record.random && !HireLifecycle::Instance().IsHired(", COSMETIC)
        self.assertIn("!ClaimLifecycle::Instance().IsClaimed(", COSMETIC)
        self.assertNotIn("IsClaimedByPlayer", COSMETIC)

    def test_show_clears_the_hide_flags(self):
        self.assertIn("player->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM);", COSMETIC)
        self.assertIn("player->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK);", COSMETIC)

    def test_hiding_sets_the_hide_flags(self):
        self.assertIn("player->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM);", COSMETIC)
        self.assertIn("player->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK);", COSMETIC)

    def test_config_keys_read_with_default_true(self):
        config = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
        self.assertIn('config.GetBoolDefault("AiPlayerbot.RandomBotShowHelmet", true)', config)
        self.assertIn('config.GetBoolDefault("AiPlayerbot.RandomBotShowCloak", true)', config)

    def test_block_lives_on_the_headless_login_hook(self):
        hook = block(MANAGER, "void BotManager::OnPlayerLogin(::Player* player)",
                     "void BotManager::OnPlayerLogout")
        self.assertIn(COSMETIC[:120], hook)


if __name__ == '__main__':
    unittest.main()
