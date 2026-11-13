"""A player's bot keeps its saved strategies and values across a relog.

The AI is built before the adapter knows the master, so the snapshot load in
ResetStrategies (gated on a player relation) was skipped at login; value
commands (`ll`, `ss`) also never saved. The adapter now reloads after setting
the master and saves on logout while a real player master is set.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ADAPTER = (ROOT / "runtime" / "PlayerbotAIAdapter.cpp").read_text()


def body(start):
    i = ADAPTER.index(start)
    return ADAPTER[i:ADAPTER.index("\n}\n", i)]


class OwnedBotRelogWiringTest(unittest.TestCase):
    def test_login_reloads_after_master(self):
        init = body("bool PlayerbotAIAdapter::Initialize()")
        self.assertLess(init.index("ai_->SetMaster(master_);"), init.index("ai_->ResetStrategies();"))

    def test_snapshot_save_rules(self):
        save = body("void PlayerbotAIAdapter::SavePlayerSnapshot()")
        self.assertIn("ai_->HasRealPlayerMaster()", save)
        self.assertIn("!AltBotService::Instance().IsAutonomous(", save)
        self.assertIn("sPlayerbotAIConfig.bExplicitDbStoreSave", save)
        self.assertNotIn("sPlayerbotDbStore.Save", body("void PlayerbotAIAdapter::Shutdown()"))

    def test_bot_logout_saves_before_the_alt_session_ends(self):
        manager = (ROOT / "runtime" / "BotManager.cpp").read_text()
        i = manager.index("void BotManager::OnPlayerBeforeLogout(")
        logout = manager[i:manager.index("\n}\n", i)]
        self.assertLess(logout.index("SavePlayerSnapshot()"), logout.index("AltBotService::Instance().OnBotLogout(player);"))


if __name__ == "__main__":
    unittest.main()
