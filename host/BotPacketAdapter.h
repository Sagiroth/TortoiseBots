#pragma once

#include "ScriptObjects.h"

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

    // Used by the native diagnostic journey to inject a packet at the same
    // module-local dispatch point without teaching core about PlayerbotAI.
    static void DispatchMasterIncoming(WorldSession* session, WorldPacket const& packet);
};

} // namespace TortoiseBots
