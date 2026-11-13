"""Bots answer the core's forced speed change (issue #1604).

The layout of the ack is the policy test tools/test_speed_change_ack_policy.cpp.
This pins the glue: PlayerbotAIPackets.cpp must handle every SMSG_FORCE_*
speed packet the core can send, echo the counter and forced speed it received,
call the core handler, keep the existing "check mount state" dispatch, never
ack for a real player's session, and only log the SpeedChange diagnostic under
AiPlayerbot.RaidBossDiagnostics and at most once a second per bot.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKETS = (ROOT / "ai/playerbot/PlayerbotAIPackets.cpp").read_text()
AI = (ROOT / "ai/playerbot/PlayerbotAI.cpp").read_text()
DELIVERY = (ROOT / "runtime/policy/infra/DeferredDeliveryPolicy.h").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


def speed_block():
    start = PACKETS.index("case SMSG_FORCE_WALK_SPEED_CHANGE:")
    return PACKETS[start:PACKETS.index("case SMSG_MOVE_KNOCK_BACK:", start)]


class SpeedCasesTest(unittest.TestCase):
    def test_every_core_speed_packet_has_a_case(self):
        block = speed_block()
        for opcode in ("SMSG_FORCE_WALK_SPEED_CHANGE", "SMSG_FORCE_RUN_SPEED_CHANGE",
                       "SMSG_FORCE_RUN_BACK_SPEED_CHANGE", "SMSG_FORCE_SWIM_SPEED_CHANGE",
                       "SMSG_FORCE_SWIM_BACK_SPEED_CHANGE", "SMSG_FORCE_TURN_RATE_CHANGE"):
            self.assertIn(f"case {opcode}:", block)

    def test_the_block_echoes_what_the_server_sent(self):
        block = speed_block()
        self.assertIn("p >> guid.ReadAsPacked() >> counter >> speed;", block)
        self.assertIn("SpeedChangeAck::WriteAck(ack, guid.GetRawValue(), counter, bot->m_movementInfo, speed)", block)
        self.assertIn("bot->GetSession()->HandleForceSpeedChangeAckOpcodes(ack);", block)

    def test_only_a_headless_bot_session_acks(self):
        block = speed_block()
        guard = block.index("!bot->GetSession()->IsHeadless()")
        self.assertLess(guard, block.index("HandleForceSpeedChangeAckOpcodes(ack)"))
        # The ack is built from the bot's own guid; a pet's change is not acked
        # with the wrong mover.
        self.assertIn("if (guid != bot->getObjectGuid())", block)

    def test_the_mount_check_dispatch_is_kept(self):
        self.assertIn("botOutgoingPacketHandlers.AddPacket(packet);", speed_block())
        self.assertIn('{ SMSG_FORCE_RUN_SPEED_CHANGE, "check mount state", false },', PACKETS)

    def test_the_diagnostic_is_opt_in_throttled_and_named(self):
        block = speed_block()
        self.assertIn("sPlayerbotAIConfig.raidBossDiagnostics", block)
        self.assertIn('sPlayerbotAIConfig.logEvent(this, "SpeedChange"', block)
        self.assertIn("speedChangeAckLogMs", block)
        self.assertIn(">= 1000", block)
        self.assertIn("HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED)", block)
        self.assertIn("GetSpeedRate(type)", block)
        self.assertIn('" counter=" << counter', block)

    def test_knockback_is_untouched(self):
        knock = body(PACKETS, "case SMSG_MOVE_KNOCK_BACK: // handle knockbacks")
        self.assertIn("WorldPacket ack(CMSG_MOVE_KNOCK_BACK_ACK);", knock)
        self.assertIn("bot->GetSession()->HandleMoveKnockBackAck(ack);", knock)


class HandledOpcodeTest(unittest.TestCase):
    def test_the_copy_filter_lists_the_speed_packets(self):
        fn = body(AI, "bool PlayerbotAI::HandlesBotOutgoingOpcode(uint16 opcode)")
        for opcode in ("SMSG_FORCE_WALK_SPEED_CHANGE", "SMSG_FORCE_RUN_BACK_SPEED_CHANGE",
                       "SMSG_FORCE_SWIM_SPEED_CHANGE", "SMSG_FORCE_SWIM_BACK_SPEED_CHANGE",
                       "SMSG_FORCE_TURN_RATE_CHANGE"):
            self.assertIn(opcode, fn)

    def test_between_maps_the_ack_is_dropped_like_the_other_map_readers(self):
        fn = body(DELIVERY, "inline bool ReadsBotMap(uint16_t opcode)")
        for name in ("kServerWalk", "kServerRun", "kServerRunBack", "kServerSwim",
                     "kServerSwimBack", "kServerTurnRate"):
            self.assertIn(f"SpeedChangeAck::{name}", fn)


if __name__ == "__main__":
    unittest.main()
