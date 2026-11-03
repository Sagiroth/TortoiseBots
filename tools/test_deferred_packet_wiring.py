"""The deferred packet drain gates every item through the delivery policy.

The policy (in-world / far-teleport / held count -> deliver, hold, drop) is
tested in tools/test_deferred_packet_policy.cpp. This pins the glue in
host/BotPacketAdapter.cpp: the drain hands both queues to DeliverOrHold, which
reads the bot's world/teleport state, re-queues a held item into its own queue,
and only reaches a handler after the policy says Deliver.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ADAPTER = (ROOT / "host/BotPacketAdapter.cpp").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class DrainGateWiringTest(unittest.TestCase):
    def test_drain_runs_items_through_the_delivery_gate(self):
        drain = body(ADAPTER, "void BotPacketAdapter::DrainDeferredPackets(uint64_t before, bool endOfTick)")
        self.assertIn("RunInSequence(packets, DeliverOrHold, chat, DeliverOrHold);", drain)
        self.assertNotIn("HandleBotOutgoingPacket", drain)
        self.assertNotIn("HandleMasterOutgoingPacket", drain)
        self.assertNotIn("HandleMasterIncomingPacket", drain)

    def test_gate_holds_and_drops_off_map_packets(self):
        gate = body(ADAPTER, "void DeliverOrHold(WorldThreadQueue<DeferredPacket>::Entry& entry)")
        self.assertIn("DeferredDelivery::Decide(bot->IsInWorld(), bot->IsBeingTeleportedFar(),", gate)
        self.assertIn("DeferredBotChat().Enqueue(entry.key, std::move(entry.item));", gate)
        self.assertIn("DeferredBotPackets().Enqueue(entry.key, std::move(entry.item));", gate)


if __name__ == "__main__":
    unittest.main()
