#include "BotPacketAdapter.h"

#include "../runtime/BotManager.h"
#include "../runtime/PlayerbotAIStorage.h"
#include "playerbot/PlayerbotAI.h"

#include "Player.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Log.h"

namespace TortoiseBots
{

BotPacketAdapter::BotPacketAdapter()
    : ServerScript("tortoisebots_packets", {
        SERVERHOOK_CAN_PACKET_SEND,
        SERVERHOOK_CAN_PACKET_RECEIVE })
{
}

bool BotPacketAdapter::CanPacketSend(WorldSession* session, WorldPacket const& packet)
{
    if (!session)
        return true;

    if (session->IsHeadless())
    {
        if (packet.getOpcode() == SMSG_GROUP_INVITE)
            sLog.outDebug("TortoiseBots: ServerScript CanPacketSend bot outgoing SMSG_GROUP_INVITE from %s", session->GetPlayer() ? session->GetPlayer()->GetName() : "<none>");
        if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(session->GetPlayer()))
            ai->HandleBotOutgoingPacket(packet);
        return true;
    }

    Player* master = session->GetPlayer();
    if (!master)
        return true;

    for (Player* bot : BotManager::Instance().GetBotsForMaster(master->GetObjectGuid()))
    {
        if (packet.getOpcode() == SMSG_PARTY_COMMAND_RESULT)
        {
            WorldPacket result(packet);
            result.rpos(0);
            uint32 operation = 0;
            uint32 partyResult = 0;
            std::string member;
            result >> operation >> member >> partyResult;
            sLog.outDebug("TortoiseBots: ServerScript CanPacketSend master SMSG_PARTY_COMMAND_RESULT %s -> bot %s operation %u result %u member %s",
                master->GetName(), bot ? bot->GetName() : "<none>", operation, partyResult, member.c_str());
        }
        if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
            ai->HandleMasterOutgoingPacket(packet);
    }

    return true;
}

bool BotPacketAdapter::CanPacketReceive(WorldSession* session, WorldPacket const& packet)
{
    if (!session || session->IsHeadless())
        return true;

    DispatchMasterIncoming(session, packet);
    return true;
}

void BotPacketAdapter::DispatchMasterIncoming(WorldSession* session, WorldPacket const& packet)
{
    if (!session || session->IsHeadless())
        return;

    Player* master = session->GetPlayer();
    if (!master)
        return;

    if (packet.getOpcode() == CMSG_GROUP_UNINVITE ||
        packet.getOpcode() == CMSG_QUESTGIVER_ACCEPT_QUEST ||
        packet.getOpcode() == CMSG_GOSSIP_HELLO ||
        packet.getOpcode() == CMSG_LOOT_ROLL)
    {
        sLog.outString("TortoiseBots: ServerScript CanPacketReceive master opcode %u from %s", packet.getOpcode(), master->GetName());
    }

    for (Player* bot : BotManager::Instance().GetBotsForMaster(master->GetObjectGuid()))
    {
        if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
            ai->HandleMasterIncomingPacket(packet);
    }
}

} // namespace TortoiseBots
