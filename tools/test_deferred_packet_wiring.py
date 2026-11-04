"""The deferred packet drain gates every item through the delivery policy.

The policy (in-world / far-teleport -> deliver, hold, drop) is tested in
tools/test_deferred_packet_policy.cpp. This pins the glue in
host/BotPacketAdapter.cpp: the drain hands both queues to DeliverOrHold, which
reads the bot's world/teleport state, keeps held items in a per-bot backlog in
their original sequence, and only reaches a handler after the policy says
Deliver or the backlog re-delivers.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ADAPTER = (ROOT / "host/BotPacketAdapter.cpp").read_text()
CHAT = (ROOT / "host/BotChatAdapter.cpp").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class DrainGateWiringTest(unittest.TestCase):
    def test_drain_runs_items_through_the_delivery_gate(self):
        drain = body(ADAPTER, "void BotPacketAdapter::DrainDeferredPackets(uint64_t before, bool endOfTick)")
        self.assertIn("RunInSequence(packets, DeliverOrHold, chat, DeliverOrHold);", drain)
        self.assertIn("g_heldBacklogs", drain)
        self.assertNotIn("HandleBotOutgoingPacket", drain)
        self.assertNotIn("HandleMasterOutgoingPacket", drain)
        self.assertNotIn("HandleMasterIncomingPacket", drain)

    def test_gate_holds_and_drops_off_map_packets(self):
        gate = body(ADAPTER, "void DeliverOrHold(WorldThreadQueue<DeferredPacket>::Entry& entry)")
        self.assertIn("DeferredDelivery::Decide(bot->IsInWorld(), bot->IsBeingTeleportedFar(),", gate)
        self.assertIn("g_heldBacklogs.Append(entry.key, entry.seq, std::move(entry.item), SteadyNowMs());", gate)
        self.assertIn("g_heldBacklogs.Contains(entry.key)", gate)

    def test_backlog_is_the_unit_tested_header(self):
        # The order, the before-bound and the staleness are the header class,
        # covered by tools/test_bot_packet_backlog.cpp.
        self.assertIn("BotPacketBacklog<DeferredPacket> g_heldBacklogs;", ADAPTER)
        self.assertIn("g_heldBacklogs.Drain(before, SteadyNowMs(), DeferredDelivery::kMaxHeldMs,", ADAPTER)
        held = body(ADAPTER, "bool BotPacketAdapter::HasHeldPacketsBefore(uint64_t masterGuid, uint64_t before)")
        self.assertIn("g_heldBacklogs.AnyBefore(", held)


class DirectHandOffWiringTest(unittest.TestCase):
    def test_world_thread_hand_offs_use_the_same_gate(self):
        # A packet arriving on the world thread must not overtake the bot's
        # backlog, and a master-outgoing packet must not reach an AI whose bot
        # is between maps.
        direct = body(ADAPTER, "void DeliverNowOrHold(uint64_t botGuid, PacketRoute route, WorldPacket const& packet)")
        self.assertIn("g_heldBacklogs.Contains(botGuid)", direct)
        self.assertIn("g_heldBacklogs.Append(botGuid, NextWorldQueueSequence(),", direct)
        self.assertIn("DeferredDelivery::Decide(bot->IsInWorld(), bot->IsBeingTeleportedFar(), packet.getOpcode())",
            direct)
        self.assertIn("DeliverNowOrHold(botGuid.GetRawValue(), PacketRoute::BotOutgoing, packet);", ADAPTER)
        self.assertIn("DeliverNowOrHold(botGuid, PacketRoute::MasterOutgoing, packet);", ADAPTER)

    def test_master_incoming_world_thread_goes_through_the_gate(self):
        # A quest accept / gossip / loot roll / uninvite on the world thread must
        # not overtake the bot's older held packets (M3 in test_bot_packet_backlog.cpp).
        dispatch = body(ADAPTER, "void BotPacketAdapter::DispatchMasterIncoming(WorldSession* session, WorldPacket const& packet)")
        self.assertIn("DeliverNowOrHold(botGuid, PacketRoute::MasterIncoming, packet);", dispatch)
        self.assertNotIn("HandleMasterIncomingPacket", dispatch)


class CommandDeferWiringTest(unittest.TestCase):
    def test_command_drain_runs_before_the_defer_check(self):
        # tools/test_bot_packet_backlog.cpp proves the gate order; this pins
        # that the chat adapter calls it with the adapter's drain and check.
        self.assertIn("return DeferCommandForHeldPackets(requester->GetObjectGuid().GetRawValue(), command.seq,",
            CHAT)
        self.assertIn("BotPacketAdapter::DrainDeferredPackets(before)", CHAT)
        self.assertIn("BotPacketAdapter::HasHeldPacketsBefore(masterGuid, before)", CHAT)


if __name__ == "__main__":
    unittest.main()
