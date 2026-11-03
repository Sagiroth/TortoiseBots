#pragma once

#include "ScriptObjects.h"

#include <cstdint>

namespace TortoiseBots
{

// Bridges Penqle's generic packet hooks into the mature PlayerbotAI packet
// queues. The core does not know why a session is headless or which AI owns a
// player; that interpretation stays here.
class BotPacketAdapter final : public ServerScript
{
public:
    BotPacketAdapter();

    bool CanPacketSend(WorldSession* session, WorldPacket const& packet) override;
    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override;

    // World thread, first thing every tick (BotHostAdapter::OnUpdate, before
    // the command drain): records this thread as the world thread, so packet
    // hooks running on it hand packets to the AI at once (issue #844).
    static void MarkWorldThread();
    // World thread only (BotHostAdapter::OnUpdate, before the bot update):
    // hands the packets queued by the hooks since the last tick to the bots'
    // AIs - chat (issue #839) and every other bot / master packet an AI
    // handles (issue #844) - in the order they were queued, and only those
    // queued ahead of sequence number `before` (issue #846: the command drain
    // runs them up to each command first). `endOfTick` also reports what the
    // per-bot caps dropped.
    static void DrainDeferredPackets(uint64_t before, bool endOfTick = false);
    // World thread: true when any bot of this master has a held-packet backlog.
    // The command drain uses it to run a `.bot` command after the chat and
    // packets that bot is still waiting on, never before them.
    static bool HasHeldPacketsForMaster(uint64_t masterGuid);
    // World thread, at shutdown: forget queued packets.
    static void ClearDeferredPackets();

private:
    void DispatchMasterIncoming(WorldSession* session, WorldPacket const& packet);
    // Raid anti-spam: when more than kMimicSummaryBots bots share the master,
    // the mimic actions stay silent and the master gets one summary line.
    static constexpr uint32_t kMimicSummaryBots = 4;
    void MaybeSendMimicSummary(Player* master, WorldPacket const& packet);
};

} // namespace TortoiseBots
