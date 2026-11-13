#pragma once

// Delivery gate of the deferred bot packet queues (host/BotPacketAdapter).
// A packet copied off the world thread must not reach the bot's AI while the
// bot is off its map: the drain runs before the bot update that acks a far
// teleport, so a mapless bot meets handlers that read GetMap(), which asserts.
// The drain holds such a packet for the next drain (chat, invites) or drops it
// when its handler would read the map anyway (cast result, spell failure,
// knock-back).

#include <cstdint>

#include "SpeedChangeAckPolicy.h"

namespace TortoiseBots
{
namespace DeferredDelivery
{

enum class Verdict
{
    Deliver,
    Hold,
    Drop,
};

// A held packet waits at most this long, in milliseconds, from the moment it
// was first held (wall clock, so every drain of a tick sees the same age). After
// that the bot has been between maps for longer than any legitimate teleport,
// so the packet is stale and dropped.
constexpr uint64_t kMaxHeldMs = 3000;

// Wire values of the server packets whose AI handler reads the bot's map
// (core Protocol/Opcodes_1_12_1.h; host/BotPacketAdapter.cpp static_asserts
// them against the core enum).
enum : uint16_t
{
    kSmsgMoveKnockBack = 0x0EF,
    kSmsgCastResult = 0x130,
    kSmsgSpellFailure = 0x133,
    kSmsgSpellDelayed = 0x1E2,
};

// The AI handlers that read the bot's map: its movement or cast target, and
// the forced speed change whose ack the core applies (and broadcasts) as a
// movement of the bot.
inline bool ReadsBotMap(uint16_t opcode)
{
    return opcode == kSmsgMoveKnockBack || opcode == kSmsgCastResult ||
        opcode == kSmsgSpellFailure || opcode == kSmsgSpellDelayed ||
        opcode == SpeedChangeAck::kServerWalk || opcode == SpeedChangeAck::kServerRun ||
        opcode == SpeedChangeAck::kServerRunBack || opcode == SpeedChangeAck::kServerSwim ||
        opcode == SpeedChangeAck::kServerSwimBack || opcode == SpeedChangeAck::kServerTurnRate;
}

inline Verdict Decide(bool inWorld, bool farTeleport, uint16_t opcode)
{
    if (inWorld && !farTeleport)
        return Verdict::Deliver;
    if (ReadsBotMap(opcode))
        return Verdict::Drop;
    return Verdict::Hold;
}

} // namespace DeferredDelivery
} // namespace TortoiseBots
