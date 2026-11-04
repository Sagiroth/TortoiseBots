"""Unit and packet lookups must refuse a bot that is between maps (crash 2026-11-03).

The deferred packet drain handed a failed SMSG_CAST_RESULT to a bot whose far
teleport had not been acked yet. PlayerbotAI::GetUnit read the bot's map (GetMap
asserts on a mapless player), the assert threw on the world thread and mangosd
died. This pins the lookup side; the drain gate is tools/test_deferred_packet_policy.cpp
(policy) and tools/test_deferred_packet_wiring.py (glue):
- the five PlayerbotAI Get* helpers refuse an out-of-world bot before any map read
  and resolve the map with FindMap (which may be null) instead of GetMap;
- HandleBotOutgoingPacket refuses its map-reading opcodes on a mapless bot, which
  covers the packets handed over on the world thread without the drain.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PLAYERBOT_AI = (ROOT / "ai/playerbot/PlayerbotAI.cpp").read_text()
PACKETS = (ROOT / "ai/playerbot/PlayerbotAIPackets.cpp").read_text()
PLAYER_ADAPTER = (ROOT / "host/BotPlayerAdapter.cpp").read_text()
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class BotMapLookupGuardTest(unittest.TestCase):
    def test_get_helpers_refuse_a_bot_between_maps(self):
        helpers = [
            "Unit* PlayerbotAI::GetUnit(ObjectGuid guid)",
            "Creature* PlayerbotAI::GetCreature(ObjectGuid guid) const",
            "Creature* PlayerbotAI::GetAnyTypeCreature(ObjectGuid guid) const",
            "GameObject* PlayerbotAI::GetGameObject(ObjectGuid guid)",
            "WorldObject* PlayerbotAI::GetWorldObject(ObjectGuid guid)",
        ]
        for signature in helpers:
            with self.subTest(helper=signature):
                fn = body(PLAYERBOT_AI, signature)
                self.assertIn("!bot->IsInWorld()", fn)
                self.assertIn("FindMap()", fn)
                self.assertNotIn("bot->GetMap()", fn)

    def test_get_unit_keeps_the_bot_identity_lookup(self):
        fn = body(PLAYERBOT_AI, "Unit* PlayerbotAI::GetUnit(ObjectGuid guid)")
        self.assertIn("sObjectAccessor.GetUnit(*bot, guid)", fn)


class PacketHandlerMapGuardTest(unittest.TestCase):
    def test_map_reading_opcodes_return_before_the_switch(self):
        fn = body(PACKETS, "void PlayerbotAI::HandleBotOutgoingPacket(const WorldPacket& packet)")
        guard = fn.index("TortoiseBots::DeferredDelivery::ReadsBotMap(packet.getOpcode()) && !bot->FindMap()")
        self.assertLess(guard, fn.index("switch (packet.getOpcode())"))
        # The cast-result handler still looks its crowd-control target up.
        self.assertIn("GetUnit(last.target)", fn)


class MapChangeSummonTest(unittest.TestCase):
    def test_only_bots_still_in_the_dungeon_are_returned(self):
        # The sweep's bot filter is the pinned policy (the polarity had been
        # inverted: bots left inside stayed, bots already outside were summoned).
        fn = body(PLAYER_ADAPTER, "void BotPlayerAdapter::OnMapChanged(Player* player)")
        self.assertIn("PlayerMapChange::BotLeftInDungeon(botMap != nullptr, botMap && botMap->IsDungeon())", fn)
        policy = (ROOT / "host/PlayerMapChangePolicy.h").read_text()
        self.assertIn("return hasMap && mapIsDungeon;", policy)


class CommanderMapReadsTest(unittest.TestCase):
    def test_the_cc_break_read_uses_find_map(self):
        fn = body(COMMANDER, "void ReadCcBreakFacts(DungeonRunSlot const& slot, Player* lead")
        self.assertIn("Map* const map = lead->FindMap();", fn)
        self.assertIn("bot->FindMap() != map", fn)
        self.assertNotIn("GetMap()", fn)


if __name__ == "__main__":
    unittest.main()
