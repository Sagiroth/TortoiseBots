#pragma once

#include <cstddef>

class Player;
class WorldPacket;

namespace TortoiseBots
{

// C' — module-owned bot packet pump for synthesized client packets.
//
// Bot AI builds client packets and hands them here instead of
// WorldSession::QueuePacket, whose queue no headless session ever drains
// (CanProcessPackets() is socket-only; WorldSession.cpp:448).
//
// One Drain() per world tick dispatches them on the world thread after all AI
// updates and before BotManager's pending-removal drain, while the removal
// guard is still set (BotManager.cpp:922-924).
//
// This is NOT "validated dispatch". It bypasses the core's ProcessPackets
// wrapper (per-session script OnPacket hooks, SERVERHOOK_CAN_PACKET_RECEIVE,
// AllowPacket flood accounting, per-update cap), exactly like the module's
// existing direct session->Handle*() calls. Never describe it otherwise.
//
// The ExecuteOpcode teleport boundary (SetCanDelayTeleport + delayed-teleport
// execution, WorldSession.cpp:1061-1088) cannot be replicated module-side:
// those members are private to Player with WorldSession as the only friend
// (Player.h:1130; private section Player.h:1956-2049). This matches the
// mod-playerbots module pump, which also calls handlers raw.
class BotPacketPump
{
public:
    // Takes ownership of `packet` (drop-in for WorldSession::QueuePacket).
    static void Enqueue(Player* bot, WorldPacket* packet);
    // Dispatch queued packets for the current tick. World thread only.
    static void Drain();
};

} // namespace TortoiseBots
