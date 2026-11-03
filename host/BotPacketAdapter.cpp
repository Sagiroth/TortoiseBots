#include "BotPacketAdapter.h"
#include "RaidBossDiagnostics.h"

#include "../runtime/BotChatGate.h"
#include "../runtime/BotLfgWhisperInbox.h"
#include "../runtime/BotManager.h"
#include "../runtime/PlayerbotAIStorage.h"
#include "../runtime/WorldThreadQueue.h"
#include "../runtime/policy/infra/DeferredDeliveryPolicy.h"
#include "playerbot/PlayerbotAI.h"

#include "ObjectMgr.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Log.h"
#include "ModuleLog.h"

#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace TortoiseBots
{

BotPacketAdapter::BotPacketAdapter()
    : ServerScript("tortoisebots_packets", {
        SERVERHOOK_CAN_PACKET_SEND,
        SERVERHOOK_CAN_PACKET_RECEIVE })
{
}

namespace
{
// Issue #844: every drained item is handed to the bot's AI or held while the
// bot is between maps. The route picks the handler, and the queue a held item
// goes back to (DeferredDeliveryPolicy.h).
enum class PacketRoute : uint8_t
{
    BotChat,
    BotOutgoing,
    MasterOutgoing,
    MasterIncoming,
};

struct DeferredPacket
{
    PacketRoute route;
    std::shared_ptr<WorldPacket const> packet;
    // Drains that already held this packet while the bot was between maps.
    uint8_t heldDrains = 0;
};

// Issue #839: SMSG_MESSAGECHAT delivered to a bot's headless session arrives on
// the sender's thread - AsyncPackets for party / raid / guild / whisper,
// ChannelBroadcaster for channels, a map thread for say / yell. Its handler
// (PlayerbotAI::HandleBotOutgoingPacket) runs whisper and party commands and
// writes chat state, so the hook only copies the packet here and the world
// hook runs it before the bot update. 64 per bot per tick is far above what a
// master types; a bot in a flooded channel drops the excess, as its reply
// logic ignores most channel lines anyway.
constexpr size_t kMaxDeferredChatPerBot = 64;

WorldThreadQueue<DeferredPacket>& DeferredBotChat()
{
    static WorldThreadQueue<DeferredPacket> queue(kMaxDeferredChatPerBot);
    return queue;
}

// The chat types PlayerbotAI::HandleBotOutgoingPacket reacts to; everything
// else (system, monster, emote, ...) is ignored there, so it is not copied.
bool IsBotHandledChatType(WorldPacket const& packet)
{
    if (packet.size() < 1)
        return false;
    switch (packet.contents()[0])
    {
        case CHAT_MSG_CHANNEL:
        case CHAT_MSG_SAY:
        case CHAT_MSG_PARTY:
        case CHAT_MSG_YELL:
        case CHAT_MSG_WHISPER:
        case CHAT_MSG_GUILD:
            return true;
        default:
            return false;
    }
}

// Issue #844: every other packet a bot's AI reacts to (bot outgoing, master
// outgoing and master incoming) is copied here too and handed to the AI on the
// world thread. A packet hook may run on AsyncPackets or ChannelBroadcaster
// while the world thread deletes that bot's AI (logout, removal), and the AI
// storage lock covers only the lookup, so no hook keeps a PlayerbotAI pointer.
// Only opcodes with an AI handler are copied (PlayerbotAI::Handles*Opcode);
// those are rare next to chat (invites, loot, quest updates, cast results),
// so 256 per bot per tick is never reached in play and only bounds a flood.
// A master packet is copied once and shared by the bots it is queued for.
constexpr size_t kMaxDeferredPacketsPerBot = 256;

WorldThreadQueue<DeferredPacket>& DeferredBotPackets()
{
    static WorldThreadQueue<DeferredPacket> queue(kMaxDeferredPacketsPerBot);
    return queue;
}

// The delivery policy names the wire values of the AI handlers that read the
// bot's map; the build fails if they drift from the core's opcodes.
static_assert(TortoiseBots::DeferredDelivery::kSmsgMoveKnockBack == SMSG_MOVE_KNOCK_BACK,
    "DeferredDelivery::kSmsgMoveKnockBack no longer matches SMSG_MOVE_KNOCK_BACK");
static_assert(TortoiseBots::DeferredDelivery::kSmsgCastResult == SMSG_CAST_RESULT,
    "DeferredDelivery::kSmsgCastResult no longer matches SMSG_CAST_RESULT");
static_assert(TortoiseBots::DeferredDelivery::kSmsgSpellFailure == SMSG_SPELL_FAILURE,
    "DeferredDelivery::kSmsgSpellFailure no longer matches SMSG_SPELL_FAILURE");
static_assert(TortoiseBots::DeferredDelivery::kSmsgSpellDelayed == SMSG_SPELL_DELAYED,
    "DeferredDelivery::kSmsgSpellDelayed no longer matches SMSG_SPELL_DELAYED");

// The world thread deletes AIs, so a hook running on it can hand a packet to
// the AI at once, as before: the AI's own casts, hire provisioning (which drops
// its queued level-up echo right after the jump) and world-processed packets
// keep their timing. Marked at the top of every world tick
// (BotHostAdapter::OnUpdate); until the first tick every hook defers.
WorldThreadMark& WorldThread()
{
    static WorldThreadMark mark;
    return mark;
}

bool OnWorldThread()
{
    return WorldThread().IsCurrent();
}

// False when the bot's AI would drop this chat line before it does anything
// with it (runtime/BotChatGate.h), so it is not copied.
bool BotWantsChat(TortoiseBots::BotChatGate const& gate, WorldPacket const& packet)
{
    if (gate.ignoreAllChat)
        return false;
    if (!gate.onlyOrders)
        return true;
    uint8 const type = packet.contents()[0];
    return type == CHAT_MSG_PARTY || type == CHAT_MSG_WHISPER;
}

// Issue #846: drops over a queue's cap are logged at debug level, once per bot
// per minute at most, with the count since the last line.
constexpr uint64_t kDropReportIntervalMs = 60 * 1000;

uint64_t SteadyNowMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void ReportDrops(char const* what, std::unordered_map<uint64_t, uint64_t> const& dropped, DropReportLimiter& limiter)
{
    for (auto const& entry : dropped)
        limiter.Add(entry.first, entry.second);
    limiter.Flush(SteadyNowMs(), [what](uint64_t botGuid, uint64_t count)
    {
        TB_LOG_DEBUG("TortoiseBots: dropped %llu deferred %s for bot guid %u (over the per-bot per-tick cap)",
            static_cast<unsigned long long>(count), what, ObjectGuid(botGuid).GetCounter());
    });
}

// Live AI of a bot named by raw guid. Only where no AI can be deleted while
// the pointer is used: the world thread (the only one that deletes AIs) or a
// map thread (the world thread waits in MapManager::Update meanwhile).
PlayerbotAI* FindBotAI(uint64_t botGuid)
{
    return PlayerbotAIStorage::Instance().GetAI(ObjectGuid(botGuid));
}

// World thread: hand one drained item to its AI. A bot between maps (a far
// teleport the bot update has not acked yet) must not run a handler that reads
// its map, so the item is held for the next drain, or dropped once it is stale
// (DeferredDeliveryPolicy.h). A bot whose AI left the storage (logout,
// removal) since the copy took its queue with it.
void DeliverOrHold(WorldThreadQueue<DeferredPacket>::Entry& entry)
{
    PlayerbotAI* ai = FindBotAI(entry.key);
    if (!ai || !ai->GetBot())
        return;
    Player* bot = ai->GetBot();
    switch (DeferredDelivery::Decide(bot->IsInWorld(), bot->IsBeingTeleportedFar(),
                entry.item.packet->getOpcode(), entry.item.heldDrains))
    {
        case DeferredDelivery::Verdict::Deliver:
            switch (entry.item.route)
            {
                case PacketRoute::BotChat:
                case PacketRoute::BotOutgoing:
                    ai->HandleBotOutgoingPacket(*entry.item.packet);
                    break;
                case PacketRoute::MasterOutgoing:
                    ai->HandleMasterOutgoingPacket(*entry.item.packet);
                    break;
                case PacketRoute::MasterIncoming:
                    ai->HandleMasterIncomingPacket(*entry.item.packet);
                    break;
            }
            break;
        case DeferredDelivery::Verdict::Hold:
            ++entry.item.heldDrains;
            if (entry.item.route == PacketRoute::BotChat)
                DeferredBotChat().Enqueue(entry.key, std::move(entry.item));
            else
                DeferredBotPackets().Enqueue(entry.key, std::move(entry.item));
            break;
        case DeferredDelivery::Verdict::Drop:
            break;
    }
}
} // namespace

bool BotPacketAdapter::CanPacketSend(WorldSession* session, WorldPacket const& packet)
{
    if (!session)
        return true;

    RaidBossDiagnostics::ObservePacket(session, packet);

    if (session->IsHeadless())
    {
        // Issue #844: any thread. Off the world thread nothing here
        // dereferences the bot's Player or AI; the storage maps the session's
        // Player pointer to the guid the deferred copy is keyed by, and only
        // bots with an AI have one.
        uint16 const opcode = packet.getOpcode();
        if (!PlayerbotAI::HandlesBotOutgoingOpcode(opcode))
            return true;
        ObjectGuid botGuid;
        BotChatGate gate;
        if (!PlayerbotAIStorage::Instance().GetBotGuid(session->GetPlayer(), botGuid,
                opcode == SMSG_MESSAGECHAT ? &gate : nullptr))
            return true;
        if (opcode == SMSG_GROUP_INVITE)
            TB_LOG_DEBUG("TortoiseBots: ServerScript CanPacketSend bot outgoing SMSG_GROUP_INVITE to bot guid %u", botGuid.GetCounter());
        // A real player whispering an advertising bot LFG leader talks to BotLfgService, not to the AI.
        if (opcode == SMSG_MESSAGECHAT && BotLfgWhisperInbox::Instance().Offer(botGuid.GetRawValue(), packet.contents(), packet.size(), CHAT_MSG_WHISPER, LANG_ADDON))
            return true;

        if (opcode != SMSG_MESSAGECHAT && OnWorldThread())
        {
            if (PlayerbotAI* ai = FindBotAI(botGuid.GetRawValue()))
                ai->HandleBotOutgoingPacket(packet);
            return true;
        }

        // Issue #839: chat has its own per-bot FIFO and cap, so a flooded
        // channel cannot crowd out invites or loot.
        if (opcode == SMSG_MESSAGECHAT)
        {
            if (IsBotHandledChatType(packet) && BotWantsChat(gate, packet))
                DeferredBotChat().Enqueue(botGuid.GetRawValue(),
                    DeferredPacket{ PacketRoute::BotChat, std::make_shared<WorldPacket const>(packet) });
            return true;
        }

        DeferredBotPackets().Enqueue(botGuid.GetRawValue(),
            DeferredPacket{ PacketRoute::BotOutgoing, std::make_shared<WorldPacket const>(packet) });
        return true;
    }

    Player* master = session->GetPlayer();
    if (!master)
        return true;
    if (!PlayerbotAI::HandlesMasterOutgoingOpcode(packet.getOpcode()))
        return true;

    // Issue #839 / #844: any thread. Published guid view; off the world
    // thread the copy is handed to each bot's AI by the world-thread drain.
    bool const onWorldThread = OnWorldThread();
    std::shared_ptr<WorldPacket const> copy;
    for (uint64_t botGuid : BotManager::Instance().GetBotGuidsForMasterAnyThread(master->GetObjectGuid()))
    {
        if (packet.getOpcode() == SMSG_PARTY_COMMAND_RESULT)
            TB_LOG_DEBUG("TortoiseBots: ServerScript CanPacketSend master SMSG_PARTY_COMMAND_RESULT %s -> bot guid %llu",
                master->GetName(), static_cast<unsigned long long>(botGuid));
        if (onWorldThread)
        {
            if (PlayerbotAI* ai = FindBotAI(botGuid))
                ai->HandleMasterOutgoingPacket(packet);
            continue;
        }
        if (!copy)
            copy = std::make_shared<WorldPacket const>(packet);
        DeferredBotPackets().Enqueue(botGuid, DeferredPacket{ PacketRoute::MasterOutgoing, copy });
    }

    return true;
}

void BotPacketAdapter::MarkWorldThread()
{
    WorldThread().MarkCurrent();
}

void BotPacketAdapter::DrainDeferredPackets(uint64_t before, bool endOfTick)
{
    // Issue #844: resolved again on the world thread, so a bot whose AI left
    // the storage (logout, removal) since the copy is skipped, and a bot
    // between maps holds its packets instead of running a map-reading handler
    // (DeliverOrHold). Issue #846: the two queues run merged in the order their
    // items were queued, so each bot's chat and packets reach its AI in
    // arrival order, and only items queued before `before` run (the command
    // drain passes each command's number, so chat typed before a `.bot`
    // command runs before it). A held item is queued again with a new number,
    // so it waits for the next drain.
    std::vector<WorldThreadQueue<DeferredPacket>::Entry> packets = DeferredBotPackets().TakeBefore(before);
    std::vector<WorldThreadQueue<DeferredPacket>::Entry> chat = DeferredBotChat().TakeBefore(before);
    RunInSequence(packets, DeliverOrHold, chat, DeliverOrHold);

    // Issue #846: the tick's last drain reports what the caps dropped.
    if (endOfTick)
    {
        static DropReportLimiter chatDrops(kDropReportIntervalMs);
        static DropReportLimiter packetDrops(kDropReportIntervalMs);
        ReportDrops("chat lines", DeferredBotChat().TakeDroppedPerKey(), chatDrops);
        ReportDrops("packets", DeferredBotPackets().TakeDroppedPerKey(), packetDrops);
    }
}

void BotPacketAdapter::ClearDeferredPackets()
{
    DeferredBotChat().Clear();
    DeferredBotPackets().Clear();
}

bool BotPacketAdapter::CanPacketReceive(WorldSession* session, WorldPacket const& packet)
{
    if (!session || session->IsHeadless())
        return true;

    DispatchMasterIncoming(session, packet);
    if (packet.getOpcode() == CMSG_USE_ITEM)
        MaybeSendMimicSummary(session->GetPlayer(), packet);
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
        TB_LOG_DEBUG("TortoiseBots: ServerScript CanPacketReceive master opcode %u from %s", packet.getOpcode(), master->GetName());
    }

    if (!PlayerbotAI::HandlesMasterIncomingOpcode(packet.getOpcode()))
        return;

    // Issue #839 / #844: runs on whichever thread processes the master's
    // packet (world, map or AsyncPackets); same any-thread view and deferred
    // copy as CanPacketSend.
    bool const onWorldThread = OnWorldThread();
    std::shared_ptr<WorldPacket const> copy;
    for (uint64_t botGuid : BotManager::Instance().GetBotGuidsForMasterAnyThread(master->GetObjectGuid()))
    {
        if (onWorldThread)
        {
            if (PlayerbotAI* ai = FindBotAI(botGuid))
                ai->HandleMasterIncomingPacket(packet);
            continue;
        }
        if (!copy)
            copy = std::make_shared<WorldPacket const>(packet);
        DeferredBotPackets().Enqueue(botGuid, DeferredPacket{ PacketRoute::MasterIncoming, copy });
    }
}
void BotPacketAdapter::MaybeSendMimicSummary(Player* master, WorldPacket const& packet)
{
    if (!master)
        return;
    // Decode which bag slot the master used; unreadable packets are ignored.
    WorldPacket copy(packet);
    copy.rpos(0);
    uint8_t bagIndex = 0, slot = 0, spellCount = 0;
    if (copy.size() < 3)
        return;
    copy >> bagIndex >> slot >> spellCount;
    Item* masterItem = master->GetItemByPos(bagIndex, slot);
    if (!masterItem || !masterItem->GetProto())
        return;

    // CMSG_USE_ITEM is PACKET_PROCESS_MAP: this runs on the master's map
    // thread, and the world thread waits in MapManager::Update while map
    // threads run, so it can neither update nor delete an AI meanwhile
    // (issue #844: the one hook path that still reads AIs directly). The bot
    // list still comes from the any-thread view (issue #839).
    std::vector<uint64_t> bots = BotManager::Instance().GetBotGuidsForMasterAnyThread(master->GetObjectGuid());
    if (bots.size() <= kMimicSummaryBots)
        return;

    // Only mimic-enabled bots count; the first one relays the summary so the
    // master sees one line instead of one whisper per bot.
    uint32_t mimicCount = 0;
    PlayerbotAI* relayAi = nullptr;
    for (uint64_t botGuid : bots)
    {
        PlayerbotAI* ai = FindBotAI(botGuid);
        if (!ai || !ai->HasStrategy("mimic consumables", BotState::BOT_STATE_NON_COMBAT))
            continue;
        if (!relayAi)
            relayAi = ai;
        ++mimicCount;
    }
    if (!relayAi || mimicCount <= kMimicSummaryBots)
        return;

    ItemPrototype const* proto = masterItem->GetProto();
    std::string what = proto->Name1.empty() ? "consumables" : proto->Name1;
    std::ostringstream out;
    out << "[Raid Mimic]: " << mimicCount << " bots mimicked " << what << " with role equivalents";
    relayAi->TellPlayerNoFacing(master, out.str());
}


} // namespace TortoiseBots
