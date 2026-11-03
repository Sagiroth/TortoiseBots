#include "playerbot/playerbot.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIPackets.h"
#include "../../runtime/RemoteCommanderTasks.h"
#include "strategy/actions/MovementActions.h"
#include "strategy/values/LastSpellCastValue.h"
#include "strategy/values/TargetValue.h"
#include "BotActionLog.h"
#include "playerbot/WandPolicy.h"

using namespace ai;

void PacketHandlingHelper::DropQueuedOpcode(uint16 opcode)
{
    m_botPacketMutex.lock();
    // The stack drains LIFO; rebuilding from the bottom preserves the
    // survivors' relative order. Move-only packets travel by move.
    std::stack<std::unique_ptr<WorldPacket>> kept;
    while (!queue.empty())
    {
        if (queue.top()->getOpcode() != opcode)
            kept.push(std::move(queue.top()));
        queue.pop();
    }
    while (!kept.empty())
    {
        queue.push(std::move(kept.top()));
        kept.pop();
    }
    m_botPacketMutex.unlock();
}

void PacketHandlingHelper::AddPacket(const WorldPacket& packet)
{
    if (packet.empty() && packet.getOpcode() != MSG_RAID_READY_CHECK)
        return;

    m_botPacketMutex.lock(); //We are going to add packets. Stop any new handling and add them.

	if (handlers.find(packet.getOpcode()) != handlers.end())
        queue.push(std::make_unique<WorldPacket>(packet));

    m_botPacketMutex.unlock();
}

namespace ai
{
// The AI's packet handlers by opcode. One list feeds both the per-AI
// PacketHandlingHelper registration and the static opcode filters the packet
// hooks use before copying a packet (issue #844), so the two cannot drift.
PacketHandlerSpec const kMasterIncomingPacketHandlers[] =
{
    { CMSG_GAMEOBJ_USE, "use game object", false },
    { CMSG_AREATRIGGER, "area trigger", false },
    { CMSG_LOOT_ROLL, "loot roll", true },
    { CMSG_GOSSIP_HELLO, "gossip hello", false },
    { CMSG_QUESTGIVER_HELLO, "gossip hello", false },
    { CMSG_QUESTGIVER_COMPLETE_QUEST, "complete quest", false },
    { CMSG_QUESTGIVER_ACCEPT_QUEST, "accept quest", false },
    { CMSG_QUEST_CONFIRM_ACCEPT, "confirm quest", false },
    { CMSG_ACTIVATETAXI, "activate taxi", false },
    { CMSG_ACTIVATETAXIEXPRESS, "activate taxi", false },
    { CMSG_TAXICLEARALLNODES, "taxi done", false },
    { CMSG_TAXICLEARNODE, "taxi done", false },
    { CMSG_GROUP_UNINVITE, "uninvite", false },
    { CMSG_GROUP_UNINVITE_GUID, "uninvite guid", false },
    { CMSG_PUSHQUESTTOPARTY, "quest share", false },
    { CMSG_REPOP_REQUEST, "release spirit", false },
    { CMSG_RECLAIM_CORPSE, "revive from corpse", false },
    { CMSG_USE_ITEM, "master use item", false },
};
size_t const kMasterIncomingPacketHandlerCount = std::size(kMasterIncomingPacketHandlers);

PacketHandlerSpec const kBotOutgoingPacketHandlers[] =
{
    { SMSG_PETITION_SHOW_SIGNATURES, "petition offer", false },
    { SMSG_BATTLEFIELD_STATUS, "bg status", false },
    { SMSG_GROUP_INVITE, "group invite", false },
    { SMSG_GUILD_INVITE, "guild accept", false },
    { BUY_ERR_NOT_ENOUGHT_MONEY, "not enough money", false },
    { BUY_ERR_REPUTATION_REQUIRE, "not enough reputation", false },
    { SMSG_GROUP_SET_LEADER, "group set leader", false },
    { SMSG_FORCE_RUN_SPEED_CHANGE, "check mount state", false },
    { SMSG_RESURRECT_REQUEST, "resurrect request", false },
    { SMSG_INVENTORY_CHANGE_FAILURE, "cannot equip", false },
    { SMSG_TRADE_STATUS, "trade status", false },
    { SMSG_TRADE_STATUS_EXTENDED, "trade status extended", false },
    { SMSG_LOOT_RESPONSE, "loot response", true },
    { SMSG_QUESTUPDATE_ADD_KILL, "quest update add kill", true },
    { SMSG_QUESTUPDATE_ADD_ITEM, "quest update add item", true },
    { SMSG_QUESTUPDATE_FAILED, "quest update failed", true },
    { SMSG_QUESTUPDATE_FAILEDTIMER, "quest update failed timer", true },
    { SMSG_QUESTUPDATE_COMPLETE, "quest update complete", true },
    { SMSG_ITEM_PUSH_RESULT, "item push result", true },
    { SMSG_PARTY_COMMAND_RESULT, "party command", false },
    { SMSG_LEVELUP_INFO, "levelup", true },
    { SMSG_LOG_XPGAIN, "xpgain", true },
    { SMSG_PVP_CREDIT, "honorgain", true },
    { SMSG_TEXT_EMOTE, "receive text emote", false },
    { SMSG_EMOTE, "receive emote", false },
    { SMSG_LOOT_START_ROLL, "loot start roll", true },
    { SMSG_LOOT_ROLL, "loot vote", true },
    { SMSG_LOOT_ROLL_WON, "loot roll won", true },
    { SMSG_SUMMON_REQUEST, "summon request", false },
    { MSG_RAID_READY_CHECK, "ready check", false },
    { SMSG_QUEST_CONFIRM_ACCEPT, "confirm quest", false },
    { SMSG_QUESTGIVER_QUEST_DETAILS, "quest details", false },
    { SMSG_CAST_RESULT, "cast failed", false },
    { SMSG_DUEL_REQUESTED, "duel requested", false },
    { SMSG_INVENTORY_CHANGE_FAILURE, "inventory change failure", false },
};
size_t const kBotOutgoingPacketHandlerCount = std::size(kBotOutgoingPacketHandlers);

PacketHandlerSpec const kMasterOutgoingPacketHandlers[] =
{
    { SMSG_PARTY_COMMAND_RESULT, "party command", false },
};
size_t const kMasterOutgoingPacketHandlerCount = std::size(kMasterOutgoingPacketHandlers);

// Built once, read-only afterwards: a constant-time test on packet threads.

// Plain pointer + count, no array-reference templates and no defaulted
// initializer_list: MSVC 19.44 rejected the template form of these helpers
// (issue #918) while GCC accepted it.
OpcodeSet BuildOpcodeSet(PacketHandlerSpec const* specs, size_t count, std::initializer_list<uint16> extra)
{
    OpcodeSet set;
    for (size_t i = 0; i < count; ++i)
        set.set(specs[i].opcode);
    for (uint16 opcode : extra)
        set.set(opcode);
    return set;
}

void RegisterPacketHandlers(PacketHandlingHelper& helper, PacketHandlerSpec const* specs, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        helper.AddHandler(specs[i].opcode, specs[i].handler, specs[i].delay);
}
} // namespace


void PlayerbotAI::HandleBotOutgoingPacket(const WorldPacket& packet)
{
    //if (packet.empty())
    //    return;

	// Issue #844: a case here that does not end in botOutgoingPacketHandlers
	// (AddPacket) must also be added to the hand-kept extras list in
	// PlayerbotAI::HandlesBotOutgoingOpcode, or packet hooks off the world
	// thread never copy that opcode to the bot.
	switch (packet.getOpcode())
	{
	case SMSG_SPELL_FAILURE:
	{
		WorldPacket p(packet);
		p.rpos(0);
		ObjectGuid casterGuid;
        p >> casterGuid.ReadAsPacked();
		if (casterGuid != bot->getObjectGuid())
			return;

		uint32 spellId;
		p >> spellId;
		SpellInterrupted(spellId);
		return;
	}
	case SMSG_SPELL_DELAYED:
	{
		WorldPacket p(packet);
		p.rpos(0);
		ObjectGuid casterGuid;
        p >> casterGuid.ReadAsPacked();

		if (casterGuid != bot->getObjectGuid())
			return;

		uint32 delaytime;
		p >> delaytime;
		if (delaytime <= 1000)
            IncreaseAIInternalUpdateDelay(delaytime);
		return;
	}
    case SMSG_EMOTE: // do not react to NPC emotes
    {
        WorldPacket p(packet);
        ObjectGuid source;
        uint32 emoteId;
        p.rpos(0);
        p >> emoteId >> source;
        if (source.IsPlayer())
            botOutgoingPacketHandlers.AddPacket(packet);

        return;
    }
    case SMSG_MESSAGECHAT: // do not react to self or if not ready to reply
    {
        if (!AllowActivity())
            return;

        WorldPacket p(packet);
        if (!p.empty() && p.getOpcode() == SMSG_MESSAGECHAT)
        {
            p.rpos(0);
            uint8 msgtype, chatTag;
            uint32 lang, textLen, unused;
            ObjectGuid guid1, guid2;
            std::string chanName, message;
            p >> msgtype >> lang;

            // filter msg type
            switch (msgtype)
            {
            case CHAT_MSG_CHANNEL:
            case CHAT_MSG_SAY:
            case CHAT_MSG_PARTY:
            case CHAT_MSG_YELL:
            case CHAT_MSG_WHISPER:
            case CHAT_MSG_GUILD:
                break;
            default:
                return;
                break;
            }

            switch (msgtype)
            {
            case CHAT_MSG_SAY:
            case CHAT_MSG_PARTY:
            case CHAT_MSG_YELL:
                p >> guid1 >> guid2;
                break;
            case CHAT_MSG_CHANNEL:
                p >> chanName >> unused >> guid1;
                break;
            default:
                p >> guid1;
                break;
            }

            if (guid1.IsEmpty() || p.size() > 0x1000)
                return;

            p >> textLen >> message >> chatTag;

            if (m_recordIncommingMessages)
            {
                std::string recievedMessage = message;

                std::string senderName;
                if(!sObjectMgr.GetPlayerNameByGUID(guid1, senderName))
                    senderName = "unknown";

                std::string recievedChatType;

                //Add chat type prefix before recieved message. Ie. for Guilg "guild:".
                switch (msgtype)
                {
                    case CHAT_MSG_SAY:
                        recievedChatType = "says";
                        break;
                    case CHAT_MSG_YELL:
                        recievedChatType = "yells";
                        break;
                    case CHAT_MSG_PARTY:
                        recievedChatType = "party";
                        break;
                    case CHAT_MSG_GUILD:
                        recievedChatType = "guild";
                        break;
                    case CHAT_MSG_OFFICER:
                        recievedChatType = "officer";
                        break;
                    case CHAT_MSG_WHISPER:
                        //Get player name for whisper and add "<whispe:" before message.
                        recievedChatType = "whispers";
                        break;
                    case CHAT_MSG_CHANNEL: //Channel needs channel name to be added as prefix, so we add "c:" before channel name.
                        recievedChatType = chanName;
                        break;
                    default:
                        break;
                }

                recievedMessage = "[" + senderName + "] " + recievedChatType + ": " + recievedMessage;

                m_recordedMessages.push_back(recievedMessage);
            }

            if (guid1 != bot->getObjectGuid())
            {
                // Dispatch party chat and whispers from authorized real players (group master / members) to HandleCommand
                if ((msgtype == CHAT_MSG_PARTY || msgtype == CHAT_MSG_WHISPER) && lang != LANG_ADDON)
                {
                    if (Player* sender = sObjectAccessor.FindPlayer(guid1))
                    {
                        if (isRealPlayer_Helper(sender))
                        {
                            bool isAuthorized = (GetMaster() && GetMaster()->GetObjectGuid() == guid1) ||
                                                (bot->GetGroup() && bot->GetGroup()->IsMember(guid1));
                            if (isAuthorized)
                            {
                                HandleCommand(msgtype, message, *sender, lang);
                            }
                        }
                    }
                }
            }
        }

        return;
    }
    case SMSG_MOVE_KNOCK_BACK: // handle knockbacks
    {
        if (!sPlayerbotAIConfig.useKnockback || !HasPlayerNearby())
            return;

        WorldPacket p(packet);
        p.rpos(0);

        ObjectGuid guid;
        uint32 counter;
        float vcos, vsin, horizontalSpeed, verticalSpeed = 0.f;

        p >> guid.ReadAsPacked() >> counter >> vcos >> vsin >> horizontalSpeed >> verticalSpeed;
        verticalSpeed = -verticalSpeed;

        // stop casting
        InterruptSpell(false);

        // stop movement
        StopMoving();

        float timeToLand, distToLand, maxHeight;
        bool goodLanding = true;
        float angleRadian = (vsin > 0) ? acos(vcos) : -acos(vcos);
        float angle = angleRadian;
        std::vector<WorldPosition> path;
        WorldPosition dest_calculated = JumpAction::CalculateJumpParameters(WorldPosition(bot), bot, angle, verticalSpeed, horizontalSpeed, timeToLand, distToLand, maxHeight, goodLanding, path, 200.f);
        bool jumpInPlace = horizontalSpeed == 0.f;
        if (!dest_calculated)
        {
            sLog.outDetail("Knockback fail to calculate");
            return;
        }

        // set fall height for fall damage calculations
        bot->SetFallInformation(maxHeight);

        // fix height
        if (goodLanding)
        {
            float ox = dest_calculated.getX();
            float oy = dest_calculated.getY();
            float oz = dest_calculated.getZ();
            bot->UpdateAllowedPositionZ(ox, oy, oz);
            // set to fall after land if not at the ground
            if (fabs(oz - dest_calculated.getZ()) > 5.0f)
            {
                SetFallAfterJump();
            }
            else
            {
                dest_calculated = WorldPosition(dest_calculated.GetMapId(), ox, oy, oz);
            }

        }
        else
            SetFallAfterJump();


        // add moveflags
        bot->m_movementInfo.SetMovementFlags(MOVEFLAG_JUMPING);
        bot->m_movementInfo.AddMovementFlag(MOVEFLAG_BACKWARD);

        // send ack
        WorldPacket ack(CMSG_MOVE_KNOCK_BACK_ACK);
        bot->m_movementInfo.jump.cosAngle = vcos;
        bot->m_movementInfo.jump.sinAngle = vsin;
        bot->m_movementInfo.jump.zspeed = -verticalSpeed;
        bot->m_movementInfo.jump.xyspeed = horizontalSpeed;
        ack << bot->getObjectGuid();
        ack << uint32(0);
        ack << bot->m_movementInfo;
        bot->GetSession()->HandleMoveKnockBackAck(ack);

        // write jump time
        uint32 curTime = WorldTimer::getMSTime();
        jumpTime = curTime + sWorld.GetAverageDiff() + (uint32)(timeToLand * static_cast<uint32>(IN_MILLISECONDS)) + 1000;
        SetJumpDestination(dest_calculated);

        // set highest jump point to relocate
        WorldPosition highestPoint = dest_calculated;
        for (auto& point : path)
        {
            if (point.getZ() > highestPoint.getZ())
                highestPoint = point;
        }

        bot->Relocate(highestPoint.getX(), highestPoint.getY(), highestPoint.getZ());
        //bot->m_movementInfo.ChangePosition(dest_calculated.getX(), dest_calculated.getY(), dest_calculated.getZ(), bot->getOrientation());
        sLog.outDetail("%s: KNOCKBACK x: %f, y: %f, z: %f, time: %f, dist: %f, maxHeight: %f inPlace: %u, landTime: %u", bot->GetName(), dest_calculated.getX(), dest_calculated.getY(), dest_calculated.getZ(), timeToLand, distToLand, maxHeight, jumpInPlace, jumpTime);
        return;
    }
    case SMSG_CAST_RESULT:
    {
        // Failed crowd-control casts go to the bot action log (only open
        // with AiPlayerbot.EnableActionLog = 1), so a sheep that never lands
        // shows why. This core sends no SMSG_SPELL_FAILURE: a cast that
        // fails at the end of its cast bar, or is cancelled (moving,
        // interrupted), arrives here. 1.12 layout (Spell::SendCastResult):
        // uint32 spellId, uint8 status (0 = ok, 2 = failed), uint8 result.
        if (botdiag::BotActionLog::GetHandle(this) && packet.size() >= 6)
        {
            WorldPacket p(packet);
            p.rpos(0);
            uint32 spellId;
            uint8 status;
            p >> spellId >> status;
            if (status != 0)
            {
                uint8 result;
                p >> result;
                SpellEntry const* info = sServerFacade.LookupSpellInfo(spellId);
                if (info && FindNonCcTargetStrategy::IsCcSpell(info))
                {
                    // The cast's own target, from the start of this cast.
                    LastSpellCast const& last = aiObjectContext->GetValue<LastSpellCast&>("last spell cast")->Get();
                    Unit* ccTarget = last.id == spellId ? GetUnit(last.target) : nullptr;
                    botdiag::BotActionLog::Write(this, "CC_CAST_FAILED",
                        "spell=%u result=%u targetGuid=0x%llx dist=%.1f moving=%u",
                        spellId, (unsigned)result,
                        ccTarget ? (unsigned long long)ccTarget->getObjectGuid().GetRawValue() : 0ULL,
                        ccTarget ? bot->GetDistance(ccTarget) : -1.0f,
                        sServerFacade.isMoving(bot) ? 1u : 0u);
                }
            }
        }
        botOutgoingPacketHandlers.AddPacket(packet);
        return;
    }
	default:
		// log group invite arrivals so we
		// can verify the auto-accept handler fires. If we see this line in
		// the server log but no subsequent "AcceptInvitationAction" event,
		// the strategy/trigger is registered but the action isn't being
		// scheduled — points us at engine priority or trigger.Check() bugs.
		if (packet.getOpcode() == SMSG_GROUP_INVITE) {
			SC_LOG("bot=%s received SMSG_GROUP_INVITE — queued for 'group invite' trigger",
			       bot ? bot->GetName() : "(null)");
		}
		botOutgoingPacketHandlers.AddPacket(packet);
	}
}

void PlayerbotAI::HandleMasterIncomingPacket(const WorldPacket& packet)
{
    masterIncomingPacketHandlers.AddPacket(packet);
}

void PlayerbotAI::HandleMasterOutgoingPacket(const WorldPacket& packet)
{
    masterOutgoingPacketHandlers.AddPacket(packet);
}

namespace ai
{
// Issue #550: the core wraps GM chat (CHAT_TAG_GM) as `|c1049e6ff<message>|r`,
// so strip one whole-message colour wrapper before parsing commands. Item and
// spell links (`|cff...|Hitem:...|h[..]|h|r`) always carry an inner |H and are
// left untouched.
void StripGmBadgeColor(std::string& text)
{
    // `|c` + 8 hex digits + message + `|r`.
    if (text.size() < 12 || text.compare(0, 2, "|c") != 0 || text.compare(text.size() - 2, 2, "|r") != 0)
        return;
    for (size_t i = 2; i < 10; ++i)
    {
        char c = text[i];
        bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!isHex)
            return;
    }
    std::string inner = text.substr(10, text.size() - 12);
    if (inner.find("|c") != std::string::npos || inner.find("|H") != std::string::npos)
        return;
    text = inner;
}

} // namespace ai
