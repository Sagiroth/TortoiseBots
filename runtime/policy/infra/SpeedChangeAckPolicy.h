#pragma once

// Forced speed changes are client-acked in this core: Unit::SetSpeedRate queues
// the change and SMSG_FORCE_*_SPEED_CHANGE asks the mover's client to answer
// CMSG_FORCE_*_SPEED_CHANGE_ACK. A bot's world session never answered, so every
// change sat in the pending queue for the 2+2 s resend timeout
// (Unit::CheckPendingMovementChanges) before it applied. The bot AI echoes the
// ACK itself; this header holds the wire mapping and the one writer of the
// field order that WorldSession::HandleForceSpeedChangeAckOpcodes reads.

#include <cstdint>

namespace TortoiseBots
{
namespace SpeedChangeAck
{

// Wire values of the core's 1.12 opcode table (src/game/Protocol/Opcodes.cpp;
// PlayerbotAIPackets.cpp and host/BotPacketAdapter.cpp static_assert them).
enum : uint16_t
{
    kServerWalk = 0x2DA,
    kServerRun = 0x0E2,
    kServerRunBack = 0x0E4,
    kServerSwim = 0x0E6,
    kServerSwimBack = 0x2DC,
    kServerTurnRate = 0x2DE,
    kClientWalk = 0x2DB,
    kClientRun = 0x0E3,
    kClientRunBack = 0x0E5,
    kClientSwim = 0x0E7,
    kClientSwimBack = 0x2DD,
    kClientTurnRate = 0x2DF,
};

// UnitMoveType order (UnitDefines.h): the ACK handler switches the client
// opcode back to the same index.
enum class Move : uint8_t
{
    Walk = 0,
    Run,
    RunBack,
    Swim,
    SwimBack,
    TurnRate,
};

inline constexpr uint8_t MoveIndex(Move move)
{
    return static_cast<uint8_t>(move);
}

inline bool MoveForServerOpcode(uint16_t opcode, Move& move)
{
    switch (opcode)
    {
        case kServerWalk: move = Move::Walk; return true;
        case kServerRun: move = Move::Run; return true;
        case kServerRunBack: move = Move::RunBack; return true;
        case kServerSwim: move = Move::Swim; return true;
        case kServerSwimBack: move = Move::SwimBack; return true;
        case kServerTurnRate: move = Move::TurnRate; return true;
        default: return false;
    }
}

inline uint16_t ClientOpcode(Move move)
{
    switch (move)
    {
        case Move::Walk: return kClientWalk;
        case Move::Run: return kClientRun;
        case Move::RunBack: return kClientRunBack;
        case Move::Swim: return kClientSwim;
        case Move::SwimBack: return kClientSwimBack;
        case Move::TurnRate: return kClientTurnRate;
    }
    return 0;
}

inline char const* MoveName(Move move)
{
    switch (move)
    {
        case Move::Walk: return "walk";
        case Move::Run: return "run";
        case Move::RunBack: return "run_back";
        case Move::Swim: return "swim";
        case Move::SwimBack: return "swim_back";
        case Move::TurnRate: return "turn_rate";
    }
    return "unknown";
}

// WorldSession::HandleForceSpeedChangeAckOpcodes reads, in order: the mover's
// guid, the counter the server sent it, the mover's MovementInfo, and the
// speed the server forced (the flat speed, not a rate). The guid is the full
// 8-byte form, unlike the packed guid of the server packet. The MovementInfo
// stays the core type so its layout is never copied here.
template <class Packet, class MovementInfo>
void WriteAck(Packet& ack, uint64_t guid, uint32_t counter, MovementInfo const& movementInfo, float speed)
{
    ack << guid;
    ack << counter;
    ack << movementInfo;
    ack << speed;
}

} // namespace SpeedChangeAck
} // namespace TortoiseBots
