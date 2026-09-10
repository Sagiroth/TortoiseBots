#include "BotPacketPump.h"

#include <deque>
#include <exception>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "ByteBuffer.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Timer.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "../runtime/BotManager.h"

namespace TortoiseBots
{
namespace
{
// Core revision this pump was written against: b74e4ee (bot-helpers).
// Opcode table / handler dispatch reference: src/game/Protocol/Opcodes.h:53-104,
// WorldSession::ExecuteOpcode reference: src/game/WorldSession.cpp:1061-1088.
constexpr size_t kMaxQueuedPackets = 256;
constexpr const char* kPumpLogPrefix = "TortoiseBots: packet pump";

struct QueuedPacket
{
    ObjectGuid botGuid;
    std::unique_ptr<WorldPacket> packet;
};

// World-thread only: BotManager's update tick enqueues, BotPacketPump::Drain
// consumes (Module invariant, host/BotPacketPump.h). No mutex while that holds.
std::deque<QueuedPacket> g_queue;
std::set<uint16> g_reportedDropOpcodes;
bool g_reportedCommandChat = false;
uint32 g_droppedForBound = 0;

void LogDropOnce(uint16 opcode, char const* reason)
{
    if (g_reportedDropOpcodes.insert(opcode).second)
        sLog.outError("%s: dropping opcode %u (0x%04X): %s",
            kPumpLogPrefix, static_cast<uint32>(opcode), static_cast<uint32>(opcode), reason);
}

// Chat that starts with '.' or '!' is parsed as a command by
// ChatHandler::ParseCommands (Chat.cpp:1763-1788) on the party/guild/say/yell
// path (ChatHandler.cpp:284-301). Bots are SEC_PLAYER, but player commands can
// be enabled server-side, so neutralize the whole packet rather than rewrite
// the length-prefixed buffer.
bool IsChatCommandLike(WorldPacket& packet)
{
    size_t savedRpos = packet.rpos();
    try
    {
        uint32 type = 0;
        uint32 language = 0;
        packet >> type >> language;

        std::string leading;
        if (type == CHAT_MSG_CHANNEL || type == CHAT_MSG_WHISPER)
            packet >> leading;

        std::string message;
        packet >> message;
        packet.rpos(savedRpos);
        return !message.empty() && (message[0] == '.' || message[0] == '!');
    }
    catch (ByteBufferException&)
    {
        packet.rpos(savedRpos);
        return false; // malformed layout: let the dispatch catch decide
    }
}

void DispatchOne(QueuedPacket& entry)
{
    ::Player* bot = sObjectAccessor.FindPlayer(entry.botGuid);
    if (!bot || !bot->IsInWorld())
        return;

    ::WorldSession* session = bot->GetSession();
    if (!session || session->GetPlayer() != bot || !session->IsHeadless() || session->HasNetworkTransport())
        return;

    BotRecord* record = BotManager::Instance().FindBot(entry.botGuid);
    if (!record || record->lifecycle != BotLifecycle::InWorld)
        return;

    std::unique_ptr<WorldPacket> packet = std::move(entry.packet);
    uint16 const opcode = packet->GetOpcode();

    // Socket ingress stamps every packet (WorldSocket.cpp:64). Synthesized
    // packets carry 0, which HandleMovementOpcodes rejects against
    // m_moveRejectTime (MovementHandler.cpp:301).
    packet->FillPacketTime(WorldTimer::getMSTime());

    if (opcode == CMSG_MESSAGECHAT && IsChatCommandLike(*packet))
    {
        if (!g_reportedCommandChat)
        {
            g_reportedCommandChat = true;
            sLog.outError("%s: dropped bot chat starting with '.' or '!' (command parsing is not available to bots)",
                kPumpLogPrefix);
        }
        return;
    }

    OpcodeHandler const* opHandle = opcodeTable.LookupOpcode(opcode);
    if (!opHandle || !opHandle->handler || opHandle->status != STATUS_LOGGEDIN)
    {
        LogDropOnce(opcode, "no STATUS_LOGGEDIN handler");
        return;
    }

    packet->rpos(0);
    try
    {
        (session->*opHandle->handler)(*packet);
    }
    catch (ByteBufferException&)
    {
        sLog.outError("%s: ByteBufferException dispatching opcode %u (0x%04X) for bot %s; packet dropped",
            kPumpLogPrefix, static_cast<uint32>(opcode), static_cast<uint32>(opcode), bot->GetName());
    }
    catch (std::exception const& e)
    {
        sLog.outError("%s: exception dispatching opcode %u (0x%04X) for bot %s: %s",
            kPumpLogPrefix, static_cast<uint32>(opcode), static_cast<uint32>(opcode), bot->GetName(), e.what());
    }
    catch (...)
    {
        sLog.outError("%s: unknown exception dispatching opcode %u (0x%04X) for bot %s",
            kPumpLogPrefix, static_cast<uint32>(opcode), static_cast<uint32>(opcode), bot->GetName());
    }
}
} // namespace

void BotPacketPump::Enqueue(Player* bot, WorldPacket* packet)
{
    if (!packet)
        return;

    if (!bot)
    {
        delete packet;
        return;
    }

    if (g_queue.size() >= kMaxQueuedPackets)
    {
        g_queue.pop_front();
        if (++g_droppedForBound == 1 || g_droppedForBound % 100 == 0)
            sLog.outError("%s: queue full (%u); dropped oldest queued packet",
                kPumpLogPrefix, static_cast<uint32>(kMaxQueuedPackets));
    }

    QueuedPacket entry;
    entry.botGuid = bot->GetObjectGuid();
    entry.packet.reset(packet);
    g_queue.push_back(std::move(entry));
}

void BotPacketPump::Enqueue(Player* bot, WorldPacket const& packet)
{
    if (!bot)
        return;

    Enqueue(bot, new WorldPacket(packet));
}

void BotPacketPump::Drain()
{
    if (g_queue.empty())
        return;

    // Snapshot the tick's work; packets enqueued by dispatched handlers wait
    // for the next tick, which also bounds reentrancy.
    std::deque<QueuedPacket> batch;
    batch.swap(g_queue);

    for (QueuedPacket& entry : batch)
        DispatchOne(entry);
}

} // namespace TortoiseBots
