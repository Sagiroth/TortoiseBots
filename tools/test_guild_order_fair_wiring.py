"""Guild order fairness wiring: the player Buy gate, the preview command and the capability.

The Buy gate sits only where the player's order comes in (HandleOrderGive):
a player's `.bot order <bot> buy` is refused while
`AiPlayerbot.GuildOrderPlayerBuy` is off, with a stable ERR code the addon
reads. The supply chain (sub-orders, the AH step inside farm/craft orders)
never asks the gate. The preview is read-only (no Give, no SetSupply, no
state write) and answers with one TBM:ORDER_PREVIEW line. The CAPS line
carries `guild-order-nobuy` while player Buy is refused.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ORDER = (ROOT / "commands/Order.cpp").read_text()
PREVIEW = (ROOT / "commands/OrderPreview.cpp").read_text()
INFO = (ROOT / "commands/Info.cpp").read_text()
POLICY = (ROOT / "runtime/GuildOrderPlayerBuyPolicy.h").read_text()


class GuildOrderFairWiringTest(unittest.TestCase):
    def test_gate_sits_in_give_only(self):
        give_start = ORDER.index("bool HandleOrderGive(")
        give_end = ORDER.index("\n}\n", give_start)
        give = ORDER[give_start:give_end]
        self.assertIn("PlayerBuyOrderRefused(type, sPlayerbotAIConfig.guildOrderPlayerBuy)", give)
        self.assertIn("PlayerBuyRefusalCode()", give)
        rest = ORDER[:give_start] + ORDER[give_end:]
        self.assertNotIn("PlayerBuyOrderRefused", rest)

    def test_gate_uses_stable_code(self):
        self.assertIn('"buy-disabled"', POLICY)
        self.assertIn('"guild-order-nobuy"', POLICY)

    def test_supply_chain_never_asks_gate(self):
        store = (ROOT / "runtime/GuildOrderStore.cpp").read_text()
        self.assertNotIn("guildOrderPlayerBuy", store)
        self.assertNotIn("PlayerBuyOrderRefused", store)
        values = (ROOT / "ai/playerbot/strategy/values/GuildValues.cpp").read_text()
        self.assertNotIn("guildOrderPlayerBuy", values)
        self.assertNotIn("PlayerBuyOrderRefused", values)

    def test_preview_dispatch(self):
        self.assertIn('lowerSecond == "preview"', ORDER)
        self.assertIn("HandleOrderPreview(", ORDER)
        self.assertIn("order <bot> preview <farm|craft> <item> <amount>", ORDER)

    def test_preview_is_read_only(self):
        self.assertIn("PlanGuildOrderSources(bot, itemId, type)", PREVIEW)
        self.assertIn("CollectBaseReagents(itemId, outputMissing", PREVIEW)
        self.assertIn("TBM:ORDER_PREVIEW|", PREVIEW)
        self.assertNotIn("::Give(", PREVIEW)
        self.assertNotIn("SetSupply(", PREVIEW)
        self.assertNotIn("SetState(", PREVIEW)
        self.assertNotIn("SetNeedItems(", PREVIEW)
        self.assertNotIn("RequestSupply(", PREVIEW)

    def test_preview_uses_shared_verdicts(self):
        self.assertIn("CraftOrderRefusal(plan.verdict)", PREVIEW)
        self.assertIn("PlanOrderDifficulty(bot, itemId, type, amount)", PREVIEW)

    def test_capability_token_present(self):
        self.assertIn("guild-order-nobuy", INFO)
        self.assertIn("playerBuyOff", INFO)

    def test_config_default_off(self):
        config = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
        self.assertIn('"AiPlayerbot.GuildOrderPlayerBuy", false', config)
        template = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
        self.assertIn("AiPlayerbot.GuildOrderPlayerBuy = 0", template)


if __name__ == "__main__":
    unittest.main()
